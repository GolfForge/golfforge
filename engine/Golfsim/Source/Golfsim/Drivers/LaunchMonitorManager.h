// Owns the launch-monitor drivers and tracks which one is active (GOL-11). This is the seam a future
// settings UI drives: enumerate the available drivers, select the active one, observe its connection
// status. Drivers publish FShotTakenEvent to the EventBus (the manager provides bus access); the
// manager forwards the active driver's status to the UI via OnActiveStatusChanged.
//
// One active driver at a time. Square Omni etc. join by registering in Initialize.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "LaunchMonitorManager.generated.h"

class ULaunchMonitorDriver;
struct FShotTakenEvent;

/** Id + label + live-state for one driver, for a settings UI to render. */
struct FLaunchMonitorDriverInfo
{
	FString Id;
	FText DisplayName;
	bool bConnected = false;
};

UCLASS()
class GOLFSIM_API ULaunchMonitorManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Resolve the manager from any UObject with a world. Null outside a running game/PIE world. */
	static ULaunchMonitorManager* Get(const UObject* WorldContext);

	/** Drivers available to choose from (for a settings UI). */
	TArray<FLaunchMonitorDriverInfo> GetAvailableDrivers() const;
	FString GetActiveDriverId() const { return ActiveDriverId; }

	/** Make a driver active: disconnect any other, optionally connect the pick now. */
	void SetActiveDriver(const FString& Id, bool bConnectNow = true);
	void ConnectActive();
	void DisconnectActive();

	ULaunchMonitorDriver* GetActiveDriver() const;
	ULaunchMonitorDriver* FindDriver(const FString& Id) const;

	/** Drivers call this to publish a normalized shot onto the EventBus. */
	void PublishShot(const FShotTakenEvent& Shot);

	/** Fired when the active driver's connection status changes (HUD wires it to the panel dot). */
	TFunction<void(bool /*bConnected*/, const FString& /*Detail*/)> OnActiveStatusChanged;

private:
	void RegisterDriver(ULaunchMonitorDriver* Driver);

	UPROPERTY(Transient) TArray<TObjectPtr<ULaunchMonitorDriver>> Drivers;
	FString ActiveDriverId;
};
