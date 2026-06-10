#include "Practice/PracticeMode.h"

namespace GolfsimPractice
{
	FCtpPin NextPin(const FCtpConfig& Config, FRandomStream& Stream)
	{
		// Normalize the order so a Min > Max config (e.g. a mis-entered spinner) still yields a sane
		// range, then clamp the corridor so a 400+ yd request never buries the pin in the tree wall.
		const double Lo = FMath::Clamp(FMath::Min(Config.MinM, Config.MaxM), 0.0, CorridorMaxM);
		const double Hi = FMath::Clamp(FMath::Max(Config.MinM, Config.MaxM), 0.0, CorridorMaxM);

		FCtpPin Pin;
		Pin.DistanceM = Stream.FRandRange(Lo, Hi);

		if (Config.bSideOffset)
		{
			const double Cap = FMath::Clamp(FMath::Abs(Config.MaxSideM), 0.0, LaneHalfWidthM);
			Pin.SideOffsetM = Stream.FRandRange(-Cap, Cap);
		}
		return Pin;
	}

	double ScoreDistanceM(const FVector& BallWorldCm, const FVector& PinWorldCm)
	{
		const double DxCm = BallWorldCm.X - PinWorldCm.X;
		const double DyCm = BallWorldCm.Y - PinWorldCm.Y;
		return FMath::Sqrt(DxCm * DxCm + DyCm * DyCm) / 100.0;   // cm -> m
	}

	void RecordAttempt(FCtpSession& Session, const FCtpAttempt& Attempt)
	{
		Session.Attempts.Add(Attempt);
	}

	int32 AttemptCount(const FCtpSession& Session)
	{
		return Session.Attempts.Num();
	}

	double BestDistanceM(const FCtpSession& Session)
	{
		if (Session.Attempts.Num() == 0) { return 0.0; }
		double Best = Session.Attempts[0].DistanceM;
		for (const FCtpAttempt& A : Session.Attempts) { Best = FMath::Min(Best, A.DistanceM); }
		return Best;
	}

	double AvgDistanceM(const FCtpSession& Session)
	{
		if (Session.Attempts.Num() == 0) { return 0.0; }
		double Sum = 0.0;
		for (const FCtpAttempt& A : Session.Attempts) { Sum += A.DistanceM; }
		return Sum / Session.Attempts.Num();
	}

	double LastDistanceM(const FCtpSession& Session)
	{
		return Session.Attempts.Num() == 0 ? 0.0 : Session.Attempts.Last().DistanceM;
	}

	int32 BestStrokes(const FCtpSession& Session)
	{
		if (Session.Attempts.Num() == 0) { return 0; }
		int32 Best = Session.Attempts[0].Strokes;
		for (const FCtpAttempt& A : Session.Attempts) { Best = FMath::Min(Best, A.Strokes); }
		return Best;
	}

	double AvgStrokes(const FCtpSession& Session)
	{
		if (Session.Attempts.Num() == 0) { return 0.0; }
		int32 Sum = 0;
		for (const FCtpAttempt& A : Session.Attempts) { Sum += A.Strokes; }
		return (double)Sum / Session.Attempts.Num();
	}

	int32 LastStrokes(const FCtpSession& Session)
	{
		return Session.Attempts.Num() == 0 ? 0 : Session.Attempts.Last().Strokes;
	}
}
