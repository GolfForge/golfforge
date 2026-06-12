#include "Game/MinimapTexture.h"

#include "Game/CoursePaths.h"
#include "Engine/Texture2D.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"

namespace GolfsimMinimap
{

UTexture2D* LoadMinimapTexture(const FString& CourseId, FString& OutErr)
{
	const FString CourseDir = GolfsimPaths::ResolveCourseDataDir(CourseId);
	if (CourseDir.IsEmpty())
	{
		OutErr = FString::Printf(TEXT("course data dir not found for '%s'"), *CourseId);
		return nullptr;
	}

	const FString PngPath = CourseDir / TEXT("minimap.png");
	TArray<uint8> Compressed;
	if (!FFileHelper::LoadFileToArray(Compressed, *PngPath))
	{
		OutErr = FString::Printf(TEXT("no minimap.png in %s (run pipeline/build_minimap.py)"), *CourseDir);
		return nullptr;
	}

	IImageWrapperModule& Mod = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> Wrap = Mod.CreateImageWrapper(EImageFormat::PNG);
	if (!Wrap.IsValid() || !Wrap->SetCompressed(Compressed.GetData(), Compressed.Num()))
	{
		OutErr = TEXT("ImageWrapper::SetCompressed failed");
		return nullptr;
	}
	const int32 W = Wrap->GetWidth();
	const int32 H = Wrap->GetHeight();
	TArray<uint8> Bgra;
	if (!Wrap->GetRaw(ERGBFormat::BGRA, 8, Bgra) || Bgra.Num() != W * H * 4)
	{
		OutErr = TEXT("ImageWrapper::GetRaw(BGRA,8) failed");
		return nullptr;
	}
	if (W != H)
	{
		// The data contract guarantees square (same dims as the splatmap); a non-square file
		// would break the world->UV affine, so refuse rather than render misregistered.
		OutErr = FString::Printf(TEXT("minimap.png is %dx%d, expected square"), W, H);
		return nullptr;
	}

	UTexture2D* Tex = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
	if (!Tex)
	{
		OutErr = TEXT("CreateTransient failed");
		return nullptr;
	}
	Tex->SRGB = true;
	Tex->NeverStream = true;
	void* MipData = Tex->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(MipData, Bgra.GetData(), Bgra.Num());
	Tex->GetPlatformData()->Mips[0].BulkData.Unlock();
	Tex->UpdateResource();
	return Tex;
}

}   // namespace GolfsimMinimap
