// Course-data path resolver (GOL-124). Course PNGs + JSON live under <repo>/courses/<id>/ in
// development, but in a cooked build the binary doesn't sit anywhere relative to <repo>.
// This helper tries multiple candidate base dirs and returns the first one that contains the
// course's heightmap.json (the canonical sentinel file).
//
// Editor path:    FPaths::ProjectDir() / "../../courses/<id>/"  (project at engine/Golfsim/)
// Cooked path:    FPaths::ProjectDir() / "../courses/<id>/"     (UAT stages courses/ next to the
//                                                                project dir via NonUFS stage hook)
//
// Returns the FULL course dir path (terminating slash baked in), or an empty FString if no
// candidate resolves. Caller appends concrete filenames.

#pragma once

#include "CoreMinimal.h"

namespace GolfsimPaths
{
	/** Returns absolute path to courses/<CourseId>/, or empty FString if nothing was found. */
	GOLFSIM_API FString ResolveCourseDataDir(const FString& CourseId);
}
