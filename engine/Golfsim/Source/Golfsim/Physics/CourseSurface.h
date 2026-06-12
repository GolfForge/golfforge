// Course-side lie classification (GOL-40). Loads the shipped splatmap PNGs for a course
// (per docs/pipeline-data-contract.md) and turns a world-XY position in METERS into an
// EGolfLie. The course analog of Physics/RangeSurface.cpp -- same EGolfLie output, same
// SurfaceProvider seam (Events/EventBusSubsystem.h), but data-driven from disk instead of
// analytic geometry. Pure C++; the UWorldSubsystem wrapper (Course/CourseSurfaceSubsystem)
// is what binds an instance to the EventBus on course levels.
//
// Affine note (verified by GOL-85): the demo course landscape is default-stretched, centered
// at the world origin with half-extents +/-1008 m. The pipeline's lon/lat->pixel mapping
// (build_splatmap.py) AND the runtime lon/lat->world mapping (build_water_actors.py) both
// flip Y in their own frames; the two flips cancel, so world XY -> pixel XY is a single
// linear mapping with no extra inversion.

#pragma once

#include "CoreMinimal.h"
#include "Physics/GroundRoll.h"   // EGolfLie

class FCourseSurfaceSampler
{
public:
	FCourseSurfaceSampler() = default;

	/**
	 * Load splat_*.png + layer_*.png + splatmap.json from <repo>/courses/<CourseId>/.
	 * Repo root is derived from FPaths::ProjectDir() (engine/Golfsim/) -> ../.. .
	 * Returns true on full success; partial loads (some PNGs missing) keep bValid=true
	 * but log per-layer; a missing splatmap.json or zero successful PNGs fails the load.
	 */
	bool Load(const FString& CourseId);

	/** Classify a world XY in METERS. EGolfLie::Unknown for off-landscape or unloaded. */
	EGolfLie ClassifyAt(double WorldXm, double WorldYm) const;

	bool   IsValid() const { return bValid; }
	int32  GetSizePx() const { return SizePx; }

	// Half-extent of the landscape in cm. GOL-85: default-stretched 2016 m x 2016 m
	// regardless of bbox physical extent; bbox content stretches to fill it. Public:
	// the GOL-209 minimap projection (UI/HoleMapProjection.h) shares this georeference
	// (minimap.png has the same bbox/dims as the splatmap, so world XY maps onto both
	// rasters with this one constant).
	static constexpr double HalfXYCm = 100800.0;

private:
	struct FLayer
	{
		EGolfLie       Lie;        // mapped EGolfLie (Trees fold to Rough at classify time)
		FString        Filename;   // for diagnostics
		TArray<uint8>  Mask;       // SizePx * SizePx bytes, 8-bit grayscale
	};

	bool          bValid = false;
	int32         SizePx = 0;
	TArray<FLayer> Layers;   // priority order: Bunker, Green, Tee, Fairway, CartPath, Trees

	static constexpr uint8  MaskThresh = 128;
};
