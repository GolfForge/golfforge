#include "Session/ShotHistorySubsystem.h"

#include "Events/EventTypes.h"   // FShotOutcomeEvent

#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "JsonObjectConverter.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace GolfsimShotHistory
{
	FString GetSessionDir()
	{
		return FPaths::ProjectSavedDir() / TEXT("ShotHistory");
	}

	FString MakeSessionId()
	{
		// Filename-safe: replace ':' (illegal on Windows) with '-' both in date AND time portions.
		return FDateTime::UtcNow().ToString(TEXT("%Y-%m-%dT%H-%M-%S"));
	}

	FString EntryToJsonLine(const FShotHistoryEntry& Entry)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		if (!FJsonObjectConverter::UStructToJsonObject(
				FShotHistoryEntry::StaticStruct(), &Entry, Obj, /*CheckFlags=*/0, /*SkipFlags=*/0))
		{
			return FString();
		}
		// Condensed writer -> one line, no pretty-printing -- this IS .jsonl.
		FString Out;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Obj, Writer);
		return Out;
	}

	bool ParseJsonLine(const FString& Line, FShotHistoryEntry& Out)
	{
		const FString Trimmed = Line.TrimStartAndEnd();
		if (Trimmed.IsEmpty() || !Trimmed.StartsWith(TEXT("{")) || !Trimmed.EndsWith(TEXT("}")))
		{
			return false;
		}
		TSharedPtr<FJsonObject> Obj;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Trimmed);
		if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid())
		{
			return false;
		}
		return FJsonObjectConverter::JsonObjectToUStruct(
			Obj.ToSharedRef(), FShotHistoryEntry::StaticStruct(), &Out, /*CheckFlags=*/0, /*SkipFlags=*/0);
	}

	static bool EnsureParentDirExists(const FString& Path)
	{
		IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
		const FString Dir = FPaths::GetPath(Path);
		if (Dir.IsEmpty() || PF.DirectoryExists(*Dir))
		{
			return true;
		}
		return PF.CreateDirectoryTree(*Dir);
	}

	bool AppendEntryToFile(const FString& Path, const FShotHistoryEntry& Entry)
	{
		if (!EnsureParentDirExists(Path))
		{
			return false;
		}
		const FString Line = EntryToJsonLine(Entry) + LINE_TERMINATOR;
		if (Line.IsEmpty())
		{
			return false;
		}
		return FFileHelper::SaveStringToFile(
			Line, *Path,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
			&IFileManager::Get(),
			FILEWRITE_Append);
	}

	TArray<FShotHistoryEntry> ReadEntriesFromFile(const FString& Path)
	{
		TArray<FShotHistoryEntry> Out;
		FString Whole;
		if (!FFileHelper::LoadFileToString(Whole, *Path))
		{
			return Out;   // missing file -> empty list
		}
		TArray<FString> Lines;
		// Allow CRLF or LF. Don't cull empty lines -- ParseJsonLine treats blank as "skip".
		Whole.ParseIntoArray(Lines, TEXT("\n"), /*InCullEmpty=*/false);
		Out.Reserve(Lines.Num());
		for (const FString& L : Lines)
		{
			FShotHistoryEntry E;
			if (ParseJsonLine(L, E))
			{
				Out.Add(MoveTemp(E));
			}
			// Silently drop any malformed/truncated line (e.g. crash mid-write).
		}
		return Out;
	}

	bool TruncateFile(const FString& Path)
	{
		// Empty string + no append = truncate to 0 bytes. Tolerates a missing file (creates it).
		return EnsureParentDirExists(Path) &&
			FFileHelper::SaveStringToFile(FString(), *Path,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
				&IFileManager::Get(),
				FILEWRITE_None);
	}
}

// --- UShotHistorySubsystem ---------------------------------------------------------------------

void UShotHistorySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UEventBusSubsystem::StaticClass());

	SessionId = GolfsimShotHistory::MakeSessionId();
	SessionJsonlPath = GolfsimShotHistory::GetSessionDir() / (SessionId + TEXT(".jsonl"));

	if (UEventBusSubsystem* EBus = GetGameInstance()->GetSubsystem<UEventBusSubsystem>())
	{
		EventBusWeak = EBus;
		OutcomeSub = EBus->Subscribe(EEventKind::ShotOutcome,
			[this](const FGolfEvent& Event) { OnShotOutcome(Event); });
	}

	UE_LOG(LogTemp, Display,
		TEXT("golfsim ShotHistory: session %s -> %s"), *SessionId, *SessionJsonlPath);
}

void UShotHistorySubsystem::Deinitialize()
{
	if (UEventBusSubsystem* EBus = EventBusWeak.Get())
	{
		EBus->Unsubscribe(OutcomeSub);
	}
	OutcomeSub = FGolfEventSubscription{};
	EventBusWeak = nullptr;
	Super::Deinitialize();
}

UShotHistorySubsystem* UShotHistorySubsystem::Get(const UObject* WorldContext)
{
	if (!GEngine || !WorldContext)
	{
		return nullptr;
	}
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull))
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			return GI->GetSubsystem<UShotHistorySubsystem>();
		}
	}
	return nullptr;
}

TArray<FString> UShotHistorySubsystem::ListPastSessionIds() const
{
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(GolfsimShotHistory::GetSessionDir() / TEXT("*.jsonl")), /*Files=*/true, /*Dirs=*/false);
	TArray<FString> Ids;
	Ids.Reserve(Files.Num());
	for (const FString& F : Files)
	{
		FString Id = FPaths::GetBaseFilename(F);
		if (Id != SessionId)
		{
			Ids.Add(MoveTemp(Id));
		}
	}
	// Session ids are lexicographically sortable timestamps -- reverse for newest-first.
	Ids.Sort([](const FString& A, const FString& B) { return A > B; });
	return Ids;
}

TArray<FShotHistoryEntry> UShotHistorySubsystem::LoadSession(const FString& InSessionId) const
{
	const FString Path = GolfsimShotHistory::GetSessionDir() / (InSessionId + TEXT(".jsonl"));
	return GolfsimShotHistory::ReadEntriesFromFile(Path);
}

void UShotHistorySubsystem::Clear()
{
	Entries.Reset();
	NextShotId = 1;
	GolfsimShotHistory::TruncateFile(SessionJsonlPath);
	OnEntriesChanged.Broadcast();
}

void UShotHistorySubsystem::OnShotOutcome(const FGolfEvent& Event)
{
	if (Event.Kind != EEventKind::ShotOutcome)
	{
		return;
	}
	const FShotOutcomeEvent& Out = static_cast<const FShotOutcomeEvent&>(Event);

	FShotHistoryEntry Entry;
	Entry.ShotId          = NextShotId++;
	Entry.TsMs            = Out.TsMs;
	Entry.Source          = Out.Source;
	Entry.Club            = Out.Club;
	Entry.BallSpeedMps    = Out.BallSpeedMps;
	Entry.LaunchAngleDeg  = Out.LaunchAngleDeg;
	Entry.BackspinRpm     = Out.BackspinRpm;
	Entry.bSpinEstimated  = Out.bSpinEstimated;
	Entry.CarryM          = Out.CarryM;
	Entry.TotalM          = Out.TotalM;
	Entry.LateralOffsetM  = Out.LateralOffsetM;
	Entry.FinalLie        = Out.FinalLie;

	AppendOne(Entry);
}

void UShotHistorySubsystem::AppendOne(const FShotHistoryEntry& Entry)
{
	Entries.Add(Entry);
	GolfsimShotHistory::AppendEntryToFile(SessionJsonlPath, Entry);
	OnEntriesChanged.Broadcast();
}
