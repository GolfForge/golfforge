#include "UI/GreenFlowTexture.h"

#include "Engine/Texture2D.h"

namespace GolfsimGreenFlow
{

bool BuildFlowPixels(const GolfMap::FGreenSlopeGrid& Grid, float SlopeMaxPct, TArray<FColor>& OutPixels)
{
	OutPixels.Reset();
	const int32 Num = Grid.NX * Grid.NY;
	if (Grid.IsEmpty() || Grid.SlopePct.Num() != Num || Grid.FallDirWorld.Num() != Num
		|| Grid.bInGreen.Num() != Num || SlopeMaxPct <= 0.f)
	{
		return false;
	}

	// Neutral = zero flow, zero slope, masked out. Bilinear taps at the green edge interpolate
	// toward this, so dots fade + stall instead of streaking off the green.
	const FColor Neutral(128, 128, 0, 0);
	OutPixels.SetNumUninitialized(Num);
	for (int32 i = 0; i < Num; ++i)
	{
		const FVector2D Dir = Grid.FallDirWorld[i];
		if (!Grid.bInGreen[i] || Dir.IsNearlyZero())
		{
			// Flat in-green cells keep their mask (dots show dim + static there) -- only true
			// off-green cells lose alpha.
			OutPixels[i] = Grid.bInGreen[i] ? FColor(128, 128, 0, 255) : Neutral;
			continue;
		}
		const auto Encode = [](double V) -> uint8
		{
			return (uint8)FMath::Clamp(FMath::RoundToInt32((V * 0.5 + 0.5) * 255.0), 0, 255);
		};
		const float Mag = FMath::Clamp(Grid.SlopePct[i] / SlopeMaxPct, 0.f, 1.f);
		OutPixels[i] = FColor(
			Encode(Dir.X),
			Encode(Dir.Y),
			(uint8)FMath::RoundToInt32(Mag * 255.f),
			255);
	}
	return true;
}

UTexture2D* CreateFlowTexture(const GolfMap::FGreenSlopeGrid& Grid, float SlopeMaxPct)
{
	TArray<FColor> Pixels;
	if (!BuildFlowPixels(Grid, SlopeMaxPct, Pixels))
	{
		return nullptr;
	}

	UTexture2D* Tex = UTexture2D::CreateTransient(Grid.NX, Grid.NY, PF_B8G8R8A8);
	if (!Tex)
	{
		return nullptr;
	}
	Tex->SRGB = false;            // direction/magnitude data, not color
	Tex->Filter = TF_Bilinear;    // smooth flow between cells (zero-crossings interpolate to stall)
	Tex->AddressX = TA_Clamp;
	Tex->AddressY = TA_Clamp;
	Tex->NeverStream = true;
	void* MipData = Tex->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(MipData, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	Tex->GetPlatformData()->Mips[0].BulkData.Unlock();
	Tex->UpdateResource();
	return Tex;
}

}   // namespace GolfsimGreenFlow
