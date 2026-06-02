#include "Game/CoursePaths.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"

namespace GolfsimPaths
{
	FString ResolveCourseDataDir(const FString& CourseId)
	{
		// Sentinel file -- every cooked / live course ships a heightmap.json next to the splat PNGs.
		// If THIS file isn't present at the candidate base, the course data isn't there.
		const FString Sentinel = TEXT("heightmap.json");

		// Try each candidate base in order. First hit wins.
		const TArray<FString> CandidateBases = {
			FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("..") / TEXT("..")),   // editor: <repo>/
			FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("..")),                // cooked: <stage>/ (UAT stages courses/ via DirectoriesToAlwaysStageAsNonUFS)
			FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()),                             // defensive: courses/ alongside the project itself
			FPaths::ConvertRelativePathToFull(FPaths::LaunchDir() / TEXT("..") / TEXT("..") / TEXT("..")),   // defensive: cooked binary path
		};

		IFileManager& FM = IFileManager::Get();
		for (const FString& Base : CandidateBases)
		{
			const FString CourseDir = Base / TEXT("courses") / CourseId;
			if (FM.FileExists(*(CourseDir / Sentinel)))
			{
				return CourseDir;
			}
		}
		return FString();
	}
}
