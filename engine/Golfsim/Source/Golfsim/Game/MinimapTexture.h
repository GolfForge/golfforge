// Minimap texture loader (GOL-209). Loads courses/<id>/minimap.png (the pipeline-baked HUD
// basemap, see docs/pipeline-data-contract.md "Minimap") into a transient UTexture2D for the
// round HUD's hole-map card. The repo's first runtime PNG -> GPU texture path -- the splatmap
// loader (Physics/CourseSurface) decodes PNGs too, but only keeps CPU byte masks.

#pragma once

#include "CoreMinimal.h"

class UTexture2D;

namespace GolfsimMinimap
{
	/**
	 * Load courses/<CourseId>/minimap.png into a transient BGRA8 sRGB texture. Returns nullptr
	 * with OutErr explaining (missing course dir / missing file / decode failure). The caller
	 * owns rooting the result (e.g. a UPROPERTY) -- transient textures are GC'd like any UObject.
	 */
	GOLFSIM_API UTexture2D* LoadMinimapTexture(const FString& CourseId, FString& OutErr);
}
