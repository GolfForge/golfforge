#include "Round/RoundState.h"

#include "Game/CoursePaths.h"   // GOL-124: cooked-vs-editor course data path resolver
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace GolfsimRound
{
	namespace
	{
		// Mirrors FCourseSurfaceSampler::HalfXYCm + the FLIP_Y=true convention in
		// build_water_actors.py:_lonlat_to_world_xy. Cm because everything UE-side is cm.
		constexpr double HalfXYCm = 100800.0;
		constexpr bool   FlipY    = true;
		constexpr bool   FlipX    = false;
		constexpr bool   SwapXY   = false;

		FVector LonLatToWorldCm(double Lon, double Lat,
			double MinLon, double MinLat, double MaxLon, double MaxLat)
		{
			const double U = (Lon - MinLon) / (MaxLon - MinLon);
			const double V = (Lat - MinLat) / (MaxLat - MinLat);
			double Ux = FlipX ? (1.0 - U) : U;
			double Vy = FlipY ? (1.0 - V) : V;
			if (SwapXY) { Swap(Ux, Vy); }
			const double X = (Ux * 2.0 - 1.0) * HalfXYCm;
			const double Y = (Vy * 2.0 - 1.0) * HalfXYCm;
			return FVector(X, Y, 0.0);
		}

		bool ReadBboxFromHeightmapJson(const FString& JsonText,
			double& MinLon, double& MinLat, double& MaxLon, double& MaxLat, FString& OutErr)
		{
			TSharedPtr<FJsonObject> Root;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
			if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
			{
				OutErr = TEXT("heightmap.json parse failed");
				return false;
			}
			const TArray<TSharedPtr<FJsonValue>>* Bbox = nullptr;
			if (!Root->TryGetArrayField(TEXT("bbox_wgs84"), Bbox) || !Bbox || Bbox->Num() != 4)
			{
				OutErr = TEXT("heightmap.json missing bbox_wgs84 [minlon, minlat, maxlon, maxlat]");
				return false;
			}
			MinLon = (*Bbox)[0]->AsNumber();
			MinLat = (*Bbox)[1]->AsNumber();
			MaxLon = (*Bbox)[2]->AsNumber();
			MaxLat = (*Bbox)[3]->AsNumber();
			return true;
		}

		// Best-effort int parse of an FString tag value (osm_tags are all strings).
		int32 ParseIntTag(const TSharedPtr<FJsonObject>& Tags, const TCHAR* Key, int32 Fallback)
		{
			FString Str;
			if (!Tags.IsValid() || !Tags->TryGetStringField(Key, Str)) { return Fallback; }
			return FCString::Atoi(*Str);
		}
	}

	int32 MaxStrokesForPar(int32 Par)
	{
		return FMath::Max(Par, 1) + 5;
	}

	bool IsWithinGimme(const FVector& BallWorldLoc, const FVector& PinWorldLoc, double GimmeRadiusFt)
	{
		// GimmeRadiusFt <= 0 disables gimme (the ticket's "must true-hole with a putter" mode --
		// auto-detection never fires; cup physics + putter-required would take over instead).
		if (GimmeRadiusFt <= 0.0) { return false; }
		constexpr double CmPerFt = 30.48;
		const double GimmeCm = GimmeRadiusFt * CmPerFt;
		const FVector2D DeltaXY(BallWorldLoc.X - PinWorldLoc.X, BallWorldLoc.Y - PinWorldLoc.Y);
		return DeltaXY.Size() <= GimmeCm;
	}

	FString MakeRoundId()
	{
		// Same shape as a UUID v4 string; the leading timestamp keeps log lines sortable.
		const FString Ts = FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%S"));
		const FString Guid = FGuid::NewGuid().ToString(EGuidFormats::DigitsLower).Mid(0, 8);
		return Ts + TEXT("-") + Guid;
	}

	FString DeriveTrackName(const FString& CourseId)
	{
		// OldAndre's hole.geojson covers the whole St Andrews complex (Old/New/Jubilee/...); the
		// Old Course holes are tagged course:name="Old". The id has no "-<track>" suffix to derive
		// from, so map it explicitly so a round filters to the Old's 18, not all 80+ holes. (GOL-205.)
		if (CourseId == TEXT("oldandre")) { return TEXT("Old"); }
		// East Long Island Hills: the multi-course OSM extract is spatially filtered in the pipeline to
		// the championship property's 18 holes, tagged golf:course:name="EastLongIsland". (Suffix-derive
		// would give "Hills", which matches nothing.)
		if (CourseId == TEXT("east-long-island-hills")) { return TEXT("EastLongIsland"); }
		int32 LastDash = INDEX_NONE;
		if (!CourseId.FindLastChar('-', LastDash)) { return FString(); }
		FString Suffix = CourseId.Mid(LastDash + 1);
		if (Suffix.IsEmpty()) { return FString(); }
		// Capitalize first letter so it matches OSM's "Black" / "Red" / "Green" / etc.
		Suffix[0] = FChar::ToUpper(Suffix[0]);
		return Suffix;
	}

	bool ParseHoleScheduleJson(const FString& JsonText,
		double MinLon, double MinLat, double MaxLon, double MaxLat,
		const FString& TrackName,
		TArray<FHoleSpec>& Out, FString& OutErr)
	{
		Out.Reset();
		TSharedPtr<FJsonObject> Root;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutErr = TEXT("hole.geojson parse failed");
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* Features = nullptr;
		if (!Root->TryGetArrayField(TEXT("features"), Features))
		{
			OutErr = TEXT("hole.geojson missing features array");
			return false;
		}

		// Two-pass: collect filtered + unfiltered separately so we can fall back if the filter
		// over-zealously rejects every feature (a course whose geojson lacks the tag).
		TArray<FHoleSpec> Matched;
		TArray<FHoleSpec> AllHoles;

		for (const TSharedPtr<FJsonValue>& FVal : *Features)
		{
			const TSharedPtr<FJsonObject> Feat = FVal->AsObject();
			if (!Feat.IsValid()) { continue; }

			const TSharedPtr<FJsonObject> Props = Feat->GetObjectField(TEXT("properties"));
			const TSharedPtr<FJsonObject> Geom  = Feat->GetObjectField(TEXT("geometry"));
			if (!Props.IsValid() || !Geom.IsValid()) { continue; }

			// We only care about golf=hole linestrings; skip everything else defensively.
			FString GeomType;
			Geom->TryGetStringField(TEXT("type"), GeomType);
			if (!GeomType.Equals(TEXT("LineString"), ESearchCase::IgnoreCase)) { continue; }

			const TSharedPtr<FJsonObject> Tags = Props->GetObjectField(TEXT("osm_tags"));
			if (!Tags.IsValid()) { continue; }
			FString Golf;
			Tags->TryGetStringField(TEXT("golf"), Golf);
			if (!Golf.Equals(TEXT("hole"), ESearchCase::IgnoreCase)) { continue; }

			const TArray<TSharedPtr<FJsonValue>>* Coords = nullptr;
			if (!Geom->TryGetArrayField(TEXT("coordinates"), Coords) || !Coords || Coords->Num() < 2)
			{
				continue;
			}

			FHoleSpec H;
			H.Ref      = ParseIntTag(Tags, TEXT("ref"),      0);
			H.Par      = ParseIntTag(Tags, TEXT("par"),      4);
			H.Handicap = ParseIntTag(Tags, TEXT("handicap"), 0);
			Tags->TryGetStringField(TEXT("name"), H.Name);

			FString HoleTrack;
			Tags->TryGetStringField(TEXT("golf:course:name"), HoleTrack);
			// Fallback: many multi-course OSM extracts tag holes with course:name instead of
			// golf:course:name (e.g. St Andrews / OldAndre, where the Old holes are course:name="Old").
			if (HoleTrack.IsEmpty()) { Tags->TryGetStringField(TEXT("course:name"), HoleTrack); }
			const bool bMatchesTrack = TrackName.IsEmpty()
				|| HoleTrack.Equals(TrackName, ESearchCase::IgnoreCase);

			auto ReadLonLat = [&](const TSharedPtr<FJsonValue>& V, double& Lon, double& Lat) -> bool
			{
				const TArray<TSharedPtr<FJsonValue>>* Pair = nullptr;
				if (!V->TryGetArray(Pair) || !Pair || Pair->Num() < 2) { return false; }
				Lon = (*Pair)[0]->AsNumber();
				Lat = (*Pair)[1]->AsNumber();
				return true;
			};

			double TeeLon, TeeLat, GreenLon, GreenLat;
			if (!ReadLonLat((*Coords)[0],          TeeLon,   TeeLat))   { continue; }
			if (!ReadLonLat((*Coords).Last(),      GreenLon, GreenLat)) { continue; }

			H.TeeWorldLoc   = LonLatToWorldCm(TeeLon,   TeeLat,   MinLon, MinLat, MaxLon, MaxLat);
			H.GreenWorldLoc = LonLatToWorldCm(GreenLon, GreenLat, MinLon, MinLat, MaxLon, MaxLat);
			H.PinWorldLoc   = H.GreenWorldLoc;   // v1: LineString endpoint; future bump = green centroid

			AllHoles.Add(H);
			if (bMatchesTrack) { Matched.Add(MoveTemp(H)); }
		}

		if (AllHoles.Num() == 0)
		{
			OutErr = TEXT("hole.geojson had no golf=hole linestrings");
			return false;
		}

		if (!TrackName.IsEmpty() && Matched.Num() == 0)
		{
			// Single-track course whose geojson doesn't tag golf:course:name -- expected; Display
			// (not Warning) so automation tests don't false-positive on the informational log.
			UE_LOG(LogTemp, Display,
				TEXT("GolfsimRound::ParseHoleScheduleJson: TrackName='%s' matched 0 features; falling back to all %d holes (geojson may not tag golf:course:name)"),
				*TrackName, AllHoles.Num());
			Out = MoveTemp(AllHoles);
		}
		else if (!TrackName.IsEmpty())
		{
			Out = MoveTemp(Matched);
		}
		else
		{
			Out = MoveTemp(AllHoles);
		}

		Out.Sort([](const FHoleSpec& A, const FHoleSpec& B) { return A.Ref < B.Ref; });
		return true;
	}

	bool LoadHoleSchedule(const FString& CourseId, TArray<FHoleSpec>& Out, FString& OutErr)
	{
		Out.Reset();
		const FString CourseDir = GolfsimPaths::ResolveCourseDataDir(CourseId);
		if (CourseDir.IsEmpty())
		{
			OutErr = FString::Printf(TEXT("no courses/%s data dir found in any candidate base (editor / cooked)"), *CourseId);
			return false;
		}
		const FString HeightmapPath = CourseDir / TEXT("heightmap.json");
		const FString HolePath      = CourseDir / TEXT("hole.geojson");

		FString HeightmapText;
		if (!FFileHelper::LoadFileToString(HeightmapText, *HeightmapPath))
		{
			OutErr = FString::Printf(TEXT("could not read %s"), *HeightmapPath);
			return false;
		}
		double MinLon, MinLat, MaxLon, MaxLat;
		if (!ReadBboxFromHeightmapJson(HeightmapText, MinLon, MinLat, MaxLon, MaxLat, OutErr))
		{
			return false;
		}

		FString HoleText;
		if (!FFileHelper::LoadFileToString(HoleText, *HolePath))
		{
			OutErr = FString::Printf(TEXT("could not read %s"), *HolePath);
			return false;
		}
		const FString TrackName = DeriveTrackName(CourseId);
		return ParseHoleScheduleJson(HoleText, MinLon, MinLat, MaxLon, MaxLat, TrackName, Out, OutErr);
	}

	// --- GOL-191/192 pin-position system ----------------------------------------------------------
	namespace
	{
		// Area-weighted polygon centroid (shoelace); vertex-average fallback for a degenerate ring.
		// Verts are the OPEN ring (no duplicate closing vertex).
		FVector2D PolygonCentroid(const TArray<FVector2D>& V)
		{
			const int32 N = V.Num();
			if (N == 0) { return FVector2D::ZeroVector; }
			auto Average = [&]() { FVector2D S(0, 0); for (const FVector2D& P : V) { S += P; } return S / N; };
			if (N < 3) { return Average(); }
			double A = 0.0, Cx = 0.0, Cy = 0.0;
			for (int32 i = 0; i < N; ++i)
			{
				const FVector2D& P0 = V[i];
				const FVector2D& P1 = V[(i + 1) % N];
				const double Cross = P0.X * P1.Y - P1.X * P0.Y;
				A  += Cross;
				Cx += (P0.X + P1.X) * Cross;
				Cy += (P0.Y + P1.Y) * Cross;
			}
			if (FMath::Abs(A) < 1e-6) { return Average(); }
			A *= 0.5;
			return FVector2D(Cx / (6.0 * A), Cy / (6.0 * A));
		}

		// A GeoJSON linear ring ([[lon,lat],...]) -> world-cm verts, dropping the closing duplicate.
		void RingToVertsCm(const TArray<TSharedPtr<FJsonValue>>& Ring,
			double MinLon, double MinLat, double MaxLon, double MaxLat, TArray<FVector2D>& Out)
		{
			Out.Reset();
			Out.Reserve(Ring.Num());
			for (const TSharedPtr<FJsonValue>& V : Ring)
			{
				const TArray<TSharedPtr<FJsonValue>>* Pair = nullptr;
				if (!V->TryGetArray(Pair) || !Pair || Pair->Num() < 2) { continue; }
				const FVector W = LonLatToWorldCm((*Pair)[0]->AsNumber(), (*Pair)[1]->AsNumber(),
					MinLon, MinLat, MaxLon, MaxLat);
				Out.Add(FVector2D(W.X, W.Y));
			}
			if (Out.Num() >= 2 && Out[0].Equals(Out.Last(), 1.0)) { Out.Pop(); }
		}
	}

	bool ParseGreenPolygonsJson(const FString& JsonText,
		double MinLon, double MinLat, double MaxLon, double MaxLat,
		TArray<FGreenPolygon>& Out, FString& OutErr)
	{
		Out.Reset();
		TSharedPtr<FJsonObject> Root;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutErr = TEXT("green.geojson parse failed");
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* Features = nullptr;
		if (!Root->TryGetArrayField(TEXT("features"), Features))
		{
			OutErr = TEXT("green.geojson missing features array");
			return false;
		}
		for (const TSharedPtr<FJsonValue>& FVal : *Features)
		{
			const TSharedPtr<FJsonObject> Feat = FVal->AsObject();
			if (!Feat.IsValid()) { continue; }
			const TSharedPtr<FJsonObject> Geom = Feat->GetObjectField(TEXT("geometry"));
			if (!Geom.IsValid()) { continue; }
			FString GeomType;
			Geom->TryGetStringField(TEXT("type"), GeomType);
			if (!GeomType.Equals(TEXT("Polygon"), ESearchCase::IgnoreCase)) { continue; }   // greens are Polygons
			const TArray<TSharedPtr<FJsonValue>>* Rings = nullptr;
			if (!Geom->TryGetArrayField(TEXT("coordinates"), Rings) || !Rings || Rings->Num() == 0) { continue; }
			const TArray<TSharedPtr<FJsonValue>>* OuterRing = nullptr;
			if (!(*Rings)[0]->TryGetArray(OuterRing) || !OuterRing || OuterRing->Num() < 3) { continue; }

			FGreenPolygon G;
			RingToVertsCm(*OuterRing, MinLon, MinLat, MaxLon, MaxLat, G.VertsCm);
			if (G.VertsCm.Num() < 3) { continue; }
			G.CentroidCm = PolygonCentroid(G.VertsCm);
			if (const TSharedPtr<FJsonObject> Props = Feat->GetObjectField(TEXT("properties")))
			{
				double WayId = 0.0;
				if (Props->TryGetNumberField(TEXT("osm_way_id"), WayId)) { G.OsmWayId = (int64)WayId; }
			}
			Out.Add(MoveTemp(G));
		}
		if (Out.Num() == 0) { OutErr = TEXT("green.geojson had no Polygon features"); return false; }
		return true;
	}

	bool ParsePinSheetJson(const FString& JsonText,
		double MinLon, double MinLat, double MaxLon, double MaxLat,
		FPinSheet& Out, FString& OutErr)
	{
		Out = FPinSheet{};
		TSharedPtr<FJsonObject> Root;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutErr = TEXT("pin sheet parse failed");
			return false;
		}
		Root->TryGetStringField(TEXT("name"), Out.Name);
		const TArray<TSharedPtr<FJsonValue>>* Pins = nullptr;
		if (!Root->TryGetArrayField(TEXT("pins"), Pins))
		{
			OutErr = TEXT("pin sheet missing pins array");
			return false;
		}
		for (const TSharedPtr<FJsonValue>& PVal : *Pins)
		{
			const TSharedPtr<FJsonObject> P = PVal->AsObject();
			if (!P.IsValid()) { continue; }
			int32 Ref = 0;
			double Lon = 0.0, Lat = 0.0;
			if (!P->TryGetNumberField(TEXT("hole_ref"), Ref)) { continue; }
			if (!P->TryGetNumberField(TEXT("lon"), Lon) || !P->TryGetNumberField(TEXT("lat"), Lat)) { continue; }
			const FVector W = LonLatToWorldCm(Lon, Lat, MinLon, MinLat, MaxLon, MaxLat);
			Out.PinXYByRefCm.Add(Ref, FVector2D(W.X, W.Y));
		}
		if (Out.PinXYByRefCm.Num() == 0) { OutErr = TEXT("pin sheet had no valid pins"); return false; }
		return true;
	}

	bool PointInPolygonCm(const FVector2D& Pt, const FGreenPolygon& Poly)
	{
		const TArray<FVector2D>& V = Poly.VertsCm;
		const int32 N = V.Num();
		bool bInside = false;
		for (int32 i = 0, j = N - 1; i < N; j = i++)
		{
			const FVector2D& A = V[i];
			const FVector2D& B = V[j];
			if (((A.Y > Pt.Y) != (B.Y > Pt.Y)) &&
				(Pt.X < (B.X - A.X) * (Pt.Y - A.Y) / (B.Y - A.Y) + A.X))
			{
				bInside = !bInside;
			}
		}
		return bInside;
	}

	FVector2D RandomPointInGreen(const FGreenPolygon& Poly, FRandomStream& Stream)
	{
		if (Poly.VertsCm.Num() < 3) { return Poly.CentroidCm; }
		FVector2D Min = Poly.VertsCm[0], Max = Poly.VertsCm[0];
		for (const FVector2D& P : Poly.VertsCm)
		{
			Min.X = FMath::Min(Min.X, P.X); Min.Y = FMath::Min(Min.Y, P.Y);
			Max.X = FMath::Max(Max.X, P.X); Max.Y = FMath::Max(Max.Y, P.Y);
		}
		for (int32 i = 0; i < 40; ++i)   // bbox rejection sampling
		{
			const FVector2D C(Stream.FRandRange(Min.X, Max.X), Stream.FRandRange(Min.Y, Max.Y));
			if (PointInPolygonCm(C, Poly)) { return C; }
		}
		return Poly.CentroidCm;   // thin/concave green or unlucky draws -> guaranteed-ish on-green point
	}

	FVector2D PointOnGreenAtDistance(const FGreenPolygon& Poly, const FVector2D& PinCm,
		double DistCm, FRandomStream& Stream)
	{
		if (Poly.VertsCm.Num() < 3) { return PinCm; }
		// Random heading at the requested distance; accept the first candidate that lands on the green.
		for (int32 i = 0; i < 48; ++i)
		{
			const double Ang = Stream.FRandRange(0.0, 2.0 * PI);
			const FVector2D C(PinCm.X + DistCm * FMath::Cos(Ang), PinCm.Y + DistCm * FMath::Sin(Ang));
			if (PointInPolygonCm(C, Poly)) { return C; }
		}
		// Green smaller than DistCm in every direction -- fall back to any on-green point that's at
		// least ~1 ft from the pin (so the putt isn't a tap on top of the cup).
		for (int32 i = 0; i < 24; ++i)
		{
			const FVector2D C = RandomPointInGreen(Poly, Stream);
			if (FVector2D::DistSquared(C, PinCm) > FMath::Square(30.48)) { return C; }
		}
		return RandomPointInGreen(Poly, Stream);
	}

	int32 MatchGreenToHole(const FHoleSpec& Hole, const TArray<FGreenPolygon>& Greens)
	{
		if (Greens.Num() == 0) { return INDEX_NONE; }
		const FVector2D HoleXY(Hole.GreenWorldLoc.X, Hole.GreenWorldLoc.Y);
		for (int32 i = 0; i < Greens.Num(); ++i)
		{
			if (PointInPolygonCm(HoleXY, Greens[i])) { return i; }   // prefer the containing polygon
		}
		int32 Best = 0;
		double BestD = TNumericLimits<double>::Max();
		for (int32 i = 0; i < Greens.Num(); ++i)
		{
			const double D = FVector2D::DistSquared(HoleXY, Greens[i].CentroidCm);
			if (D < BestD) { BestD = D; Best = i; }
		}
		return Best;
	}

	void ResolvePinPositions(TArray<FHoleSpec>& Schedule, EGolfPinMode Mode,
		const TArray<FGreenPolygon>& Greens, const FPinSheet* Sheet, FRandomStream& Stream)
	{
		if (Mode == EGolfPinMode::Static) { return; }   // leave the authored endpoints
		for (FHoleSpec& H : Schedule)
		{
			const int32 Gi = MatchGreenToHole(H, Greens);
			if (Mode == EGolfPinMode::Random)
			{
				if (Gi != INDEX_NONE)
				{
					const FVector2D P = RandomPointInGreen(Greens[Gi], Stream);
					H.PinWorldLoc = FVector(P.X, P.Y, 0.0);
				}
			}
			else // Tournament
			{
				const FVector2D* SheetPin = Sheet ? Sheet->PinXYByRefCm.Find(H.Ref) : nullptr;
				if (SheetPin)
				{
					H.PinWorldLoc = FVector(SheetPin->X, SheetPin->Y, 0.0);
				}
				else if (Gi != INDEX_NONE)
				{
					H.PinWorldLoc = FVector(Greens[Gi].CentroidCm.X, Greens[Gi].CentroidCm.Y, 0.0);
				}
			}
		}
	}

	bool LoadGreenPolygons(const FString& CourseId, TArray<FGreenPolygon>& Out, FString& OutErr)
	{
		Out.Reset();
		const FString CourseDir = GolfsimPaths::ResolveCourseDataDir(CourseId);
		if (CourseDir.IsEmpty()) { OutErr = FString::Printf(TEXT("no courses/%s data dir"), *CourseId); return false; }
		FString HeightmapText;
		if (!FFileHelper::LoadFileToString(HeightmapText, *(CourseDir / TEXT("heightmap.json"))))
		{
			OutErr = TEXT("could not read heightmap.json");
			return false;
		}
		double MinLon, MinLat, MaxLon, MaxLat;
		if (!ReadBboxFromHeightmapJson(HeightmapText, MinLon, MinLat, MaxLon, MaxLat, OutErr)) { return false; }
		FString GreenText;
		if (!FFileHelper::LoadFileToString(GreenText, *(CourseDir / TEXT("green.geojson"))))
		{
			OutErr = FString::Printf(TEXT("could not read courses/%s/green.geojson"), *CourseId);
			return false;
		}
		return ParseGreenPolygonsJson(GreenText, MinLon, MinLat, MaxLon, MaxLat, Out, OutErr);
	}

	bool LoadPinSheet(const FString& CourseId, const FString& SetId, FPinSheet& Out, FString& OutErr)
	{
		const FString CourseDir = GolfsimPaths::ResolveCourseDataDir(CourseId);
		if (CourseDir.IsEmpty()) { OutErr = FString::Printf(TEXT("no courses/%s data dir"), *CourseId); return false; }
		FString HeightmapText;
		if (!FFileHelper::LoadFileToString(HeightmapText, *(CourseDir / TEXT("heightmap.json"))))
		{
			OutErr = TEXT("could not read heightmap.json");
			return false;
		}
		double MinLon, MinLat, MaxLon, MaxLat;
		if (!ReadBboxFromHeightmapJson(HeightmapText, MinLon, MinLat, MaxLon, MaxLat, OutErr)) { return false; }
		const FString SheetPath = CourseDir / TEXT("pins") / (SetId + TEXT(".json"));
		FString SheetText;
		if (!FFileHelper::LoadFileToString(SheetText, *SheetPath))
		{
			OutErr = FString::Printf(TEXT("could not read %s"), *SheetPath);
			return false;
		}
		return ParsePinSheetJson(SheetText, MinLon, MinLat, MaxLon, MaxLat, Out, OutErr);
	}

	TArray<FHoleSpec> SelectHoles(const TArray<FHoleSpec>& Full, const FRoundConfig& Config)
	{
		switch (Config.HolesMode)
		{
			case ERoundHolesMode::Front9:
				return Full.FilterByPredicate([](const FHoleSpec& H) { return H.Ref >= 1 && H.Ref <= 9; });
			case ERoundHolesMode::Back9:
				return Full.FilterByPredicate([](const FHoleSpec& H) { return H.Ref >= 10 && H.Ref <= 18; });
			case ERoundHolesMode::Custom:
			{
				const TSet<int32> Wanted(Config.CustomHoles);
				return Full.FilterByPredicate([&Wanted](const FHoleSpec& H) { return Wanted.Contains(H.Ref); });
			}
			case ERoundHolesMode::Full18:
			default:
				return Full;
		}
	}

	// --- State machine ---------------------------------------------------------------------

	double EffectiveGimmeRadiusFt(const FRoundConfig& Config, double DifficultyRadiusFt)
	{
		return Config.HoleOutRule == EHoleOutRule::Gimme
			? FMath::Max(DifficultyRadiusFt, (double)Config.GimmeFeet)
			: DifficultyRadiusFt;
	}

	FRoundStep StartRound(FRoundState& S, const FString& CourseId, EGolfDifficulty D,
		TArray<FHoleSpec> Schedule, const FRoundConfig& Config)
	{
		S = FRoundState{};
		if (Schedule.Num() == 0)
		{
			// Empty schedule -> not started; caller is expected to have already errored on load.
			return FRoundStep{};
		}
		S.RoundId         = MakeRoundId();
		S.CourseId        = CourseId;
		S.Difficulty      = D;
		S.Config          = Config;
		S.bActive         = true;
		S.HoleIndex       = 0;
		S.StrokesThisHole = 0;
		S.PerHoleStrokes.Reset();
		S.Schedule        = MoveTemp(Schedule);

		FRoundStep Step;
		Step.bRoundStart = true;
		Step.bHoleStart  = true;
		Step.HoleRefForHoleStart = S.Schedule[0].Ref;
		return Step;
	}

	FRoundStep OnShotOutcome(FRoundState& S)
	{
		if (!S.bActive || S.HoleIndex >= S.Schedule.Num()) { return FRoundStep{}; }
		S.StrokesThisHole++;
		const int32 Par = S.Schedule[S.HoleIndex].Par;
		if (S.StrokesThisHole >= MaxStrokesForPar(Par))
		{
			// Cap tripped -- advance just like a hole-out, but log the strokes hit the ceiling.
			return OnHoleHoled(S);
		}
		return FRoundStep{};
	}

	FRoundStep OnHoleHoled(FRoundState& S)
	{
		if (!S.bActive || S.HoleIndex >= S.Schedule.Num()) { return FRoundStep{}; }

		const FHoleSpec& Hole = S.Schedule[S.HoleIndex];
		// If hole-out is called with zero strokes recorded (paranoid), treat as 1 stroke (no whiffs).
		const int32 Strokes = FMath::Max(S.StrokesThisHole, 1);
		const int32 ScoreVsPar = Strokes - Hole.Par;
		S.PerHoleStrokes.Add(Strokes);

		FRoundStep Step;
		Step.bHoleComplete            = true;
		Step.HoleRefForHoleComplete   = Hole.Ref;
		Step.StrokesForHoleComplete   = Strokes;
		Step.ScoreVsParForHoleComplete = ScoreVsPar;

		const bool bWasLast = (S.HoleIndex == S.Schedule.Num() - 1);
		S.HoleIndex++;
		S.StrokesThisHole = 0;

		if (bWasLast)
		{
			S.bActive = false;
			Step.bRoundComplete = true;
			int32 Total = 0;
			int32 ParTotal = 0;
			for (int32 i = 0; i < S.PerHoleStrokes.Num(); ++i)
			{
				Total    += S.PerHoleStrokes[i];
				ParTotal += S.Schedule[i].Par;
			}
			Step.TotalStrokesForRoundComplete    = Total;
			Step.TotalScoreVsParForRoundComplete = Total - ParTotal;
			Step.PerHoleStrokesForRoundComplete  = S.PerHoleStrokes;
		}
		else
		{
			Step.bHoleStart = true;
			Step.HoleRefForHoleStart = S.Schedule[S.HoleIndex].Ref;
		}
		return Step;
	}

	FRoundStep AbandonRound(FRoundState& S)
	{
		if (!S.bActive) { return FRoundStep{}; }
		S = FRoundState{};
		return FRoundStep{};
	}
}
