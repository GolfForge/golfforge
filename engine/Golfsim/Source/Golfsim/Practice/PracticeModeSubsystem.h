// CTP practice-mode controller (GOL-73). A GameInstance subsystem that owns the active drill's
// config + session + RNG and publishes practice.shot_scored. The pure scoring/RNG lives in
// GolfsimPractice:: (Practice/PracticeMode.h); this is the thin UObject glue. AGolfRangeHUD owns the
// world side (pin actor, ball, settle timing) and feeds completed attempts in via RecordCarry /
// RecordPuttOut -- this subsystem deliberately does NOT subscribe to shot.outcome itself, because
// scoring needs the settled world XY of the ball + pin, which the HUD has and the launch-frame
// outcome event does not.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Practice/PracticeMode.h"
#include "PracticeModeSubsystem.generated.h"

class UEventBusSubsystem;

UCLASS()
class GOLFSIM_API UPracticeModeSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Resolve from any UObject with a world. Null outside a running game/PIE world. */
	static UPracticeModeSubsystem* Get(const UObject* WorldContext);

	GolfsimPractice::EPracticeMode GetMode() const { return Mode; }
	bool IsCtpActive() const { return Mode == GolfsimPractice::EPracticeMode::ClosestToPin; }

	const GolfsimPractice::FCtpConfig& GetConfig() const { return Config; }
	void SetConfig(const GolfsimPractice::FCtpConfig& InConfig) { Config = InConfig; }

	const GolfsimPractice::FCtpSession& GetSession() const { return Session; }

	/** Enter CTP: stamp the mode, reset the session, reseed the RNG. Call before the first NextPin. */
	void StartCtpSession(const GolfsimPractice::FCtpConfig& InConfig);

	/** Leave CTP back to free play (keeps the finished session readable until the next StartCtpSession). */
	void EndSession();

	/** Pick the next pin via the owned (seeded-at-session-start) RNG. */
	GolfsimPractice::FCtpPin NextPin();

	/** Record a settled carry-only attempt (distance lie->pin, meters) + publish practice.shot_scored. */
	void RecordCarry(double DistanceM);

	/** Record a completed putt-out attempt (total strokes + final lie->pin distance) + publish. */
	void RecordPuttOut(int32 Strokes, double FinalDistanceM);

private:
	void PublishScored(const GolfsimPractice::FCtpAttempt& Attempt);

	GolfsimPractice::EPracticeMode Mode = GolfsimPractice::EPracticeMode::Free;
	GolfsimPractice::FCtpConfig Config;
	GolfsimPractice::FCtpSession Session;
	FRandomStream Stream;

	TWeakObjectPtr<UEventBusSubsystem> EventBusWeak;
};
