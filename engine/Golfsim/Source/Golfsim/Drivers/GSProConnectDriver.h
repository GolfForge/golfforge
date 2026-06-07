// Launch-monitor driver that speaks GSPro Open Connect V1 -- but as the SERVER (GSPro's role). GOL-178.
//
// One TCP listener on 127.0.0.1:921 lets us inherit the whole GSPro-connector ecosystem: Rapsodo
// MLM2PRO, FlightScope Mevo+, SkyTrak, Garmin R10, Square Omni, Foresight GC2 all ship community
// connectors that dial a GSPro server, push newline-delimited JSON shots, and expect a {Code:200} back.
// We are that server -- so those connectors work against GolfForge with no per-LM driver from us and
// "no GSPro subscription required". The protocol is open (the spec is verbatim: "the socket connection
// is open and does not require authentication"). Raw TCP, so it sidesteps GOL-36 (libwebsockets) too.
//
// LICENSE NOTE: same separate-process boundary as OpenFlight (Drivers/README.md) -- we implement the
// published wire protocol (facts, not code); the connectors stay third-party processes we talk to.
//
// Threading: a dedicated FRunnable owns the blocking socket I/O (accept + recv, newline framing) and
// parses each line with the pure static ParseShot. Parsed shots + status changes are marshaled to the
// game thread over SPSC queues, drained by an FTSTicker, because EventBus dispatch is synchronous on
// the game thread (the integrator touches the world). {Code:200} acks are written from the socket
// thread; the {Code:201} player push is written under a lock from SetSelectedClub / on client connect.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"   // FTSTicker -- drains the cross-thread queues on the game thread
#include "Drivers/LaunchMonitorDriver.h"
#include "GSProConnectDriver.generated.h"

struct FShotTakenEvent;
class FGSProConnectListener;

/**
 * Per-connector behavior profile. The open-source GSPro connectors do NOT all implement Open Connect
 * identically (squaregolf: newline framing + arm/re-arm + 201-as-ready; springbok: concatenated JSON +
 * no arming + 201-is-club-change-and-requires-Player.Club). One UGSProConnectDriver is registered once
 * per connector, each carrying its own profile, so a quirk fix for one connector can't break another.
 * Framing is intentionally NOT here -- the streaming object extractor handles both delimited and
 * concatenated JSON, so it's universal.
 */
struct FGSProConnectProfile
{
	FString Id = TEXT("gsproconnect");
	FText   DisplayName = NSLOCTEXT("Golfsim", "GSProConnectDriver", "GSPro Connect");
	bool    bArmModel = false;            // squaregolf: arm {Code:201} on connect + re-arm after club-data
	double  RearmSettleSeconds = 3.0;     // used only when bArmModel
	bool    bSendPlayerOnConnect = true;  // push a {Code:201} player/club on connect
	FString DefaultClub = TEXT("DR");     // Club sent in any 201 when none selected (springbok needs it present)
};

UCLASS()
class GOLFSIM_API UGSProConnectDriver : public ULaunchMonitorDriver
{
	GENERATED_BODY()

public:
	// Out-of-line dtor: deletes the forward-declared listener, so its teardown is emitted in the .cpp
	// (where FGSProConnectListener is complete), not inline / in the UHT-generated constructor.
	virtual ~UGSProConnectDriver() override;

	/** Set this entry's behavior profile (identity + per-connector quirks). All GSPro-connector entries
	 *  share this one driver but register separately, each with its own profile, so they can be
	 *  troubleshot + tuned in isolation. Only the active entry binds the port, so registering several
	 *  is safe. Call once at registration (before Connect). */
	void SetProfile(const FGSProConnectProfile& InProfile) { Profile = InProfile; }

	virtual FString GetDriverId() const override { return Profile.Id; }
	virtual FText GetDisplayName() const override { return Profile.DisplayName; }

	virtual void Connect() override;       // bind + listen on 921, spawn the listener thread
	virtual void Disconnect() override;    // stop the thread, close sockets
	virtual void InjectTestMessage(const FString& Payload) override;   // parse->publish, no socket
	virtual void SetSelectedClub(const FString& ClubDisplayName) override;   // pushes {Code:201,Player:{...}}

	/**
	 * Parse a GSPro Open Connect V1 message (one JSON object) into our envelope. Pure + defensive, so
	 * it's unit-testable headless. Returns false on a non-shot message (heartbeat, ContainsBallData
	 * false, or missing/zero BallData.Speed) or unusable input. Sets bOutSpinEstimated when backspin
	 * was filled by the launch-angle heuristic rather than measured/derived.
	 */
	static bool ParseShot(const FString& Json, FShotTakenEvent& Out, bool& bOutSpinEstimated);

	/**
	 * Extract complete top-level JSON objects from a receive buffer. Brace-depth scan, string/escape
	 * aware, so it handles BOTH newline-delimited (squaregolf) and raw-concatenated (springbok) framing.
	 * Appends each object's text to OutObjects; returns the number of leading chars consumed (up to the
	 * end of the last complete object) so the caller can chop them and keep any partial tail buffered.
	 * Pure -> unit-testable headless.
	 */
	static int32 ExtractJsonObjects(const FString& Buffer, TArray<FString>& OutObjects);

private:
	static bool ParseShotObject(const TSharedPtr<class FJsonObject>& Root, FShotTakenEvent& Out, bool& bOutSpinEstimated);

	// Drain the listener's shot + status queues on the game thread; publish + fire status there.
	bool DrainQueues(float DeltaTime);

	// Per-connector behavior profile (identity + quirks); set at registration via SetProfile.
	FGSProConnectProfile Profile;

	// Owned listener thread (raw ptr, not TUniquePtr: a forward-declared TUniquePtr member forces the
	// UHT-generated constructor to instantiate the deleter on the incomplete type). new'd in Connect,
	// deleted in Disconnect / the destructor.
	FGSProConnectListener* Listener = nullptr;
	FTSTicker::FDelegateHandle DrainHandle;
};
