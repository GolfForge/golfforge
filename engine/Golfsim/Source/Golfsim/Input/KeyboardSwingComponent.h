// Virtua-Tennis-style swing meter for Game mode (GOL-67). Pure C++, no UObject -- the HUD
// owns one instance by value and ticks/feeds it. Easy to unit-test the math in isolation.
//
// 3-state machine driven by Space (both bars oscillate so each value has a visible peak the
// player times Space against -- the player can read the rhythm rather than counting frames):
//   Idle      --Space--> Power (triangle wave 0 -> 1 -> 0 over PowerOscillationPeriodS)
//   Power     --Space--> Accuracy (triangle wave 0 -> 1 -> 0 over AccuracyOscillationPeriodS)
//   Accuracy  --Space--> Idle (locks accuracy, publishes the resolved shot)

#pragma once

#include "CoreMinimal.h"

namespace GolfsimKeyboardSwing
{
	enum class EState : uint8
	{
		Idle = 0,
		Power,
		Accuracy,
	};

	struct GOLFSIM_API FState
	{
		EState State = EState::Idle;
		double Power = 0.0;          // [0, 1] -- locked at Press 2
		double Accuracy = 0.0;       // [0, 1] -- locked at Press 3
		double ElapsedInStateS = 0.0;
	};

	struct GOLFSIM_API FConfig
	{
		double PowerOscillationPeriodS = 0.8;     // full 0 -> 1 -> 0 cycle, repeats
		double AccuracyOscillationPeriodS = 1.5;  // full 0 -> 1 -> 0 cycle (slowed from 1.0s for readability)
		double SweetSpotLow = 0.80;               // accuracy band low end
		double SweetSpotHigh = 0.90;              // accuracy band high end
		double WhiffPowerThreshold = 0.10;        // below this -> no shot
	};

	struct GOLFSIM_API FClubPreset
	{
		FString Name;
		double NominalSpeedMps = 0.0;
		double LaunchDeg = 0.0;
		double SpinRpm = 0.0;
	};

	struct GOLFSIM_API FResolution
	{
		bool bWhiffed = false;
		double BallSpeedMps = 0.0;
		double LaunchAngleDeg = 0.0;
		double AzimuthDeg = 0.0;        // - left, + right (matches FShotTakenEvent)
		double BackspinRpm = 0.0;
		double SidespinRpm = 0.0;       // - draw, + fade
	};

	/** Triangle wave in [0, 1]: starts at 0, peaks at 1 at PeriodS/2, returns to 0 at PeriodS, repeats. */
	GOLFSIM_API double TriangleWave(double SecondsElapsed, double PeriodS);

	/** Advance one frame. Drives the live Power-bar fill or Accuracy oscillation; no state transitions here. */
	GOLFSIM_API void Tick(FState& S, const FConfig& C, double DeltaSeconds);

	/**
	 * Space press: transitions Idle -> Power -> Accuracy -> Idle. Returns true ONLY on the
	 * third press (the lock-fire moment); Out is populated then. Returns false on the first two
	 * presses (no shot yet) and on a whiff-on-press-3 (Out.bWhiffed = true to let the HUD play a
	 * chuff and still consume the event).
	 */
	GOLFSIM_API bool OnSpace(FState& S, const FConfig& C, const FClubPreset& Club, FResolution& Out);

	/** Pure: resolve a final shot from locked Power + Accuracy + club. Tested directly. */
	GOLFSIM_API FResolution ResolveShot(double Power, double Accuracy,
		const FClubPreset& Club, const FConfig& C = FConfig{});
}
