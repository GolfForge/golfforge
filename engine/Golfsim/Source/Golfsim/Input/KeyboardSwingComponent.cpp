#include "Input/KeyboardSwingComponent.h"

namespace GolfsimKeyboardSwing
{
	FSwingDifficultyProfile FSwingDifficultyProfile::Easy()
	{
		// Today's shipped values (post-2026-06-01 softening). Default-constructed profile == Easy.
		FSwingDifficultyProfile P;
		P.MaxAzimuthDeg     = 6.0;
		P.SidespinPushRpm   = 600.0;
		P.MishitLaunchScale = 0.80;
		P.NormSpan          = 0.40;
		P.GimmeRadiusFt     = 8.0;
		return P;
	}

	FSwingDifficultyProfile FSwingDifficultyProfile::Normal()
	{
		FSwingDifficultyProfile P;
		P.MaxAzimuthDeg     = 8.0;
		P.SidespinPushRpm   = 900.0;
		P.MishitLaunchScale = 0.65;
		P.NormSpan          = 0.30;
		P.GimmeRadiusFt     = 6.0;
		return P;
	}

	FSwingDifficultyProfile FSwingDifficultyProfile::Pro()
	{
		// Original alpha-1 cut, pre-softening.
		FSwingDifficultyProfile P;
		P.MaxAzimuthDeg     = 10.0;
		P.SidespinPushRpm   = 1200.0;
		P.MishitLaunchScale = 0.55;
		P.NormSpan          = 0.20;
		P.GimmeRadiusFt     = 3.0;
		return P;
	}

	FSwingDifficultyProfile FSwingDifficultyProfile::For(EGolfDifficulty D)
	{
		switch (D)
		{
			case EGolfDifficulty::Normal: return Normal();
			case EGolfDifficulty::Pro:    return Pro();
			case EGolfDifficulty::Easy:
			default:                      return Easy();
		}
	}

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
		// GAME-MODE DIFFICULTY KNOBS  --  promoted to FSwingDifficultyProfile in GOL-122.
		// =====================================================================================
		// These constants used to live inline here as `constexpr double`s. GOL-122 promoted four
		// of them (the actual difficulty-meaningful ones) to FSwingDifficultyProfile fields with
		// three named presets (Easy / Normal / Pro). The pre-round picker (GOL-121) will let the
		// player pick; URoundSubsystem (GOL-116) writes the chosen profile into the live FConfig.
		// Outside a round (range / Game mode default) the profile stays Easy.
		//
		//   field               Easy   Normal   Pro     meaning
		//   MaxAzimuthDeg       6.0    8.0      10.0    max deg off-target at full-penalty Accuracy
		//   SidespinPushRpm     600    900      1200    max sidespin (curve) at full-penalty Accuracy
		//   MishitLaunchScale   0.80   0.65     0.55    launch multiplier on a mishit (lower = flatter)
		//   NormSpan            0.40   0.30     0.20    distance from sweet-spot edge -> MAX penalty
		//   GimmeRadiusFt       8.0    6.0      3.0    GOL-119 hole-out radius (carried with the profile)
		//
		// History waypoints worth keeping:
		//   - "Original cut" (alpha-1): 10 / 1200 / 0.55 / 0.20 -- felt punishing; post-playtest
		//     softening 2026-06-01 lowered to today's Easy values (6 / 600 / 0.80 / 0.40).
		//   - The asymmetric below-sweet/above-sweet normalization that made hitting the bar's
		//     natural apex (Accuracy=1.0) an auto-mishit was replaced by symmetric NormSpan in
		//     the same softening pass; apex now reads as a moderate pull regardless of profile.
		//
		// MishitSidespinRpm / MishitLowAccuracy / MishitHighAccuracy stay as inline constants --
		// they're branch thresholds (not tuning knobs) and don't vary across profiles.
		// =====================================================================================
		const double MaxAzimuthDeg     = C.Profile.MaxAzimuthDeg;
		const double SidespinPushRpm   = C.Profile.SidespinPushRpm;
		const double MishitLaunchScale = C.Profile.MishitLaunchScale;
		const double NormSpan          = C.Profile.NormSpan;
		constexpr double MishitSidespinRpm = 1300.0;
		constexpr double MishitLowAccuracy = 0.15;
		constexpr double MishitHighAccuracy = 1.10;

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
