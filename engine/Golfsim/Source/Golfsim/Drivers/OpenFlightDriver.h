// Launch-monitor driver for OpenFlight (open-source DIY Doppler-radar LM). GOL-11.
//
// LICENSE NOTE: OpenFlight and this project are both AGPL-3.0, so vendoring their source would be
// license-compatible. We still talk to OpenFlight ONLY as a separate process over its socket -- for
// architecture reasons now, not licensing (invariants #1/#2: the LM runs on the user's Pi/localhost
// and just publishes shots our driver normalizes into FShotTakenEvent). See Drivers/README.md.
//
// OpenFlight's realtime API is Socket.IO (Engine.IO v4) over Flask-SocketIO -- NOT a raw WebSocket.
// So this driver speaks a minimal Socket.IO/EIO4 layer on top of UE's IWebSocket: it connects to
// /socket.io/?EIO=4&transport=websocket, completes the open/connect handshake, answers ping/pong,
// and decodes `42["shot",{shot,stats}]` event frames into FShotTakenEvent. Reconnects with backoff.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"   // FTSTicker for one-shot reconnect timers
#include "Drivers/LaunchMonitorDriver.h"
#include "OpenFlightDriver.generated.h"

class IWebSocket;
struct FShotTakenEvent;

UCLASS()
class GOLFSIM_API UOpenFlightDriver : public ULaunchMonitorDriver
{
	GENERATED_BODY()

public:
	virtual FString GetDriverId() const override { return TEXT("openflight"); }
	virtual FText GetDisplayName() const override { return NSLOCTEXT("Golfsim", "OpenFlightDriver", "OpenFlight"); }

	virtual void Connect() override;
	virtual void Disconnect() override;
	virtual void InjectTestMessage(const FString& Payload) override;
	virtual void RequestSimulatedShot() override;   // sends Socket.IO 42["simulate_shot"] when connected
	virtual void SetSelectedClub(const FString& ClubDisplayName) override;   // sends 42["set_club",{club}]

	/**
	 * Parse an OpenFlight shot payload (JSON string) into our envelope. Pure + defensive: tolerant of
	 * the `{shot,stats}` wrapper (or data/payload, or a bare shot object) and missing optional fields,
	 * so it's unit-testable headless. Returns false on unusable input (not a shot). Sets
	 * bOutSpinEstimated when backspin was filled by the launch-angle heuristic rather than measured.
	 */
	static bool ParseShot(const FString& Json, FShotTakenEvent& Out, bool& bOutSpinEstimated);

private:
	// Field extraction from an already-parsed payload object (the live Socket.IO path uses this
	// directly; ParseShot(FString) parses then delegates here).
	static bool ParseShotObject(const TSharedPtr<class FJsonObject>& Payload, FShotTakenEvent& Out, bool& bOutSpinEstimated);

	void HandleConnected();
	void HandleConnectionError(const FString& Error);
	void HandleClosed(int32 StatusCode, const FString& Reason, bool bWasClean);
	void HandleMessage(const FString& Message);   // Engine.IO/Socket.IO frame decode

	void ScheduleReconnect();
	void CancelReconnect();
	FString ResolveUrl() const;

	TSharedPtr<IWebSocket> Socket;
	FString Url;
	bool bIntentionalDisconnect = false;
	int32 ReconnectAttempt = 0;
	FTSTicker::FDelegateHandle ReconnectHandle;
};
