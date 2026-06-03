#include "Game/CourseRegistry.h"

namespace GolfCourseRegistry
{
	TArray<FGolfCourseInfo> All()
	{
		TArray<FGolfCourseInfo> Out;

		// The one real, cooked course. Name stays trademark-neutral per GOL-20 even though the
		// terrain is geographically inspired. Metadata is static this milestone.
		Out.Add({ TEXT("golfforge-demo-black"), TEXT("GolfForge Demo Black"),
			TEXT("GolfForge demo course"), TEXT("Links"), 18, 72, TEXT("7,000"), 3, true });

		// Disabled placeholders -- the design's fictional tracks, shown greyed until they exist.
		Out.Add({ TEXT("coastal"),  TEXT("Coastal Links"),     TEXT("Pebble-grade coastal links"), TEXT("Links"),     18, 72, TEXT("7,040"), 4, false });
		Out.Add({ TEXT("pinewood"), TEXT("Pinewood National"), TEXT("Tree-lined parkland"),         TEXT("Parkland"),  18, 71, TEXT("6,870"), 3, false });
		Out.Add({ TEXT("desert"),   TEXT("Desert Ridge"),      TEXT("Target golf in the canyons"),  TEXT("Desert"),    18, 72, TEXT("7,210"), 5, false });
		Out.Add({ TEXT("highland"), TEXT("Highland Moor"),     TEXT("Windswept heathland"),         TEXT("Heathland"), 18, 70, TEXT("6,640"), 4, false });
		Out.Add({ TEXT("riverbend"),TEXT("Riverbend"),         TEXT("Water on every turn"),         TEXT("Resort"),    18, 72, TEXT("6,950"), 3, false });

		return Out;
	}
}
