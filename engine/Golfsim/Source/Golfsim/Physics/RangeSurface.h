// Practice-range lie classification (GOL-9). Pure function over world-XY meters: which painted
// surface a point falls on. The range landscape is centered at the world origin (confirmed in
// engine/scripts/scatter_range_trees.py), so world meters == the splatmap's landscape-local frame.
//
// This MUST stay in lock-step with engine/scripts/build_range_splatmap.py::_classify -- same geometry
// constants, same layout. If you retune the splatmap, retune here. (Real OSM courses will get their
// lies from the shipped splatmap PNGs instead; that's a later, course-general path.)

#pragma once

#include "CoreMinimal.h"
#include "Physics/GroundRoll.h"   // EGolfLie

namespace GolfRangeSurface
{
	/** Lie at world point (X, Y) in METERS (origin = landscape center). Trees map to Rough. */
	EGolfLie ClassifyLie(double WorldXm, double WorldYm);
}
