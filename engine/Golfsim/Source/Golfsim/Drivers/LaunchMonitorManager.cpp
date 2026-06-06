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
	// ecosystem (MLM2PRO, Mevo+, SkyTrak, R10, Square Omni/Square Golf, GC2). We register it as
	// separate dropdown entries per connector -- all share UGSProConnectDriver (same protocol) but
	// appear individually so a specific connector can be troubleshot in isolation; only the active
	// entry binds the port. Adding another (mlm2pro, skytrak, ...) is one line here. Opt-in -- the
	// default active driver stays openflight; select via golfsim.LMSelect / the settings UI.
	{
		UGSProConnectDriver* GSPro = NewObject<UGSProConnectDriver>(this);
		GSPro->SetIdentity(TEXT("gsproconnect"), NSLOCTEXT("Golfsim", "GSProConnectDriver", "GSPro Connect"));
		RegisterDriver(GSPro);

		UGSProConnectDriver* SquareGolf = NewObject<UGSProConnectDriver>(this);
		SquareGolf->SetIdentity(TEXT("squaregolf"), NSLOCTEXT("Golfsim", "SquareGolfDriver", "Square Golf"));
		RegisterDriver(SquareGolf);
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
