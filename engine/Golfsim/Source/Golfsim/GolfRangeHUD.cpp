#include "GolfRangeHUD.h"

#include "GolfBallActor.h"
#include "Physics/BallFlightSolver.h"
#include "Physics/BallFlightTypes.h"

#include "Engine/Canvas.h"
#include "EngineUtils.h"
#include "Components/InputComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

namespace
{
	struct FClubPreset { const TCHAR* Name; double SpeedMps; double LaunchDeg; double SpinRpm; };

	// Trackman PGA-Tour averages (ball speed mph -> m/s), hit straight; per-shot
	// dispersion is added at fire time so no two shots are identical.
	static const FClubPreset GBag[] = {
		{ TEXT("Driver"),         74.6, 10.9, 2686.0 },
		{ TEXT("3-Wood"),         70.6,  9.2, 3655.0 },
		{ TEXT("5-Iron"),         60.3, 11.9, 5280.0 },
		{ TEXT("7-Iron"),         55.0, 16.3, 7097.0 },
		{ TEXT("9-Iron"),         48.7, 20.4, 8647.0 },
		{ TEXT("Pitching Wedge"), 45.6, 24.2, 9304.0 },
	};
	static constexpr int32 GBagNum = UE_ARRAY_COUNT(GBag);

	// Aim turn speed while an arrow is held, deg/sec. Tunable.
	static constexpr float TurnRateDegPerSec = 75.0f;

	// Place the ball at the player pawn (the tee), facing its heading with pitch/roll
	// flattened. Reuses one ball, repositioning per shot. Mirrors GolfsimConsole.cpp.
	AGolfBallActor* GetOrSpawnBallAt(UWorld* World, const FVector& Loc, const FRotator& Rot)
	{
		for (TActorIterator<AGolfBallActor> It(World); It; ++It)
		{
			It->SetActorLocationAndRotation(Loc, Rot);
			return *It;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<AGolfBallActor>(AGolfBallActor::StaticClass(), Loc, Rot, Params);
	}
}

AGolfRangeHUD::AGolfRangeHUD()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void AGolfRangeHUD::BeginPlay()
{
	Super::BeginPlay();
	EnsureInputBound();
}

void AGolfRangeHUD::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const float Dir = (bTurnRight ? 1.0f : 0.0f) - (bTurnLeft ? 1.0f : 0.0f);
	if (Dir == 0.0f)
	{
		return;
	}
	APlayerController* PC = GetOwningPlayerController();
	if (!PC)
	{
		return;
	}
	FRotator CR = PC->GetControlRotation();
	CR.Yaw += TurnRateDegPerSec * DeltaSeconds * Dir;   // camera follows (bUsePawnControlRotation)
	PC->SetControlRotation(CR);
}

void AGolfRangeHUD::EnsureInputBound()
{
	if (bInputBound)
	{
		return;
	}
	APlayerController* PC = GetOwningPlayerController();
	if (!PC)
	{
		return;   // PlayerOwner not ready yet; retried from DrawHUD
	}
	EnableInput(PC);
	if (!bControlsLocked)
	{
		PC->SetIgnoreMoveInput(true);   // planted on the tee
		PC->SetIgnoreLookInput(true);   // arrows aim instead of the mouse
		bControlsLocked = true;
	}
	if (!InputComponent)
	{
		return;
	}
	InputComponent->BindKey(EKeys::One,      IE_Pressed, this, &AGolfRangeHUD::SelectClub0);
	InputComponent->BindKey(EKeys::Two,      IE_Pressed, this, &AGolfRangeHUD::SelectClub1);
	InputComponent->BindKey(EKeys::Three,    IE_Pressed, this, &AGolfRangeHUD::SelectClub2);
	InputComponent->BindKey(EKeys::Four,     IE_Pressed, this, &AGolfRangeHUD::SelectClub3);
	InputComponent->BindKey(EKeys::Five,     IE_Pressed, this, &AGolfRangeHUD::SelectClub4);
	InputComponent->BindKey(EKeys::Six,      IE_Pressed, this, &AGolfRangeHUD::SelectClub5);
	InputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &AGolfRangeHUD::FireRandom);
	InputComponent->BindKey(EKeys::Left,     IE_Pressed,  this, &AGolfRangeHUD::TurnLeftPressed);
	InputComponent->BindKey(EKeys::Left,     IE_Released, this, &AGolfRangeHUD::TurnLeftReleased);
	InputComponent->BindKey(EKeys::Right,    IE_Pressed,  this, &AGolfRangeHUD::TurnRightPressed);
	InputComponent->BindKey(EKeys::Right,    IE_Released, this, &AGolfRangeHUD::TurnRightReleased);
	bInputBound = true;
}

void AGolfRangeHUD::SelectClub(int32 Index)
{
	ActiveClub = FMath::Clamp(Index, 0, GBagNum - 1);
	UE_LOG(LogTemp, Display, TEXT("golfsim range: club -> %s"), GBag[ActiveClub].Name);
}

void AGolfRangeHUD::FireRandom()
{
	UWorld* World = GetWorld();
	APlayerController* PC = GetOwningPlayerController();
	if (!World || !PC)
	{
		return;
	}

	FRotator Rot = PC->GetControlRotation();   // aim = where the camera points
	Rot.Pitch = 0.f;
	Rot.Roll = 0.f;
	FVector Loc = FVector::ZeroVector;
	if (APawn* Pawn = PC->GetPawn())
	{
		Loc = Pawn->GetActorLocation();        // launch from the tee
	}

	const FClubPreset& C = GBag[FMath::Clamp(ActiveClub, 0, GBagNum - 1)];
	FShotInput Shot;
	Shot.BallSpeedMps   = C.SpeedMps  * FMath::FRandRange(0.97, 1.03);
	Shot.LaunchAngleDeg = C.LaunchDeg + FMath::FRandRange(-1.5, 1.5);
	Shot.BackspinRpm    = C.SpinRpm   * FMath::FRandRange(0.92, 1.08);
	Shot.AzimuthDeg     = FMath::FRandRange(-2.5, 2.5);     // face/path spread
	Shot.SidespinRpm    = FMath::FRandRange(-700.0, 700.0); // curve

	const FBallTrajectory T = GolfBallFlight::Simulate(Shot);
	if (AGolfBallActor* Ball = GetOrSpawnBallAt(World, Loc, Rot))
	{
		Ball->PlayTrajectory(T);
	}

	LastShotText = FString::Printf(TEXT("%s   carry %.0f m   %s%.0f m"),
		C.Name, T.CarryM,
		(T.LateralOffsetM >= 0.0 ? TEXT("R") : TEXT("L")), FMath::Abs(T.LateralOffsetM));
	UE_LOG(LogTemp, Display,
		TEXT("golfsim range: %s aim=%.1fdeg carry=%.1fm apex=%.1fm lateral=%.1fm"),
		C.Name, Rot.Yaw, T.CarryM, T.ApexM, T.LateralOffsetM);
}

void AGolfRangeHUD::DrawHUD()
{
	Super::DrawHUD();
	EnsureInputBound();
	if (!Canvas)
	{
		return;
	}

	const FClubPreset& C = GBag[FMath::Clamp(ActiveClub, 0, GBagNum - 1)];
	const FString L1 = FString::Printf(TEXT("Club:  %s"), C.Name);
	const FString L2 = TEXT("[1-6] club   [<- ->] aim   [Space] hit");
	const FString L3 = LastShotText.IsEmpty() ? FString() : (TEXT("Last:  ") + LastShotText);

	const float TitleScale = 1.5f;
	float w1 = 0.f, h1 = 0.f, w2 = 0.f, h2 = 0.f, w3 = 0.f, h3 = 0.f;
	GetTextSize(L1, w1, h1, nullptr, TitleScale);
	GetTextSize(L2, w2, h2, nullptr, 1.0f);
	if (!L3.IsEmpty())
	{
		GetTextSize(L3, w3, h3, nullptr, 1.0f);
	}

	const float Margin = 28.f;
	const float BlockW = FMath::Max3(w1, w2, w3);
	const float BlockH = h1 + h2 + (L3.IsEmpty() ? 0.f : h3) + 12.f;
	float X = Canvas->ClipX - BlockW - Margin;
	float Y = Canvas->ClipY - BlockH - Margin;

	DrawText(L1, FLinearColor(1.0f, 0.92f, 0.35f), X, Y, nullptr, TitleScale);
	Y += h1 + 4.f;
	DrawText(L2, FLinearColor::White, X, Y, nullptr, 1.0f);
	if (!L3.IsEmpty())
	{
		Y += h2 + 4.f;
		DrawText(L3, FLinearColor(0.65f, 0.9f, 1.0f), X, Y, nullptr, 1.0f);
	}
}
