// Single-player round state machine (GOL-116). Pure C++ -- no UObject, no UWorld -- so the
// state machine itself is unit-testable headlessly. The UGameInstanceSubsystem wrapper in
// Round/RoundSubsystem.{h,cpp} glues this to the EventBus and snaps tee/green/pin Z to the
// landscape at hole.start time.
//
// State machine contract:
//   StartRound      -> RoundStart + HoleStart(hole 1)
//   OnShotOutcome   -> increments stroke count; if >= MaxStrokesPerHole -> HoleComplete + advance
//   OnHoleHoled     -> HoleComplete + (next HoleStart, or RoundComplete on hole 18)
//   AbandonRound    -> clears state; nothing published
//
// Each entry point returns an FRoundStep describing what the subsystem should publish next, so
// tests assert on the step (no bus required) and the subsystem just relays.

#pragma once

#include "CoreMinimal.h"
#include "Game/GolfDifficulty.h"
#include "Round/RoundConfig.h"

namespace GolfsimRound
{
	/** Per-hole metadata baked from courses/<id>/hole.geojson. Z=0 from this layer -- the subsystem
	 *  snaps each XY to the landscape before publishing FHoleStartEvent. */
	struct GOLFSIM_API FHoleSpec
	{
		int32 Ref = 0;          // 1..18 from osm_tags.ref
		int32 Par = 0;
		int32 Handicap = 0;
		FString Name;           // e.g. "Black 1"
		FVector TeeWorldLoc   = FVector::ZeroVector;
		FVector GreenWorldLoc = FVector::ZeroVector;
		FVector PinWorldLoc   = FVector::ZeroVector;   // v1: same as GreenWorldLoc (LineString endpoint)
	};

	/** A real green outline from courses/<id>/green.geojson, projected to world cm (XY). Verts are the
	 *  outer ring with the duplicate closing vertex dropped. CentroidCm is the area-weighted centroid
	 *  (a guaranteed-ish on-green point used as the Random/Tournament fallback). GOL-191/192. */
	struct GOLFSIM_API FGreenPolygon
	{
		TArray<FVector2D> VertsCm;
		FVector2D CentroidCm = FVector2D::ZeroVector;
		int64 OsmWayId = 0;
	};

	/** A named pin sheet from courses/<id>/pins/<id>.json: per-hole pin world XY (cm). GOL-191. */
	struct GOLFSIM_API FPinSheet
	{
		FString Name;
		TMap<int32, FVector2D> PinXYByRefCm;   // keyed by hole Ref
	};

	struct GOLFSIM_API FRoundState
	{
		FString RoundId;
		FString CourseId;
		EGolfDifficulty Difficulty = EGolfDifficulty::Easy;
		FRoundConfig Config;                    // GOL-142: holes subset + hole-out rule (gimme radius)
		bool bActive = false;
		int32 HoleIndex = 0;                    // 0-based; Schedule[HoleIndex] is the active hole
		int32 StrokesThisHole = 0;
		TArray<int32> PerHoleStrokes;           // filled at HoleComplete; length grows to Schedule.Num()
		TArray<FHoleSpec> Schedule;             // immutable for the round's life
	};

	/** What the state machine wants the subsystem to publish next. The subsystem fans out to the
	 *  bus + (for HoleStart) snaps the tee/green/pin Z to ground before publishing. */
	struct GOLFSIM_API FRoundStep
	{
		bool bRoundStart = false;
		bool bHoleStart = false;
		bool bHoleComplete = false;
		bool bRoundComplete = false;

		// Payload mirrors the round events 1:1. The subsystem stamps RoundId/etc from FRoundState.
		int32 HoleRefForHoleStart = 0;
		int32 HoleRefForHoleComplete = 0;
		int32 StrokesForHoleComplete = 0;
		int32 ScoreVsParForHoleComplete = 0;
		int32 TotalStrokesForRoundComplete = 0;
		int32 TotalScoreVsParForRoundComplete = 0;
		TArray<int32> PerHoleStrokesForRoundComplete;
	};

	/** Per-hole stroke cap so a stuck player advances. Default = Par + 5 (par 4 -> 9 max).
	 *  Real play never hits this; the cap is a safety backstop. */
	GOLFSIM_API int32 MaxStrokesForPar(int32 Par);

	/** GOL-119: XY-only "is the ball close enough to the pin to count as holed out?" check. Z
	 *  ignored on purpose -- ball-rest and pin-rest can sit at different terrain elevations on
	 *  hilly greens; gimme is the horizontal "near the cup" check, not a 3D sphere. */
	GOLFSIM_API bool IsWithinGimme(const FVector& BallWorldLoc, const FVector& PinWorldLoc, double GimmeRadiusFt);

	/** UTC timestamp + 4 random hex chars. Filename- and log-safe. */
	GOLFSIM_API FString MakeRoundId();

	/** Derive the OSM `golf:course:name` filter from a CourseId. Convention: the trailing
	 *  hyphen-segment is the track name ("golfforge-demo-black" -> "Black"). Returns an empty
	 *  string if there's no hyphen (single-track courses; consumer treats as "no filter"). */
	GOLFSIM_API FString DeriveTrackName(const FString& CourseId);

	/** Read courses/<CourseId>/hole.geojson + heightmap.json; emit one FHoleSpec per `golf=hole`
	 *  LineString, world XY via the same bbox affine the pipeline uses (build_water_actors.py
	 *  _lonlat_to_world_xy: FLIP_Y=true, HalfXY=100800 cm; bbox from heightmap.json:bbox_wgs84).
	 *  TrackName filters features by `osm_tags["golf:course:name"]` (case-insensitive); empty =
	 *  no filter. Falls back to no-filter if a non-empty filter yields zero matches (logs a
	 *  warning) so single-track courses without the tag still work. Sorted by Ref ascending.
	 *  Returns false on missing/malformed files; OutErr explains. */
	GOLFSIM_API bool LoadHoleSchedule(const FString& CourseId, TArray<FHoleSpec>& Out, FString& OutErr);

	// --- GOL-191/192 pin-position system (all pure; reuse the hole.geojson affine) ----------------

	/** Parse green.geojson (FeatureCollection of golf=green Polygons) into world-cm outlines, using the
	 *  same course bbox/affine as the holes. Outer ring only; closing-duplicate vertex dropped. */
	GOLFSIM_API bool ParseGreenPolygonsJson(const FString& JsonText,
		double MinLon, double MinLat, double MaxLon, double MaxLat,
		TArray<FGreenPolygon>& Out, FString& OutErr);

	/** Parse a pin sheet ({name, pins:[{hole_ref, lon, lat}]}) into per-hole world XY, projecting the
	 *  lon/lat with the COURSE bbox (not any bbox the sheet may carry) so it lines up with the holes. */
	GOLFSIM_API bool ParsePinSheetJson(const FString& JsonText,
		double MinLon, double MinLat, double MaxLon, double MaxLat,
		FPinSheet& Out, FString& OutErr);

	/** Even-odd ray-cast point-in-polygon on a green outline (XY cm). */
	GOLFSIM_API bool PointInPolygonCm(const FVector2D& PointCm, const FGreenPolygon& Poly);

	/** A uniform-random point inside the green (bbox rejection sampling; falls back to the centroid). */
	GOLFSIM_API FVector2D RandomPointInGreen(const FGreenPolygon& Poly, FRandomStream& Stream);

	/** GOL-199: a point on the green a given distance (cm) from the pin -- the putt-from spot for a
	 *  putting drill. Random heading at DistCm clamped to stay on-green; falls back to any on-green
	 *  point >1 ft from the pin when the green is smaller than DistCm in every direction. */
	GOLFSIM_API FVector2D PointOnGreenAtDistance(const FGreenPolygon& Poly, const FVector2D& PinCm,
		double DistCm, FRandomStream& Stream);

	/** Index of the green polygon for a hole: the polygon containing GreenWorldLoc, else the nearest
	 *  centroid. INDEX_NONE only if Greens is empty. */
	GOLFSIM_API int32 MatchGreenToHole(const FHoleSpec& Hole, const TArray<FGreenPolygon>& Greens);

	/** Set each scheduled hole's PinWorldLoc.XY per Mode (Z stays 0 -- snapped at hole.start). Static
	 *  leaves the endpoint; Random samples in-green; Tournament reads the sheet (fallback: green
	 *  centroid, then the endpoint). No-ops gracefully when green/sheet data is missing. */
	GOLFSIM_API void ResolvePinPositions(TArray<FHoleSpec>& Schedule, EGolfPinMode Mode,
		const TArray<FGreenPolygon>& Greens, const FPinSheet* Sheet, FRandomStream& Stream);

	/** Read courses/<id>/green.geojson (+ heightmap.json bbox) -> world-cm green outlines. */
	GOLFSIM_API bool LoadGreenPolygons(const FString& CourseId, TArray<FGreenPolygon>& Out, FString& OutErr);

	/** Read courses/<id>/pins/<SetId>.json (+ heightmap.json bbox) -> a pin sheet. */
	GOLFSIM_API bool LoadPinSheet(const FString& CourseId, const FString& SetId, FPinSheet& Out, FString& OutErr);

	/** GOL-142: filter a full (Ref-ascending) schedule to the holes the round will actually play.
	 *  Full18 -> all; Front9 -> Ref 1..9; Back9 -> Ref 10..18; Custom -> entries whose Ref is in
	 *  Config.CustomHoles. Order is preserved (so the round plays them in Ref order and round.complete
	 *  fires after the last). Pure -- no world -- so it's unit-tested headlessly. */
	GOLFSIM_API TArray<FHoleSpec> SelectHoles(const TArray<FHoleSpec>& Full, const FRoundConfig& Config);

	/** GOL-142: the auto-hole-out radius for a round, in feet. With "everyone holes out" the round
	 *  keeps the difficulty's natural tolerance (DifficultyRadiusFt); a gimme concession only ever
	 *  loosens it (max), never tightens -- conceding a putt can't make holing harder. */
	GOLFSIM_API double EffectiveGimmeRadiusFt(const FRoundConfig& Config, double DifficultyRadiusFt);

	/** Same as above but takes the raw bytes + bbox directly -- the test helper. Public so the
	 *  geojson-shape tests don't need filesystem fixtures. */
	GOLFSIM_API bool ParseHoleScheduleJson(const FString& JsonText,
		double MinLon, double MinLat, double MaxLon, double MaxLat,
		const FString& TrackName,
		TArray<FHoleSpec>& Out, FString& OutErr);

	// --- State-machine entry points ---------------------------------------------------------

	/** Fills S, returns "publish RoundStart + first HoleStart". Schedule MUST be non-empty.
	 *  Config carries the round's hole-out rule (read later by the hole-out subsystem). */
	GOLFSIM_API FRoundStep StartRound(FRoundState& S, const FString& CourseId, EGolfDifficulty D,
		TArray<FHoleSpec> Schedule, const FRoundConfig& Config = FRoundConfig());

	/** Increments StrokesThisHole. Returns HoleComplete + advance if the cap trips, else empty step. */
	GOLFSIM_API FRoundStep OnShotOutcome(FRoundState& S);

	/** Closes the current hole (records its strokes, stamps ScoreVsPar). Returns HoleComplete plus
	 *  either next HoleStart or RoundComplete if last hole. No-op if !bActive (returns empty). */
	GOLFSIM_API FRoundStep OnHoleHoled(FRoundState& S);

	/** Clears state. Returns an empty step (round abandoned = no round.complete). No-op if !bActive. */
	GOLFSIM_API FRoundStep AbandonRound(FRoundState& S);
}
