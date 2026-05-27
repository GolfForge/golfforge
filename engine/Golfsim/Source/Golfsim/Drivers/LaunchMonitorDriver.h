// Abstract base for a launch-monitor connector (GOL-11). Concrete drivers -- OpenFlight (WebSocket),
// Square Omni (BLE, later), ... -- implement the connect/disconnect lifecycle and normalize their
// device's shot data into an FShotTakenEvent, which the base publishes to the EventBus through the
// owning ULaunchMonitorManager. A future settings UI selects the active driver via the manager.
//
// The bus stays driver-agnostic: every driver publishes the same envelope (Source-tagged), so the
// sim never learns about specific hardware.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "LaunchMonitorDriver.generated.h"

struct FShotTakenEvent;
class ULaunchMonitorManager;

UCLASS(Abstract)
class GOLFSIM_API ULaunchMonitorDriver : public UObject
{
	GENERATED_BODY()

public:
	/** Stable machine id, e.g. "openflight". Used by config + the selection API. */
	virtual FString GetDriverId() const PURE_VIRTUAL(ULaunchMonitorDriver::GetDriverId, return FString(););
	/** Human label for the settings UI, e.g. "OpenFlight". */
	virtual FText GetDisplayName() const PURE_VIRTUAL(ULaunchMonitorDriver::GetDisplayName, return FText::GetEmpty(););

	virtual void Connect() {}
	virtual void Disconnect() {}
	virtual bool IsConnected() const { return bConnected; }

	/** Feed a raw device-shaped payload through the driver's parse->publish path (console / tests). */
	virtual void InjectTestMessage(const FString& /*Payload*/) {}

	/** Ask the device/service to emit a simulated shot, if it supports that while connected
	 *  (e.g. OpenFlight's mock mode). No-op by default. */
	virtual void RequestSimulatedShot() {}

	/** Fired on connect/disconnect/error. The manager forwards the active driver's status to the UI. */
	TFunction<void(bool /*bConnected*/, const FString& /*Detail*/)> OnStatusChanged;

	/** Set by the manager right after creation so the driver can publish + reach the world.
	 *  Body lives in the .cpp -- TWeakObjectPtr assignment needs the complete manager type. */
	void SetManager(ULaunchMonitorManager* InManager);

protected:
	/** Publish a normalized shot to the EventBus (routed through the manager, which owns bus access). */
	void PublishShot(const FShotTakenEvent& Shot);
	/** Update connection state + fire OnStatusChanged. */
	void SetStatus(bool bInConnected, const FString& Detail);

	TWeakObjectPtr<ULaunchMonitorManager> Manager;
	bool bConnected = false;
};
