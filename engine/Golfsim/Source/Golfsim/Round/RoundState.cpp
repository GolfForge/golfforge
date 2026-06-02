#include "Round/RoundState.h"

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

		FString DeriveRepoRoot()
		{
			// engine/Golfsim/Golfsim.uproject -> ../.. == <repo>. Mirrors Physics/CourseSurface.cpp.
			return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("..") / TEXT(".."));
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
		const FString CourseDir = DeriveRepoRoot() / TEXT("courses") / CourseId;
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

	// --- State machine ---------------------------------------------------------------------

	FRoundStep StartRound(FRoundState& S, const FString& CourseId, EGolfDifficulty D,
		TArray<FHoleSpec> Schedule)
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
