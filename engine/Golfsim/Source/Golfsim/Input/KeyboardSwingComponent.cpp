#include "Input/KeyboardSwingComponent.h"

namespace GolfsimKeyboardSwing
{
	double TriangleWave(double SecondsElapsed, double PeriodS)
	{
		if (PeriodS <= 0.0) { return 0.0; }
		const double T = SecondsElapsed - FMath::FloorToDouble(SecondsElapsed / PeriodS) * PeriodS;
		const double Half = PeriodS * 0.5;
		return (T < Half) ? (T / Half) : (2.0 - T / Half);   // 0 -> 1 -> 0
	}

	void Tick(FState& S, const FConfig& C, double DeltaSeconds)
	{
		if (S.State == EState::Idle) { return; }
		S.ElapsedInStateS += DeltaSeconds;
		if (S.State == EState::Power)
		{
			S.Power = TriangleWave(S.ElapsedInStateS, C.PowerOscillationPeriodS);
		}
		else if (S.State == EState::Accuracy)
		{
			S.Accuracy = TriangleWave(S.ElapsedInStateS, C.AccuracyOscillationPeriodS);
		}
	}

	bool OnSpace(FState& S, const FConfig& C, const FClubPreset& Club, FResolution& Out)
	{
		switch (S.State)
		{
			case EState::Idle:
				S.State = EState::Power;
				S.Power = 0.0;
				S.Accuracy = 0.0;
				S.ElapsedInStateS = 0.0;
				return false;
			case EState::Power:
				// Lock power at its current bar value (driven by Tick), start the accuracy bar.
				S.State = EState::Accuracy;
				S.ElapsedInStateS = 0.0;
				S.Accuracy = 0.0;
				return false;
			case EState::Accuracy:
				S.State = EState::Idle;
				Out = ResolveShot(S.Power, S.Accuracy, Club, C);
				return true;   // shot resolved (HUD must check Out.bWhiffed for chuff vs publish)
		}
		return false;
	}

	FResolution ResolveShot(double Power, double Accuracy, const FClubPreset& Club, const FConfig& C)
	{
		FResolution R;

		// Whiff: power too low to make contact. Returns bWhiffed=true; other fields meaningless.
		if (Power < C.WhiffPowerThreshold)
		{
			R.bWhiffed = true;
			return R;
		}

		// Power scales ball speed linearly. Spin scales with power too (a half-power swing imparts
		// less backspin -- knuckles instead of a tour-rate spin).
		R.BallSpeedMps = Club.NominalSpeedMps * Power;
		R.LaunchAngleDeg = Club.LaunchDeg;
		R.BackspinRpm = Club.SpinRpm * Power;

		// =====================================================================================
		// GAME-MODE DIFFICULTY KNOBS  --  REVISIT when the single-player-on-courses epic lands.
		// =====================================================================================
		// These constants define how forgiving keyboard-swing Game mode feels. Current values are
		// tuned for ARCADE / CASUAL play on the practice range -- "slightly mistimed swing reads
		// as a playable shot with shape, not a boomerang". When single-player rounds on real
		// courses ship, we'll likely want a per-difficulty profile (Easy / Normal / Pro) selectable
		// in the round-setup screen, because:
		//   - Easy course play: even softer than these -- the player is also navigating wind,
		//     hazards, club selection. Penalty stack means swing penalty alone shouldn't dominate.
		//   - Pro course play: harder than these (closer to the original cut: 10 deg / 1200 rpm /
		//     0.55 launch scale on mishit, normalized over 0.20 span) -- skill ceiling matters
		//     when the player is competing for a leaderboard score.
		// The cleanest refactor will be to promote these to FConfig fields (one set per profile)
		// and have the HUD pick a profile based on the active round's difficulty setting. Don't do
		// that until we actually have a difficulty UI -- premature flexibility otherwise.
		//
		//   MaxAzimuthDeg       max degrees the ball flies off-target at full-penalty Accuracy
		//   SidespinPushRpm     max sidespin (curve) at full-penalty Accuracy
		//   MishitLaunchScale   multiplier on the club's launch angle when a mishit fires
		//   MishitSidespinRpm   sidespin override on mishit (independent of the linear ramp above)
		//   MishitLowAccuracy   below this -> mishit branch fires
		//   MishitHighAccuracy  above this -> mishit branch fires (currently 1.10 = unreachable;
		//                       see NormSpan note below)
		//   NormSpan            distance from sweet-spot edge that maps to MAX penalty. With the
		//                       bar's natural peak at 1.0 and SweetSpotHigh at 0.90, NormSpan=0.40
		//                       means the apex gives Above = 0.25 (a moderate pull, not a max
		//                       penalty). If we ever want hitting the apex to feel risky, narrow
		//                       this span.
		// =====================================================================================
		constexpr double MaxAzimuthDeg = 6.0;
		constexpr double SidespinPushRpm = 600.0;
		constexpr double MishitLaunchScale = 0.80;
		constexpr double MishitSidespinRpm = 1300.0;
		constexpr double MishitLowAccuracy = 0.15;
		constexpr double MishitHighAccuracy = 1.10;
		constexpr double NormSpan = 0.40;

		const double Low = C.SweetSpotLow;
		const double High = C.SweetSpotHigh;

		if (Accuracy >= Low && Accuracy <= High)
		{
			// Sweet spot: deterministic straight strike (HUD layers any per-shot jitter on top).
			R.AzimuthDeg = 0.0;
			R.SidespinRpm = 0.0;
		}
		else if (Accuracy < Low)
		{
			const double Below = FMath::Clamp((Low - Accuracy) / NormSpan, 0.0, 1.0);
			R.AzimuthDeg  = +MaxAzimuthDeg * Below;
			R.SidespinRpm = +SidespinPushRpm * Below;
			if (Accuracy < MishitLowAccuracy)
			{
				R.LaunchAngleDeg *= MishitLaunchScale;
				R.SidespinRpm = +MishitSidespinRpm;
			}
		}
		else   // Accuracy > High
		{
			const double Above = FMath::Clamp((Accuracy - High) / NormSpan, 0.0, 1.0);
			R.AzimuthDeg  = -MaxAzimuthDeg * Above;
			R.SidespinRpm = -SidespinPushRpm * Above;
			if (Accuracy > MishitHighAccuracy)
			{
				R.LaunchAngleDeg *= MishitLaunchScale;
				R.SidespinRpm = -MishitSidespinRpm;
			}
		}

		return R;
	}
}
