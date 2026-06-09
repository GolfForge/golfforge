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

		// The other four GolfForge demo tracks -- same geographic source as Black, trademark-neutral
		// names per GOL-20. All built + cooked. Par is from each track's hole.geojson; yards are
		// rough estimates.
		Out.Add({ TEXT("golfforge-demo-blue"),   TEXT("GolfForge Demo Blue"),   TEXT("GolfForge demo course"), TEXT("Parkland"), 18, 72, TEXT("6,700"), 3, true });
		Out.Add({ TEXT("golfforge-demo-red"),    TEXT("GolfForge Demo Red"),    TEXT("GolfForge demo course"), TEXT("Parkland"), 18, 70, TEXT("6,500"), 4, true });
		Out.Add({ TEXT("golfforge-demo-green"),  TEXT("GolfForge Demo Green"),  TEXT("GolfForge demo course"), TEXT("Parkland"), 18, 71, TEXT("6,200"), 2, true });
		Out.Add({ TEXT("golfforge-demo-yellow"), TEXT("GolfForge Demo Yellow"), TEXT("GolfForge demo course"), TEXT("Parkland"), 18, 67, TEXT("6,300"), 2, true });

		return Out;
	}
}
