#include "Drivers/LaunchMonitorManager.h"

#include "Drivers/LaunchMonitorDriver.h"
#include "Drivers/OpenFlightDriver.h"
#include "Drivers/GSProConnectDriver.h"
#include "Events/EventBusSubsystem.h"

#include "Engine/Engine.h"          // GEngine->GetWorldFromContextObject
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/ConfigCacheIni.h"    // GConfig

void ULaunchMonitorManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Register the known connectors. Square Omni (GOL-12) etc. add a RegisterDriver line here.
	RegisterDriver(NewObject<UOpenFlightDriver>(this));

	// GSPro Open Connect server (GOL-178): one TCP listener (921) inherits the whole connector
	// ecosystem. The open-source connectors don't implement Open Connect identically, so each is
	// registered as its own dropdown entry carrying a behavior PROFILE (framing is universal; arm
	// model / 201 rules / default club differ) -- tuning one connector can't break another. Only the
	// active entry binds the port. Adding another (skytrak, r10, gc2, ...) is one profile here. Opt-in
	// -- default active stays openflight; select via golfsim.LMSelect / the settings UI.
	{
		auto RegisterGSPro = [this](const FGSProConnectProfile& P)
		{
			UGSProConnectDriver* D = NewObject<UGSProConnectDriver>(this);
			D->SetProfile(P);
			RegisterDriver(D);
		};

		FGSProConnectProfile Generic;   // generic GSPro Open Connect (no arm model)
		Generic.Id = TEXT("gsproconnect");
		Generic.DisplayName = NSLOCTEXT("Golfsim", "GSProConnectDriver", "GSPro Connect");
		RegisterGSPro(Generic);

		FGSProConnectProfile SquareGolf;   // squaregolf-connector: arm-on-connect + re-arm after club-data
		SquareGolf.Id = TEXT("squaregolf");
		SquareGolf.DisplayName = NSLOCTEXT("Golfsim", "SquareGolfDriver", "Square Golf");
		SquareGolf.bArmModel = true;
		RegisterGSPro(SquareGolf);

		// Springbok connector (Rapsodo MLM2PRO + FlightScope Mevo+) is HIDDEN until validated (GOL-181):
		// the springbok project looks stale and its full release expects the GSPro APIv1 connect-window
		// handshake we don't implement yet -- we likely need an official path with them first. The profile
		// + parser quirks (concatenated JSON framing, lowercase "Backspin", 201-must-carry-Club, no
		// heartbeat) are all still in GSProConnectDriver, so re-enabling is just restoring this block once
		// the connect-window work lands. Do NOT re-add to the shipping dropdown before then.
		// FGSProConnectProfile Springbok;
		// Springbok.Id = TEXT("springbok");
		// Springbok.DisplayName = NSLOCTEXT("Golfsim", "SpringbokDriver", "Springbok (MLM2PRO / Mevo+)");
		// RegisterGSPro(Springbok);   // bArmModel stays false; 201-always-includes-Club is universal.
	}

	FString ConfiguredActive = TEXT("openflight");
	GConfig->GetString(TEXT("LaunchMonitor"), TEXT("ActiveDriver"), ConfiguredActive, GGameIni);
	bool bAutoConnect = false;
	GConfig->GetBool(TEXT("LaunchMonitor"), TEXT("bAutoConnect"), bAutoConnect, GGameIni);

	SetActiveDriver(ConfiguredActive, /*bConnectNow=*/bAutoConnect);

	UE_LOG(LogTemp, Display,
		TEXT("golfsim LM: manager initialized (active=%s autoconnect=%d, %d driver(s))"),
		*ActiveDriverId, bAutoConnect ? 1 : 0, Drivers.Num());
}

void ULaunchMonitorManager::Deinitialize()
{
	for (ULaunchMonitorDriver* Driver : Drivers)
	{
		if (Driver)
		{
			Driver->Disconnect();
		}
	}
	Super::Deinitialize();
}

ULaunchMonitorManager* ULaunchMonitorManager::Get(const UObject* WorldContext)
{
	if (!GEngine || !WorldContext)
	{
		return nullptr;
	}
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull))
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			return GI->GetSubsystem<ULaunchMonitorManager>();
		}
	}
	return nullptr;
}

void ULaunchMonitorManager::RegisterDriver(ULaunchMonitorDriver* Driver)
{
	if (!Driver)
	{
		return;
	}
	Driver->SetManager(this);

	// Forward this driver's status to the UI only while it's the active one.
	TWeakObjectPtr<ULaunchMonitorManager> WeakThis(this);
	TWeakObjectPtr<ULaunchMonitorDriver> WeakDriver(Driver);
	Driver->OnStatusChanged = [WeakThis, WeakDriver](bool bConnected, const FString& Detail)
	{
		ULaunchMonitorManager* M = WeakThis.Get();
		ULaunchMonitorDriver* D = WeakDriver.Get();
		if (M && D && D->GetDriverId() == M->ActiveDriverId && M->OnActiveStatusChanged)
		{
			// Drivers only know connected/not; map to the §6 status (Online/Off). Sim is a HUD-level
			// state (no driver active) the HUD applies directly, so it never arrives here.
			M->OnActiveStatusChanged(
				bConnected ? ELaunchMonitorStatus::Online : ELaunchMonitorStatus::Off, Detail);
		}
	};

	// Forward the active driver's ball-ready state to the HUD (GOL-186), same active-only gating.
	Driver->OnReadyChanged = [WeakThis, WeakDriver](bool bReady)
	{
		ULaunchMonitorManager* M = WeakThis.Get();
		ULaunchMonitorDriver* D = WeakDriver.Get();
		if (M && D && D->GetDriverId() == M->ActiveDriverId && M->OnActiveReadyChanged)
		{
			M->OnActiveReadyChanged(bReady);
		}
	};
	Drivers.Add(Driver);
}

TArray<FLaunchMonitorDriverInfo> ULaunchMonitorManager::GetAvailableDrivers() const
{
	TArray<FLaunchMonitorDriverInfo> Infos;
	Infos.Reserve(Drivers.Num());
	for (const ULaunchMonitorDriver* Driver : Drivers)
	{
		if (Driver)
		{
			const bool bConn = Driver->IsConnected();
			Infos.Add({ Driver->GetDriverId(), Driver->GetDisplayName(), bConn,
				bConn ? ELaunchMonitorStatus::Online : ELaunchMonitorStatus::Off });
		}
	}
	return Infos;
}

ELaunchMonitorStatus ULaunchMonitorManager::GetActiveStatus() const
{
	const ULaunchMonitorDriver* Active = GetActiveDriver();
	if (!Active) { return ELaunchMonitorStatus::Sim; }   // no driver selected = "Simulated (no device)"
	return Active->IsConnected() ? ELaunchMonitorStatus::Online : ELaunchMonitorStatus::Off;
}

ULaunchMonitorDriver* ULaunchMonitorManager::FindDriver(const FString& Id) const
{
	for (ULaunchMonitorDriver* Driver : Drivers)
	{
		if (Driver && Driver->GetDriverId() == Id)
		{
			return Driver;
		}
	}
	return nullptr;
}

ULaunchMonitorDriver* ULaunchMonitorManager::GetActiveDriver() const
{
	return FindDriver(ActiveDriverId);
}

void ULaunchMonitorManager::SetActiveDriver(const FString& Id, bool bConnectNow)
{
	// One active at a time: drop whatever's current before switching.
	if (ULaunchMonitorDriver* Prev = GetActiveDriver())
	{
		Prev->Disconnect();
	}
	ActiveDriverId = Id;

	if (bConnectNow)
	{
		ConnectActive();
	}
}

void ULaunchMonitorManager::ConnectActive()
{
	if (ULaunchMonitorDriver* Driver = GetActiveDriver())
	{
		Driver->Connect();
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("golfsim LM: no driver with id '%s'"), *ActiveDriverId);
	}
}

void ULaunchMonitorManager::DisconnectActive()
{
	if (ULaunchMonitorDriver* Driver = GetActiveDriver())
	{
		Driver->Disconnect();
	}
}

void ULaunchMonitorManager::PublishShot(const FShotTakenEvent& Shot)
{
	if (UEventBusSubsystem* Bus = UEventBusSubsystem::Get(this))
	{
		Bus->Publish(Shot);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("golfsim LM: no EventBus subsystem; shot dropped"));
	}
}
