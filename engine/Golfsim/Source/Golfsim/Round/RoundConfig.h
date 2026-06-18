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

// Pin-position mode (GOL-191/192). Resolved at round start (URoundSubsystem::StartRound) into each
// hole's FHoleSpec.PinWorldLoc; the pure resolver lives in GolfsimRound::ResolvePinPositions.
UENUM(BlueprintType)
enum class EGolfPinMode : uint8
{
	Static     = 0,   // pin at the hole.geojson green endpoint (default; today's behavior)
	Random     = 1,   // random point inside the hole's green.geojson polygon, fixed for the round
	Tournament = 2,   // positions from a named pin sheet (courses/<id>/pins/<PinSetId>.json)
};

// One player in the group (GOL-143). Single-player consumes Players[0]; 2..4 are a GOL-69 seam.
// TeeIndex: 0=Black 1=Blue 2=White 3=Red (single tee geometry today, so cosmetic). Handicap 0..54.
USTRUCT(BlueprintType)
struct GOLFSIM_API FRoundPlayer
{
	GENERATED_BODY()

	UPROPERTY() FString Name;
	UPROPERTY() int32   TeeIndex = 1;   // Blue by default (matches the design's player 1)
	UPROPERTY() int32   Handicap = 0;
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
	UPROPERTY() EGolfPinMode        PinMode = EGolfPinMode::Static;   // GOL-191/192 pin-position mode
	UPROPERTY() FString         PinSetId = TEXT("default");   // pin sheet id, used iff PinMode == Tournament
	UPROPERTY() int32           PlayerCount = 1;       // 1 live; 2..4 disabled this milestone
	UPROPERTY() TArray<FRoundPlayer> Players;          // the HUD persists Players[0] at Tee Off
};
