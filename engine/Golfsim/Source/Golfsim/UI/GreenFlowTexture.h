// Flow-field texture for the in-world green break grid (GOL-203). Encodes a GolfMap::
// FGreenSlopeGrid into an NX x NY BGRA pixel block the M_GreenFlow material samples:
//   R, G = downhill direction XY, mapped [-1, 1] -> [0, 255]
//   B    = slope magnitude, SlopePct / SlopeMaxPct clamped to [0, 1] -> [0, 255]
//   A    = on-green mask (255 in, 0 out)
// Off-green / flat / invalid cells write the neutral pixel (128, 128, 0, 0) so bilinear
// filtering at the green edge decays toward zero-flow instead of bleeding garbage.
//
// BuildFlowPixels is pure (no UObject) and unit-tested headlessly (Tests/GreenFlowTextureTests
// .cpp); CreateFlowTexture is the thin UTexture2D wrapper (CreateTransient, linear, bilinear,
// clamped -- same pattern as Game/MinimapTexture).

#pragma once

#include "CoreMinimal.h"
#include "UI/HoleMapProjection.h"

class UTexture2D;

namespace GolfsimGreenFlow
{
	// Greens run ~0-6% grade; 8% saturates the heat ramp + flow speed.
	inline constexpr float DefaultSlopeMaxPct = 8.0f;

	// Fill OutPixels (resized to NX*NY, row-major matching the grid) from the slope grid.
	// Returns false (and empties OutPixels) on an empty/mismatched grid.
	GOLFSIM_API bool BuildFlowPixels(const GolfMap::FGreenSlopeGrid& Grid, float SlopeMaxPct,
		TArray<FColor>& OutPixels);

	// BuildFlowPixels + a transient PF_B8G8R8A8 texture (SRGB off, bilinear, clamp, NeverStream).
	// Null on failure.
	GOLFSIM_API UTexture2D* CreateFlowTexture(const GolfMap::FGreenSlopeGrid& Grid,
		float SlopeMaxPct = DefaultSlopeMaxPct);
}
