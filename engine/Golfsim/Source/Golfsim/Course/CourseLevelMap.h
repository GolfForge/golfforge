#pragma once

#include "CoreMinimal.h"

// Single source of truth for the course-id <-> level-name mapping. Shared by
// CourseSurfaceSubsystem (level -> course-id, for terrain/lie) and RoundSubsystem
// (course-id -> level, for OpenLevel) so the two never drift. Add one row per
// first-party course as it lands. (GOL-86 / GOL-91 will replace this hardcoded
// table with a data-driven course registry.)
namespace GolfsimCourseMap
{
	struct FCourseLevel { const TCHAR* CourseId; const TCHAR* LevelName; };

	inline const TArray<FCourseLevel>& Table()
	{
		static const TArray<FCourseLevel> Rows = {
			{ TEXT("golfforge-demo-black"),  TEXT("GolfForgeDemoBlack")  },
			{ TEXT("golfforge-demo-blue"),   TEXT("GolfForgeDemoBlue")   },
			{ TEXT("golfforge-demo-red"),    TEXT("GolfForgeDemoRed")    },
			{ TEXT("golfforge-demo-green"),  TEXT("GolfForgeDemoGreen")  },
			{ TEXT("golfforge-demo-yellow"), TEXT("GolfForgeDemoYellow") },
		};
		return Rows;
	}

	// course-id -> level asset name (exact match). Empty string if unknown.
	inline FString LevelNameForCourse(const FString& CourseId)
	{
		for (const FCourseLevel& R : Table())
		{
			if (CourseId == R.CourseId) { return FString(R.LevelName); }
		}
		return FString();
	}

	// (possibly PIE-prefixed `UEDPIE_<n>_<MapName>`) level name -> course-id.
	// Substring match sidesteps the PIE prefix without UWorld::StripPIEPrefix.
	inline FString CourseIdForLevel(const FString& MapNameRaw)
	{
		for (const FCourseLevel& R : Table())
		{
			if (MapNameRaw.Contains(R.LevelName)) { return FString(R.CourseId); }
		}
		return FString();
	}
}
