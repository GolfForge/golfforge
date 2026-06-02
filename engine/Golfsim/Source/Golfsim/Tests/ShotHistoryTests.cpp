// Automation tests for the pure GolfsimShotHistory:: JSONL helpers (no world/RHI). Mirrors
// the SettingsTests style. Each test writes to a unique file under <Saved>/Test/ to avoid
// stomping the live ShotHistory dir.
//
// Run: UnrealEditor-Cmd <proj> -ExecCmds="Automation RunTests Golfsim.ShotHistory; Quit" -unattended -nullrhi

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Session/ShotHistorySubsystem.h"

#if WITH_AUTOMATION_TESTS

namespace
{
	FString MakeTempPath(const FString& Suffix)
	{
		const FString Dir = FPaths::ProjectSavedDir() / TEXT("Test") / TEXT("ShotHistory");
		FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*Dir);
		return Dir / Suffix;
	}

	void RemoveIfExists(const FString& Path)
	{
		if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*Path))
		{
			FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*Path);
		}
	}

	FShotHistoryEntry MakeEntry(int32 Id, const TCHAR* Club, double CarryM, double LateralM)
	{
		FShotHistoryEntry E;
		E.ShotId = Id;
		E.TsMs = 1717200000000 + Id;   // arbitrary stable timestamps
		E.Source = TEXT("test-suite");
		E.Club = Club;
		E.BallSpeedMps = 60.0 + Id;
		E.LaunchAngleDeg = 12.0 + Id;
		E.BackspinRpm = 5000.0 + 100.0 * Id;
		E.bSpinEstimated = (Id % 2 == 0);
		E.CarryM = CarryM;
		E.TotalM = CarryM + 5.0;
		E.LateralOffsetM = LateralM;
		E.FinalLie = TEXT("fairway");
		return E;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimShotHistoryAppendRoundTripTest, "Golfsim.ShotHistory.AppendRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimShotHistoryAppendRoundTripTest::RunTest(const FString&)
{
	const FString Path = MakeTempPath(TEXT("round_trip.jsonl"));
	RemoveIfExists(Path);

	const FShotHistoryEntry E1 = MakeEntry(1, TEXT("Driver"),  220.0, -2.5);
	const FShotHistoryEntry E2 = MakeEntry(2, TEXT("7-Iron"),  142.7,  1.1);
	const FShotHistoryEntry E3 = MakeEntry(3, TEXT("Putter"),    3.0,  0.0);

	TestTrue(TEXT("append E1"), GolfsimShotHistory::AppendEntryToFile(Path, E1));
	TestTrue(TEXT("append E2"), GolfsimShotHistory::AppendEntryToFile(Path, E2));
	TestTrue(TEXT("append E3"), GolfsimShotHistory::AppendEntryToFile(Path, E3));

	const TArray<FShotHistoryEntry> Read = GolfsimShotHistory::ReadEntriesFromFile(Path);
	TestEqual(TEXT("3 entries back"), Read.Num(), 3);
	if (Read.Num() == 3)
	{
		TestEqual(TEXT("E1 id"), Read[0].ShotId, 1);
		TestEqual(TEXT("E1 club"), Read[0].Club, FString(TEXT("Driver")));
		TestTrue(TEXT("E1 carry"), FMath::IsNearlyEqual(Read[0].CarryM, 220.0));
		TestEqual(TEXT("E2 id"), Read[1].ShotId, 2);
		TestTrue(TEXT("E2 lateral"), FMath::IsNearlyEqual(Read[1].LateralOffsetM, 1.1));
		TestEqual(TEXT("E3 id"), Read[2].ShotId, 3);
		TestEqual(TEXT("E3 lie"), Read[2].FinalLie, FString(TEXT("fairway")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimShotHistoryTruncatedTailTest, "Golfsim.ShotHistory.TolerantToTruncatedTail",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimShotHistoryTruncatedTailTest::RunTest(const FString&)
{
	const FString Path = MakeTempPath(TEXT("truncated_tail.jsonl"));
	RemoveIfExists(Path);

	const FShotHistoryEntry E1 = MakeEntry(1, TEXT("Driver"), 220.0, 0.0);
	const FShotHistoryEntry E2 = MakeEntry(2, TEXT("7-Iron"), 142.7, 0.0);
	TestTrue(TEXT("append E1"), GolfsimShotHistory::AppendEntryToFile(Path, E1));
	TestTrue(TEXT("append E2"), GolfsimShotHistory::AppendEntryToFile(Path, E2));

	// Hand-write a deliberately truncated 3rd entry: a half-JSON object with no closing brace
	// (simulates a crash mid-write). Append uses FILEWRITE_Append, so we just slap it on the tail.
	const FString Garbage = FString(TEXT("{\"ShotId\":3,\"Club\":\"Pitc")) + LINE_TERMINATOR;
	TestTrue(TEXT("append garbage"), FFileHelper::SaveStringToFile(
		Garbage, *Path,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
		&IFileManager::Get(),
		FILEWRITE_Append));

	const TArray<FShotHistoryEntry> Read = GolfsimShotHistory::ReadEntriesFromFile(Path);
	TestEqual(TEXT("2 well-formed entries returned, truncated tail dropped"), Read.Num(), 2);
	if (Read.Num() == 2)
	{
		TestEqual(TEXT("E1 id"), Read[0].ShotId, 1);
		TestEqual(TEXT("E2 id"), Read[1].ShotId, 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimShotHistoryPrecisionTest, "Golfsim.ShotHistory.CarryAndLateralPrecision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimShotHistoryPrecisionTest::RunTest(const FString&)
{
	const FString Path = MakeTempPath(TEXT("precision.jsonl"));
	RemoveIfExists(Path);

	// Awkward floats that would round badly under int truncation.
	const FShotHistoryEntry E1 = MakeEntry(1, TEXT("Driver"), 264.357,  -3.51);
	const FShotHistoryEntry E2 = MakeEntry(2, TEXT("LW 60"),   77.099,   0.012);
	TestTrue(TEXT("append E1"), GolfsimShotHistory::AppendEntryToFile(Path, E1));
	TestTrue(TEXT("append E2"), GolfsimShotHistory::AppendEntryToFile(Path, E2));

	const TArray<FShotHistoryEntry> Read = GolfsimShotHistory::ReadEntriesFromFile(Path);
	TestEqual(TEXT("2 entries"), Read.Num(), 2);
	if (Read.Num() == 2)
	{
		TestTrue(TEXT("E1 carry precise"),   FMath::IsNearlyEqual(Read[0].CarryM,         264.357, 1e-6));
		TestTrue(TEXT("E1 lateral precise"), FMath::IsNearlyEqual(Read[0].LateralOffsetM, -3.51,   1e-6));
		TestTrue(TEXT("E2 carry precise"),   FMath::IsNearlyEqual(Read[1].CarryM,         77.099,  1e-6));
		TestTrue(TEXT("E2 lateral precise"), FMath::IsNearlyEqual(Read[1].LateralOffsetM, 0.012,   1e-6));
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
