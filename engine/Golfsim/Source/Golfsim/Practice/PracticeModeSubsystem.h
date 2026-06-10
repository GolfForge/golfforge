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
	bool IsPuttingActive() const { return Mode == GolfsimPractice::EPracticeMode::Putting; }

	const GolfsimPractice::FCtpConfig& GetConfig() const { return Config; }
	void SetConfig(const GolfsimPractice::FCtpConfig& InConfig) { Config = InConfig; }

	const GolfsimPractice::FCtpSession& GetSession() const { return Session; }

	/** Enter CTP: stamp the mode, reset the session, reseed the RNG. Call before the first NextPin. */
	void StartCtpSession(const GolfsimPractice::FCtpConfig& InConfig);

	/** Enter the putting drill (GOL-75): same as StartCtpSession but stamps Mode = Putting. The HUD
	 *  drives a putt-from-the-tee flow and reports holed attempts via RecordHoleOut. */
	void StartPuttingSession(const GolfsimPractice::FCtpConfig& InConfig);

	/** Leave the active drill back to free play (keeps the finished session readable until the next Start). */
	void EndSession();

	/** Pick the next pin via the owned (seeded-at-session-start) RNG. */
	GolfsimPractice::FCtpPin NextPin();

	/** Record a settled approach attempt (distance lie->pin, meters) + publish practice.shot_scored.
	 *  This is the closest-to-pin score; putt-out, when enabled, is unscored "play it out". */
	void RecordCarry(double DistanceM);

	/** Record a holed putting attempt (GOL-75): Putts = strokes taken to hole, FinalDistanceM = the
	 *  final lie->pin distance (~0 at the cup). Scores by strokes-to-hole + publishes practice.shot_scored. */
	void RecordHoleOut(int32 Putts, double FinalDistanceM);

private:
	void PublishScored(const GolfsimPractice::FCtpAttempt& Attempt);

	GolfsimPractice::EPracticeMode Mode = GolfsimPractice::EPracticeMode::Free;
	GolfsimPractice::FCtpConfig Config;
	GolfsimPractice::FCtpSession Session;
	FRandomStream Stream;

	TWeakObjectPtr<UEventBusSubsystem> EventBusWeak;
};
