// Round configuration (GOL-142) -- the Format-step selections the Round Setup wizard collects and
// hands to URoundSubsystem::StartRound. Mirrors the Game/GolfDifficulty.h pattern (UENUM/USTRUCT in
// one header so it can ride on events / persistence later); including the .generated.h does not make
// pure-C++ consumers UObjects, so RoundState.h can include this and stay headless-testable.
//
// This milestone (GOL-142) only consumes HolesMode + CustomHoles (the round plays the selected hole
// subset -- see GolfsimRound::SelectHoles). GameType / TurnOrder / HoleOutRule / GimmeFeet are seams:
// the wizard collects them, but scoring + multiplayer + gimme wiring land with GOL-69 -- the struct
// defaults to the single supported path so those options light up with zero UI change.

#pragma once

#include "CoreMinimal.h"
#include "RoundConfig.generated.h"

UENUM(BlueprintType)
enum class ERoundHolesMode : uint8
{
	Front9  = 0,   // Refs 1..9, round ends after hole 9
	Back9   = 1,   // Refs 10..18
	Full18  = 2,   // all holes (default)
	Custom  = 3,   // the explicit CustomHoles set, in Ref order
};

UENUM(BlueprintType)
enum class ERoundGameType : uint8
{
	Stroke     = 0,   // only supported path this milestone
	Stableford = 1,
	Match      = 2,
	Skins      = 3,
};

UENUM(BlueprintType)
enum class ETurnOrder : uint8
{
	PlayItOut = 0,   // each player finishes the hole before the next tees off (only supported path)
	Rotate    = 1,   // stroke-by-stroke (player away from the hole plays next)
};

UENUM(BlueprintType)
enum class EHoleOutRule : uint8
{
	HoleOut = 0,   // everyone holes out (only supported path)
	Gimme   = 1,   // concede inside GimmeFeet
};

USTRUCT(BlueprintType)
struct GOLFSIM_API FRoundConfig
{
	GENERATED_BODY()

	UPROPERTY() ERoundHolesMode HolesMode = ERoundHolesMode::Full18;
	UPROPERTY() TArray<int32>   CustomHoles;   // hole Refs, used iff HolesMode == Custom
	UPROPERTY() ERoundGameType  GameType = ERoundGameType::Stroke;
	UPROPERTY() ETurnOrder      TurnOrder = ETurnOrder::PlayItOut;
	UPROPERTY() EHoleOutRule    HoleOutRule = EHoleOutRule::HoleOut;
	UPROPERTY() int32           GimmeFeet = 3;
};
