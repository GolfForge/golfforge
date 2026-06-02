// Per-session shot history (GOL-65). Owns the table; the panel + GOL-66 data tab are readers.
//
// Subscribes to session.shot_outcome on the EventBus, builds an FShotHistoryEntry from each
// outcome, pushes to an in-memory TArray, and appends one JSONL line to
// <Project>/Saved/ShotHistory/<SessionId>.jsonl. SessionId is a UTC timestamp stamped at
// Initialize. Replay is out of scope -- this is table-only.
//
// The on-disk I/O is split into a GolfsimShotHistory:: namespace so the headless automation
// tests can exercise the round-trip without spinning up a UGameInstance.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Events/EventBusSubsystem.h"   // FGolfEventSubscription, EEventKind
#include "ShotHistorySubsystem.generated.h"

/**
 * A single shot's projected metrics, derived from FShotOutcomeEvent. All-UPROPERTY so
 * FJsonObjectConverter round-trips it without a hand-rolled serializer. SI internally; the
 * panel converts to display units (yd) at render time.
 */
USTRUCT()
struct GOLFSIM_API FShotHistoryEntry
{
	GENERATED_BODY()

	UPROPERTY() int32 ShotId = 0;          // session-local, 1-based
	UPROPERTY() int64 TsMs = 0;            // from FGolfEvent (unix ms)
	UPROPERTY() FString Source;            // "range-fire" / "manual-shot-dialog" / "openflight" / ...
	UPROPERTY() FString Club;
	UPROPERTY() double BallSpeedMps = 0.0;
	UPROPERTY() double LaunchAngleDeg = 0.0;
	UPROPERTY() double BackspinRpm = 0.0;
	UPROPERTY() bool   bSpinEstimated = false;
	UPROPERTY() double CarryM = 0.0;
	UPROPERTY() double TotalM = 0.0;
	UPROPERTY() double LateralOffsetM = 0.0;
	UPROPERTY() FString FinalLie;
};

/**
 * Pure-namespace file/JSON helpers. No engine subsystem needed -- the automation tests run
 * directly against these against a temp file. Keep them dependency-light (no UWorld, no GEngine).
 */
namespace GolfsimShotHistory
{
	/** Serialize one entry to a single-line JSON string (no trailing newline). */
	GOLFSIM_API FString EntryToJsonLine(const FShotHistoryEntry& Entry);

	/** Parse a JSON line into Out. Returns false for blank lines or truncated/malformed JSON. */
	GOLFSIM_API bool ParseJsonLine(const FString& Line, FShotHistoryEntry& Out);

	/** Append one entry to Path (creates the file + parent dir if missing). UTF-8, no BOM, newline-terminated. */
	GOLFSIM_API bool AppendEntryToFile(const FString& Path, const FShotHistoryEntry& Entry);

	/** Read every well-formed entry. A truncated trailing line is silently skipped (crash-tolerant). */
	GOLFSIM_API TArray<FShotHistoryEntry> ReadEntriesFromFile(const FString& Path);

	/** Truncate a session JSONL to zero bytes. Safer than delete (file may already be open by appender). */
	GOLFSIM_API bool TruncateFile(const FString& Path);

	/** Resolve the directory the session JSONLs live in: <Project>/Saved/ShotHistory/. */
	GOLFSIM_API FString GetSessionDir();

	/** Stamp a fresh session id from UtcNow (e.g. "2026-06-01T18-34-56"). Filename-safe. */
	GOLFSIM_API FString MakeSessionId();
}

DECLARE_MULTICAST_DELEGATE(FOnShotHistoryChanged);

/**
 * GameInstance subsystem that owns the current session's TArray of entries + on-disk JSONL.
 * Depends on UEventBusSubsystem (initialized first via Collection.InitializeDependency).
 * Panel reads via GetEntries() + listens to OnEntriesChanged; past sessions are loaded
 * on-demand via LoadSession(id).
 */
UCLASS()
class GOLFSIM_API UShotHistorySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Resolve from any UObject with a world. Null outside a running game/PIE world. */
	static UShotHistorySubsystem* Get(const UObject* WorldContext);

	const TArray<FShotHistoryEntry>& GetEntries() const { return Entries; }
	const FString& GetSessionId() const { return SessionId; }

	/** Past session ids on disk (newest first), excluding the current session. */
	TArray<FString> ListPastSessionIds() const;

	/** Parse a past session's JSONL. Empty array if missing. */
	TArray<FShotHistoryEntry> LoadSession(const FString& InSessionId) const;

	/** Empty the in-memory list AND truncate the current session's JSONL on disk to 0 bytes. */
	void Clear();

	/** Broadcast on every Append and on Clear. Readers connect/disconnect in their lifetime. */
	FOnShotHistoryChanged OnEntriesChanged;

private:
	void OnShotOutcome(const FGolfEvent& Event);
	void AppendOne(const FShotHistoryEntry& Entry);

	UPROPERTY(Transient) TArray<FShotHistoryEntry> Entries;
	FString SessionId;
	FString SessionJsonlPath;
	int32 NextShotId = 1;

	TWeakObjectPtr<UEventBusSubsystem> EventBusWeak;
	FGolfEventSubscription OutcomeSub;
};
