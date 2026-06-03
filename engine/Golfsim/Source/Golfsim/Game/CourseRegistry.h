// Lightweight course registry (GOL-141, epic GOL-137) -- the card metadata the Round Setup wizard's
// Course step renders. This is static display data only (name, type flag, holes/par/yards, a 1..5
// difficulty meter, and whether the course is actually built/playable); it deliberately does NOT
// touch URoundSubsystem or load any course files -- that coupling stays on the round side, and the
// wizard remains a dumb view seeded with this array by the HUD.
//
// Seeded with the one cooked course (golfforge-demo-black, bAvailable=true) plus the design's
// fictional tracks shown as disabled placeholders. When a real course lands, flip bAvailable (and a
// later ticket can compute par/yards from courses/<id>/hole.geojson instead of the static values).

#pragma once

#include "CoreMinimal.h"
#include "CourseRegistry.generated.h"

USTRUCT()
struct FGolfCourseInfo
{
	GENERATED_BODY()

	UPROPERTY() FString Id;          // URoundSubsystem CourseId (e.g. "golfforge-demo-black")
	UPROPERTY() FString Name;        // display name
	UPROPERTY() FString Location;    // one-line descriptor under the name
	UPROPERTY() FString Type;        // course-type flag pill (Links / Parkland / Desert / ...)
	UPROPERTY() int32   Holes = 18;
	UPROPERTY() int32   Par = 72;
	UPROPERTY() FString Yards;       // pre-formatted (e.g. "7,040") -- display only
	UPROPERTY() int32   Difficulty = 3;  // 1..5, fills the dot meter
	UPROPERTY() bool    bAvailable = false;  // false = unbuilt placeholder (card shown disabled)
};

namespace GolfCourseRegistry
{
	// The Course-step card list: the cooked course first (selectable), then disabled placeholders.
	GOLFSIM_API TArray<FGolfCourseInfo> All();
}
