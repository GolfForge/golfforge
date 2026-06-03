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

/** Connection state of a launch monitor, for the in-round HUD's §6 gating + status pill (GOL-145).
 *  Online = a real device is connected (game mode OFF -- the LM owns the shot stream). Sim = the
 *  built-in "Simulated (no device)" keyboard mode (no driver active). Pairing = an async connect is
 *  in flight. Off = a driver is selected but not connected. The status drives EInputMode in the HUD
 *  (Online -> Simulation, everything else -> Game) so adding a real driver (Square Omni / Blue Tees
 *  Rainmaker, incoming) needs no HUD changes -- it just appears with live status. */
enum class ELaunchMonitorStatus : uint8 { Sim, Off, Pairing, Online };

/** Id + label + live-state for one driver, for a settings UI to render. */
struct FLaunchMonitorDriverInfo
{
	FString Id;
	FText DisplayName;
	bool bConnected = false;
	ELaunchMonitorStatus Status = ELaunchMonitorStatus::Off;
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

	/** §6 status of the active selection: Sim when no driver is active ("Simulated (no device)"),
	 *  Online when the active driver is connected, else Off. (Pairing is reported transiently by the
	 *  HUD on a fresh pick; a future async driver can refine it.) */
	ELaunchMonitorStatus GetActiveStatus() const;

	/** Make a driver active: disconnect any other, optionally connect the pick now. */
	void SetActiveDriver(const FString& Id, bool bConnectNow = true);
	void ConnectActive();
	void DisconnectActive();

	ULaunchMonitorDriver* GetActiveDriver() const;
	ULaunchMonitorDriver* FindDriver(const FString& Id) const;

	/** Drivers call this to publish a normalized shot onto the EventBus. */
	void PublishShot(const FShotTakenEvent& Shot);

	/** Fired when the active driver's connection status changes (HUD wires it to the status pill). */
	TFunction<void(ELaunchMonitorStatus /*Status*/, const FString& /*Detail*/)> OnActiveStatusChanged;

private:
	void RegisterDriver(ULaunchMonitorDriver* Driver);

	UPROPERTY(Transient) TArray<TObjectPtr<ULaunchMonitorDriver>> Drivers;
	FString ActiveDriverId;
};
