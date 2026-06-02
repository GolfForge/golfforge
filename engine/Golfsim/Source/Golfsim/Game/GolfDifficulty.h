// Shared difficulty enum used by both the round protocol (FRoundStartEvent travels on the bus
// stamped with one of these) AND the gameplay tuning (FSwingDifficultyProfile selects on it,
// future hole-out gimme radius reads from it). One header so adding a difficulty later -- "Tour"
// or whatever -- is a single-line change visible to both sides.
//
// UENUM so it lives inside FRoundStartEvent as a UPROPERTY (event reflection / future
// JSON-lines persistence). Pure C++ consumers in KeyboardSwingComponent::FConfig store it as a
// plain enum-class value; including the .generated.h does not make them UObjects.

#pragma once

#include "CoreMinimal.h"
#include "GolfDifficulty.generated.h"

UENUM(BlueprintType)
enum class EGolfDifficulty : uint8
{
	Easy   = 0,
	Normal = 1,
	Pro    = 2,
};
