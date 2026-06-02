#include "Physics/CourseSurface.h"

#include "Game/CoursePaths.h"   // GOL-124: cooked-vs-editor course data path resolver
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "Modules/ModuleManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	// Per-layer config. Priority is implicit from order: highest priority first. The classifier
	// walks this list and returns the first layer whose mask byte > MaskThresh at the sampled
	// pixel. Lie maps to the EGolfLie reported -- Trees fold to Rough (no Tree variant in EGolfLie,
	// mirrors GolfRangeSurface). If nothing matches, Rough is the catch-all.
	struct FLayerSpec { const TCHAR* Filename; EGolfLie Lie; };

	static const FLayerSpec LayerSpecs[] = {
		{ TEXT("splat_bunker.png"),  EGolfLie::Bunker   },
		{ TEXT("splat_green.png"),   EGolfLie::Green    },
		{ TEXT("layer_tee.png"),     EGolfLie::Tee      },
		{ TEXT("splat_fairway.png"), EGolfLie::Fairway  },
		{ TEXT("layer_cart_path.png"), EGolfLie::CartPath },
		{ TEXT("layer_trees.png"),   EGolfLie::Rough    },   // trees collapse to Rough at classify time
	};

	bool DecodeGrayscalePng(const TArray<uint8>& Compressed, int32 ExpectedSize, TArray<uint8>& OutGray, FString& OutErr)
	{
		IImageWrapperModule& Mod = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		TSharedPtr<IImageWrapper> Wrap = Mod.CreateImageWrapper(EImageFormat::PNG);
		if (!Wrap.IsValid() || !Wrap->SetCompressed(Compressed.GetData(), Compressed.Num()))
		{
			OutErr = TEXT("ImageWrapper::SetCompressed failed");
			return false;
		}
		const int32 W = Wrap->GetWidth();
		const int32 H = Wrap->GetHeight();
		if (W != ExpectedSize || H != ExpectedSize)
		{
			OutErr = FString::Printf(TEXT("size mismatch: png %dx%d vs expected %dx%d"),
				W, H, ExpectedSize, ExpectedSize);
			return false;
		}
		if (!Wrap->GetRaw(ERGBFormat::Gray, 8, OutGray))
		{
			OutErr = TEXT("ImageWrapper::GetRaw(Gray,8) failed");
			return false;
		}
		if (OutGray.Num() != W * H)
		{
			OutErr = FString::Printf(TEXT("decoded byte count %d != %d"), OutGray.Num(), W * H);
			return false;
		}
		return true;
	}
}

bool FCourseSurfaceSampler::Load(const FString& CourseId)
{
	bValid = false;
	SizePx = 0;
	Layers.Reset();

	const FString CourseDir = GolfsimPaths::ResolveCourseDataDir(CourseId);
	if (CourseDir.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("CourseSurface: no courses/%s data dir found in any candidate base"), *CourseId);
		return false;
	}

	// splatmap.json: pull size_px (and bbox, informationally).
	const FString JsonPath = CourseDir / TEXT("splatmap.json");
	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *JsonPath))
	{
		UE_LOG(LogTemp, Warning, TEXT("CourseSurface: %s not readable -- aborting load"), *JsonPath);
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("CourseSurface: %s parse failed"), *JsonPath);
		return false;
	}
	int32 ParsedSize = 0;
	if (!Root->TryGetNumberField(TEXT("size_px"), ParsedSize) || ParsedSize <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("CourseSurface: %s missing size_px"), *JsonPath);
		return false;
	}
	SizePx = ParsedSize;

	// Walk layer specs in priority order; missing layers log a warning and skip.
	for (const FLayerSpec& Spec : LayerSpecs)
	{
		const FString PngPath = CourseDir / Spec.Filename;
		TArray<uint8> Compressed;
		if (!FFileHelper::LoadFileToArray(Compressed, *PngPath))
		{
			UE_LOG(LogTemp, Warning, TEXT("CourseSurface: %s not readable -- layer skipped"), *PngPath);
			continue;
		}
		TArray<uint8> Gray;
		FString Err;
		if (!DecodeGrayscalePng(Compressed, SizePx, Gray, Err))
		{
			UE_LOG(LogTemp, Warning, TEXT("CourseSurface: decode %s failed: %s"), Spec.Filename, *Err);
			continue;
		}
		FLayer L;
		L.Lie      = Spec.Lie;
		L.Filename = Spec.Filename;
		L.Mask     = MoveTemp(Gray);
		Layers.Add(MoveTemp(L));
	}

	if (Layers.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("CourseSurface: %s loaded zero layers -- aborting"), *CourseDir);
		return false;
	}

	bValid = true;
	UE_LOG(LogTemp, Display, TEXT("CourseSurface: loaded %s -- size %dpx, %d layer(s)"),
		*CourseId, SizePx, Layers.Num());
	return true;
}

EGolfLie FCourseSurfaceSampler::ClassifyAt(double WorldXm, double WorldYm) const
{
	if (!bValid)
	{
		return EGolfLie::Unknown;
	}

	const double WorldXCm = WorldXm * 100.0;
	const double WorldYCm = WorldYm * 100.0;
	if (FMath::Abs(WorldXCm) > HalfXYCm || FMath::Abs(WorldYCm) > HalfXYCm)
	{
		return EGolfLie::Unknown;   // off-landscape
	}

	const double Inv = static_cast<double>(SizePx - 1) / (HalfXYCm * 2.0);
	const int32 Px = FMath::Clamp(static_cast<int32>((WorldXCm + HalfXYCm) * Inv), 0, SizePx - 1);
	const int32 Py = FMath::Clamp(static_cast<int32>((WorldYCm + HalfXYCm) * Inv), 0, SizePx - 1);
	const int32 Idx = Py * SizePx + Px;

	// Priority walk: first layer above threshold wins.
	for (const FLayer& L : Layers)
	{
		if (L.Mask.IsValidIndex(Idx) && L.Mask[Idx] > MaskThresh)
		{
			return L.Lie;
		}
	}
	return EGolfLie::Rough;   // catch-all
}
