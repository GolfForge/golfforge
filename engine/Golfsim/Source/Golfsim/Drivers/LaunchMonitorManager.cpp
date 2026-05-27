#include "Drivers/LaunchMonitorManager.h"

#include "Drivers/LaunchMonitorDriver.h"
#include "Drivers/OpenFlightDriver.h"
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
			M->OnActiveStatusChanged(bConnected, Detail);
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
			Infos.Add({ Driver->GetDriverId(), Driver->GetDisplayName(), Driver->IsConnected() });
		}
	}
	return Infos;
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
