# Settings + Credits Menu Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> **Project git convention:** the human runs git (see project memory). Do NOT run `git` yourself. At each "Commit" step, hand the user the two commands (`git add …`; then `git commit …`) and let them run them. Build with the **editor closed** for any change that adds/changes reflected types (new `UCLASS`/`UPROPERTY`/`UFUNCTION`); body-only changes can use Live Coding.

**Goal:** Add an Esc-toggled centered settings menu hosting Display settings (resolution / window mode / quality / screen-%, persisted via `UGameUserSettings`) and a Credits/Attributions screen, owned by `AGolfRangeHUD`.

**Architecture:** A pure, testable display-settings module (`GolfDisplaySettings.{h,cpp}`) holds the data struct + parse/apply logic. A dumb-view pure-C++ `UUserWidget` (`USettingsMenu`) renders the menu and reports user actions via `TFunction` callbacks (same idiom as `UGolfRangePanel`). `AGolfRangeHUD` creates/toggles it, gates gameplay keys while open, and wires the apply path. Three `golfsim.*` console commands reuse the same apply path.

**Tech Stack:** UE5.7 C++, UMG (`WidgetTree->ConstructWidget`), `UGameUserSettings`, the engine automation-test framework (`IMPLEMENT_SIMPLE_AUTOMATION_TEST`).

**Tickets:** GOL-52 (display settings), GOL-59 (credits). Out of scope: DLSS/FSR (GOL-53), a global menu owner.

---

## File structure

- **Create** `engine/Golfsim/Source/Golfsim/GolfDisplaySettings.h` — `FGolfDisplaySettings` POD struct + `namespace GolfDisplay` free-function declarations. No UObject/UMG deps → unit-testable.
- **Create** `engine/Golfsim/Source/Golfsim/GolfDisplaySettings.cpp` — pure helpers (`ParseResolution`, clamps, `CreditsText`) + engine-touching helpers (`ReadCurrent`, `Apply`, `SupportedResolutions`).
- **Create** `engine/Golfsim/Source/Golfsim/Tests/SettingsTests.cpp` — automation tests for the pure helpers.
- **Create** `engine/Golfsim/Source/Golfsim/SettingsMenu.h` / `.cpp` — `USettingsMenu : UUserWidget` (view).
- **Modify** `engine/Golfsim/Source/Golfsim/GolfRangeHUD.h` / `.cpp` — own/create/toggle the menu, Esc bind, gameplay-key gating, apply/close callbacks, `OpenCreditsSection()` for the console.
- **Modify** `engine/Golfsim/Source/Golfsim/GolfsimConsole.cpp` — `golfsim.SetResolution` / `golfsim.SetQuality` / `golfsim.Credits`.
- No `Golfsim.Build.cs` change (Engine/UMG/Slate already linked; `UGameUserSettings` is in Engine).

**Build / test commands** (placeholders: `<UE_ROOT>` = your UE 5.7 install, `<repo>` = repo root):

- Build (editor closed): `<UE_ROOT>\Engine\Build\BatchFiles\Build.bat GolfsimEditor Win64 Development -Project=<repo>\engine\Golfsim\Golfsim.uproject -WaitMutex -FromMsBuild`
- Run a test category headless: `<UE_ROOT>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe <repo>\engine\Golfsim\Golfsim.uproject -ExecCmds="Automation RunTests Golfsim.Settings; Quit" -unattended -nullrhi -nosplash`
- Full suite: same but `Automation RunTests Golfsim`.

---

## Task 1: Pure display-settings module + tests

**Files:**
- Create: `engine/Golfsim/Source/Golfsim/GolfDisplaySettings.h`
- Create: `engine/Golfsim/Source/Golfsim/GolfDisplaySettings.cpp`
- Test: `engine/Golfsim/Source/Golfsim/Tests/SettingsTests.cpp`

- [ ] **Step 1: Write the header**

`GolfDisplaySettings.h`:
```cpp
// Display-settings data + logic, split from the UI widget so the parse/clamp helpers are unit-testable
// (no UObject, no UMG, no world). Apply/ReadCurrent touch UGameUserSettings (PIE-verified). GOL-52.
#pragma once

#include "CoreMinimal.h"
#include "Misc/Optional.h"

// WindowModeIndex: 0 = Windowed, 1 = Borderless (WindowedFullscreen), 2 = Fullscreen. Kept as an int
// index (not EWindowMode) so this header stays engine-enum-free and POD; mapped in Apply/ReadCurrent.
struct FGolfDisplaySettings
{
	FIntPoint Resolution = FIntPoint(1920, 1080);
	int32 WindowModeIndex = 0;
	int32 QualityLevel = 3;       // 0=Low 1=Medium 2=High 3=Epic
	float ScreenPercentage = 100.f;
};

namespace GolfDisplay
{
	// Pure (unit-tested):
	TOptional<FIntPoint> ParseResolution(const FString& In);   // "1920x1080" -> (1920,1080); else unset
	int32 ClampQualityLevel(int32 Level);                      // -> [0,3]
	int32 ClampWindowModeIndex(int32 Index);                   // -> [0,2]
	FString CreditsText();                                     // attribution block (sync w/ ATTRIBUTION.md)

	// Engine-touching (PIE-verified):
	FGolfDisplaySettings ReadCurrent();                        // from UGameUserSettings + r.ScreenPercentage
	void Apply(const FGolfDisplaySettings& S);                 // write + ApplyResolutionSettings + SaveSettings
	TArray<FIntPoint> SupportedResolutions();                  // monitor modes, or a sane fallback list
}
```

- [ ] **Step 2: Write the failing tests**

`Tests/SettingsTests.cpp`:
```cpp
// Automation tests for the pure GolfDisplay helpers (no world/RHI). Mirrors the EventBus test style.
// Run: UnrealEditor-Cmd <proj> -ExecCmds="Automation RunTests Golfsim.Settings; Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"
#include "GolfDisplaySettings.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimSettingsParseResTest, "Golfsim.Settings.ParseResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimSettingsParseResTest::RunTest(const FString&)
{
	TOptional<FIntPoint> Ok = GolfDisplay::ParseResolution(TEXT("1920x1080"));
	TestTrue(TEXT("valid parses"), Ok.IsSet());
	TestEqual(TEXT("width"), Ok.GetValue().X, 1920);
	TestEqual(TEXT("height"), Ok.GetValue().Y, 1080);
	TestTrue(TEXT("uppercase X parses"), GolfDisplay::ParseResolution(TEXT("1280X720")).IsSet());
	TestFalse(TEXT("garbage rejected"), GolfDisplay::ParseResolution(TEXT("abc")).IsSet());
	TestFalse(TEXT("missing height rejected"), GolfDisplay::ParseResolution(TEXT("1920x")).IsSet());
	TestFalse(TEXT("zero rejected"), GolfDisplay::ParseResolution(TEXT("0x0")).IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimSettingsClampTest, "Golfsim.Settings.Clamps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimSettingsClampTest::RunTest(const FString&)
{
	TestEqual(TEXT("quality high clamps to 3"), GolfDisplay::ClampQualityLevel(9), 3);
	TestEqual(TEXT("quality low clamps to 0"), GolfDisplay::ClampQualityLevel(-2), 0);
	TestEqual(TEXT("quality in-range passes"), GolfDisplay::ClampQualityLevel(2), 2);
	TestEqual(TEXT("window mode clamps to 2"), GolfDisplay::ClampWindowModeIndex(5), 2);
	TestEqual(TEXT("window mode clamps to 0"), GolfDisplay::ClampWindowModeIndex(-1), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimSettingsCreditsTest, "Golfsim.Settings.CreditsContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimSettingsCreditsTest::RunTest(const FString&)
{
	const FString C = GolfDisplay::CreditsText();
	TestTrue(TEXT("credits OSM"), C.Contains(TEXT("OpenStreetMap")));
	TestTrue(TEXT("credits Unreal"), C.Contains(TEXT("Unreal")));
	TestTrue(TEXT("credits USGS"), C.Contains(TEXT("USGS")));
	TestTrue(TEXT("credits license"), C.Contains(TEXT("AGPL")));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
```

- [ ] **Step 3: Write a stub `.cpp` so it links but fails**

`GolfDisplaySettings.cpp` (stub bodies for the pure helpers — wrong on purpose; engine helpers can be final now since they aren't unit-tested):
```cpp
#include "GolfDisplaySettings.h"
#include "Engine/Engine.h"
#include "Engine/GameUserSettings.h"
#include "Kismet/KismetSystemLibrary.h"
#include "HAL/IConsoleManager.h"

namespace GolfDisplay
{
	TOptional<FIntPoint> ParseResolution(const FString&) { return TOptional<FIntPoint>(); }   // STUB
	int32 ClampQualityLevel(int32) { return 0; }                                              // STUB
	int32 ClampWindowModeIndex(int32) { return 0; }                                           // STUB
	FString CreditsText() { return FString(); }                                               // STUB

	FGolfDisplaySettings ReadCurrent() { return FGolfDisplaySettings(); }                     // STUB
	void Apply(const FGolfDisplaySettings&) {}                                                // STUB
	TArray<FIntPoint> SupportedResolutions() { return {}; }                                   // STUB
}
```

- [ ] **Step 4: Build + run; verify the tests FAIL**

Build (editor closed), then run `Automation RunTests Golfsim.Settings`.
Expected: the three `Golfsim.Settings.*` tests **FAIL** (parse returns unset, clamps return 0, credits empty).

- [ ] **Step 5: Implement the real bodies**

Replace the stub `namespace GolfDisplay { ... }` block with:
```cpp
namespace GolfDisplay
{
	TOptional<FIntPoint> ParseResolution(const FString& In)
	{
		FString L, R;
		if (!In.Split(TEXT("x"), &L, &R) && !In.Split(TEXT("X"), &L, &R))
		{
			return TOptional<FIntPoint>();
		}
		const int32 W = FCString::Atoi(*L);
		const int32 H = FCString::Atoi(*R);
		if (W <= 0 || H <= 0)
		{
			return TOptional<FIntPoint>();
		}
		return FIntPoint(W, H);
	}

	int32 ClampQualityLevel(int32 Level) { return FMath::Clamp(Level, 0, 3); }
	int32 ClampWindowModeIndex(int32 Index) { return FMath::Clamp(Index, 0, 2); }

	FString CreditsText()
	{
		return FString::Join(TArray<FString>{
			TEXT("GolfForge"),
			TEXT(""),
			TEXT("Course data: © OpenStreetMap contributors (ODbL)."),
			TEXT("Elevation: USGS 3DEP / SRTM (public domain), via OpenTopography."),
			TEXT("Made with Unreal® Engine. © Epic Games, Inc."),
			TEXT(""),
			TEXT("GolfForge is licensed under AGPL-3.0, plus a commercial option."),
			TEXT("See LICENSE, COMMERCIAL.md, and ATTRIBUTION.md."),
		}, TEXT("\n"));
	}

	FGolfDisplaySettings ReadCurrent()
	{
		FGolfDisplaySettings S;
		if (UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr)
		{
			S.Resolution = GUS->GetScreenResolution();
			switch (GUS->GetFullscreenMode())
			{
				case EWindowMode::WindowedFullscreen: S.WindowModeIndex = 1; break;
				case EWindowMode::Fullscreen:         S.WindowModeIndex = 2; break;
				default:                              S.WindowModeIndex = 0; break;
			}
			const int32 Q = GUS->GetOverallScalabilityLevel();
			S.QualityLevel = (Q < 0) ? 3 : ClampQualityLevel(Q);
		}
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
		{
			const float V = CVar->GetFloat();
			S.ScreenPercentage = (V > 0.f) ? V : 100.f;
		}
		return S;
	}

	void Apply(const FGolfDisplaySettings& S)
	{
		UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
		if (!GUS)
		{
			return;
		}
		GUS->SetScreenResolution(S.Resolution);
		EWindowMode::Type Mode = EWindowMode::Windowed;
		switch (ClampWindowModeIndex(S.WindowModeIndex))
		{
			case 1: Mode = EWindowMode::WindowedFullscreen; break;
			case 2: Mode = EWindowMode::Fullscreen; break;
			default: Mode = EWindowMode::Windowed; break;
		}
		GUS->SetFullscreenMode(Mode);
		GUS->SetOverallScalabilityLevel(ClampQualityLevel(S.QualityLevel));
		GUS->ApplyResolutionSettings(false);
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
		{
			CVar->Set(FMath::Clamp(S.ScreenPercentage, 50.f, 100.f));
		}
		GUS->ApplySettings(false);
		GUS->SaveSettings();
	}

	TArray<FIntPoint> SupportedResolutions()
	{
		TArray<FIntPoint> Res;
		UKismetSystemLibrary::GetSupportedFullscreenResolutions(Res);
		if (Res.Num() == 0)
		{
			Res = { FIntPoint(1280, 720), FIntPoint(1920, 1080), FIntPoint(2560, 1440), FIntPoint(3840, 2160) };
		}
		return Res;
	}
}
```

- [ ] **Step 6: Build + run; verify tests PASS**

Build, then `Automation RunTests Golfsim.Settings` → all three **PASS**.

- [ ] **Step 7: Commit** (hand these to the user)

```bash
git add engine/Golfsim/Source/Golfsim/GolfDisplaySettings.h engine/Golfsim/Source/Golfsim/GolfDisplaySettings.cpp engine/Golfsim/Source/Golfsim/Tests/SettingsTests.cpp
git commit -m "feat(settings): display-settings module + tests (GOL-52)"
```

---

## Task 2: `USettingsMenu` widget (view)

**Files:**
- Create: `engine/Golfsim/Source/Golfsim/SettingsMenu.h`
- Create: `engine/Golfsim/Source/Golfsim/SettingsMenu.cpp`

- [ ] **Step 1: Write the header**

`SettingsMenu.h`:
```cpp
// Settings/Credits menu (UMG). Pure-C++ UUserWidget, no WBP -- same idiom as UGolfRangePanel. A
// centered modal with a dimmed backdrop, a Display | Credits nav, the display controls, and a
// credits scroll. Dumb view: reports the user's Apply/Close via TFunctions; AGolfRangeHUD owns logic.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Types/SlateEnums.h"
#include "GolfDisplaySettings.h"
#include "SettingsMenu.generated.h"

class UButton;
class UComboBoxString;
class USlider;
class UTextBlock;
class UVerticalBox;
class UScrollBox;

UCLASS()
class GOLFSIM_API USettingsMenu : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetResolutionOptions(const TArray<FIntPoint>& Resolutions);
	void SetCurrent(const FGolfDisplaySettings& S);   // seed controls from current values
	void SetCreditsText(const FString& Text);
	void ShowSection(int32 Index);                    // 0 = Display, 1 = Credits

	// Set by the owning HUD.
	TFunction<void(const FGolfDisplaySettings&)> OnApplyDisplay;
	TFunction<void()> OnClose;

protected:
	virtual void NativeOnInitialized() override;

	UFUNCTION() void HandleApplyClicked();
	UFUNCTION() void HandleCloseClicked();
	UFUNCTION() void HandleDisplayNavClicked();
	UFUNCTION() void HandleCreditsNavClicked();

private:
	void BuildTree();
	UButton* MakeButton(const TCHAR* Label);          // light button + black centered label

	UPROPERTY(Transient) TObjectPtr<UComboBoxString> ResCombo;
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> WindowCombo;
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> QualityCombo;
	UPROPERTY(Transient) TObjectPtr<USlider> ScreenPctSlider;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ScreenPctLabel;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> DisplayBox;
	UPROPERTY(Transient) TObjectPtr<UScrollBox> CreditsScroll;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CreditsBody;

	TArray<FIntPoint> ResOptions;   // index-aligned with ResCombo options
};
```

- [ ] **Step 2: Write the implementation**

`SettingsMenu.cpp`:
```cpp
#include "SettingsMenu.h"

#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "TimerManager.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ComboBoxString.h"
#include "Components/HorizontalBox.h"
#include "Components/ScrollBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/SlateTypes.h"

void USettingsMenu::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildTree();
}

UButton* USettingsMenu::MakeButton(const TCHAR* Label)
{
	UButton* B = WidgetTree->ConstructWidget<UButton>();
	UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>();
	T->SetText(FText::FromString(Label));
	T->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
	T->SetJustification(ETextJustify::Center);
	B->SetContent(T);
	return B;
}

void USettingsMenu::BuildTree()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	// Full-screen dim backdrop.
	UBorder* Dim = WidgetTree->ConstructWidget<UBorder>();
	Dim->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.6f));
	UCanvasPanelSlot* DimSlot = Root->AddChildToCanvas(Dim);
	DimSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
	DimSlot->SetOffsets(FMargin(0.f));

	// Centered card.
	UBorder* Card = WidgetTree->ConstructWidget<UBorder>();
	Card->SetBrushColor(FLinearColor(0.05f, 0.05f, 0.05f, 0.95f));
	Card->SetPadding(FMargin(20.f));
	UCanvasPanelSlot* CardSlot = Root->AddChildToCanvas(Card);
	CardSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	CardSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	CardSlot->SetAutoSize(true);

	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
	Card->SetContent(Col);

	UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>();
	Title->SetText(FText::FromString(TEXT("SETTINGS")));
	{ FSlateFontInfo F = Title->GetFont(); F.Size = 20; Title->SetFont(F); }
	Title->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.92f, 0.35f)));
	Col->AddChildToVerticalBox(Title);

	// Nav row: Display | Credits.
	UHorizontalBox* Nav = WidgetTree->ConstructWidget<UHorizontalBox>();
	UButton* DisplayNav = MakeButton(TEXT("Display"));
	DisplayNav->OnClicked.AddDynamic(this, &USettingsMenu::HandleDisplayNavClicked);
	UButton* CreditsNav = MakeButton(TEXT("Credits"));
	CreditsNav->OnClicked.AddDynamic(this, &USettingsMenu::HandleCreditsNavClicked);
	Nav->AddChildToHorizontalBox(DisplayNav);
	Nav->AddChildToHorizontalBox(CreditsNav);
	UVerticalBoxSlot* NavSlot = Col->AddChildToVerticalBox(Nav);
	NavSlot->SetPadding(FMargin(0.f, 8.f, 0.f, 8.f));

	// --- Display section ---
	DisplayBox = WidgetTree->ConstructWidget<UVerticalBox>();
	auto AddLabeledCombo = [&](const TCHAR* Label) -> UComboBoxString*
	{
		UTextBlock* Lbl = WidgetTree->ConstructWidget<UTextBlock>();
		Lbl->SetText(FText::FromString(Label));
		DisplayBox->AddChildToVerticalBox(Lbl);
		UComboBoxString* Combo = WidgetTree->ConstructWidget<UComboBoxString>();
		FTableRowStyle ItemStyle = Combo->GetItemStyle();
		ItemStyle.TextColor = FSlateColor(FLinearColor::White);
		Combo->SetItemStyle(ItemStyle);
		DisplayBox->AddChildToVerticalBox(Combo);
		return Combo;
	};
	ResCombo = AddLabeledCombo(TEXT("Resolution"));
	WindowCombo = AddLabeledCombo(TEXT("Window mode"));
	WindowCombo->AddOption(TEXT("Windowed"));
	WindowCombo->AddOption(TEXT("Borderless"));
	WindowCombo->AddOption(TEXT("Fullscreen"));
	QualityCombo = AddLabeledCombo(TEXT("Quality"));
	for (const TCHAR* Q : { TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Epic") }) { QualityCombo->AddOption(Q); }

	UTextBlock* PctLbl = WidgetTree->ConstructWidget<UTextBlock>();
	PctLbl->SetText(FText::FromString(TEXT("Screen %")));
	DisplayBox->AddChildToVerticalBox(PctLbl);
	ScreenPctSlider = WidgetTree->ConstructWidget<USlider>();
	ScreenPctSlider->SetMinValue(50.f);
	ScreenPctSlider->SetMaxValue(100.f);
	ScreenPctSlider->SetValue(100.f);
	DisplayBox->AddChildToVerticalBox(ScreenPctSlider);
	ScreenPctLabel = WidgetTree->ConstructWidget<UTextBlock>();
	ScreenPctLabel->SetText(FText::FromString(TEXT("100")));
	DisplayBox->AddChildToVerticalBox(ScreenPctLabel);

	UButton* ApplyBtn = MakeButton(TEXT("Apply"));
	ApplyBtn->OnClicked.AddDynamic(this, &USettingsMenu::HandleApplyClicked);
	UVerticalBoxSlot* ApplySlot = DisplayBox->AddChildToVerticalBox(ApplyBtn);
	ApplySlot->SetPadding(FMargin(0.f, 10.f, 0.f, 0.f));
	Col->AddChildToVerticalBox(DisplayBox);

	// --- Credits section ---
	CreditsScroll = WidgetTree->ConstructWidget<UScrollBox>();
	CreditsBody = WidgetTree->ConstructWidget<UTextBlock>();
	CreditsBody->SetText(FText::FromString(TEXT("")));
	CreditsScroll->AddChild(CreditsBody);
	Col->AddChildToVerticalBox(CreditsScroll);

	// Close (always visible at the bottom).
	UButton* CloseBtn = MakeButton(TEXT("Close"));
	CloseBtn->OnClicked.AddDynamic(this, &USettingsMenu::HandleCloseClicked);
	UVerticalBoxSlot* CloseSlot = Col->AddChildToVerticalBox(CloseBtn);
	CloseSlot->SetPadding(FMargin(0.f, 12.f, 0.f, 0.f));

	ShowSection(0);   // Display first
}

void USettingsMenu::ShowSection(int32 Index)
{
	if (DisplayBox)   { DisplayBox->SetVisibility(Index == 0 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
	if (CreditsScroll){ CreditsScroll->SetVisibility(Index == 1 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
}

void USettingsMenu::HandleDisplayNavClicked() { ShowSection(0); }
void USettingsMenu::HandleCreditsNavClicked() { ShowSection(1); }

void USettingsMenu::SetResolutionOptions(const TArray<FIntPoint>& Resolutions)
{
	ResOptions = Resolutions;
	if (!ResCombo) { return; }
	ResCombo->ClearOptions();
	for (const FIntPoint& R : Resolutions)
	{
		ResCombo->AddOption(FString::Printf(TEXT("%dx%d"), R.X, R.Y));
	}
}

void USettingsMenu::SetCreditsText(const FString& Text)
{
	if (CreditsBody) { CreditsBody->SetText(FText::FromString(Text)); }
}

void USettingsMenu::SetCurrent(const FGolfDisplaySettings& S)
{
	if (ResCombo)
	{
		const int32 Idx = ResOptions.IndexOfByPredicate([&](const FIntPoint& R){ return R == S.Resolution; });
		if (Idx != INDEX_NONE) { ResCombo->SetSelectedIndex(Idx); }
	}
	if (WindowCombo)  { WindowCombo->SetSelectedIndex(GolfDisplay::ClampWindowModeIndex(S.WindowModeIndex)); }
	if (QualityCombo) { QualityCombo->SetSelectedIndex(GolfDisplay::ClampQualityLevel(S.QualityLevel)); }
	if (ScreenPctSlider) { ScreenPctSlider->SetValue(S.ScreenPercentage); }
	if (ScreenPctLabel)  { ScreenPctLabel->SetText(FText::FromString(FString::Printf(TEXT("%.0f"), S.ScreenPercentage))); }
}

void USettingsMenu::HandleApplyClicked()
{
	FGolfDisplaySettings S;
	if (ResCombo && ResOptions.IsValidIndex(ResCombo->GetSelectedIndex()))
	{
		S.Resolution = ResOptions[ResCombo->GetSelectedIndex()];
	}
	S.WindowModeIndex = WindowCombo ? WindowCombo->GetSelectedIndex() : 0;
	S.QualityLevel    = QualityCombo ? QualityCombo->GetSelectedIndex() : 3;
	S.ScreenPercentage = ScreenPctSlider ? ScreenPctSlider->GetValue() : 100.f;
	if (ScreenPctLabel) { ScreenPctLabel->SetText(FText::FromString(FString::Printf(TEXT("%.0f"), S.ScreenPercentage))); }
	if (OnApplyDisplay) { OnApplyDisplay(S); }
}

void USettingsMenu::HandleCloseClicked()
{
	if (OnClose) { OnClose(); }
}
```

- [ ] **Step 3: Build (editor closed — new `UCLASS`)**

Run the Build.bat command. Expected: `Build succeeded`, `UnrealEditor-Golfsim.dll` rebuilt.

- [ ] **Step 4: Commit**

```bash
git add engine/Golfsim/Source/Golfsim/SettingsMenu.h engine/Golfsim/Source/Golfsim/SettingsMenu.cpp
git commit -m "feat(settings): USettingsMenu widget (GOL-52, GOL-59)"
```

---

## Task 3: HUD ownership, toggle, key gating, apply/close wiring

**Files:**
- Modify: `engine/Golfsim/Source/Golfsim/GolfRangeHUD.h`
- Modify: `engine/Golfsim/Source/Golfsim/GolfRangeHUD.cpp`

- [ ] **Step 1: Header additions**

In `GolfRangeHUD.h`: add the forward declaration `class USettingsMenu;` (near `class UManualShotDialog;`), and inside the class:
```cpp
	// Settings/credits menu (GOL-52/GOL-59): a key toggles a centered modal; gameplay keys are gated
	// while it's open. OpenCreditsSection is the entry point for golfsim.Credits.
	void ToggleSettingsMenu();
	void EnsureSettingsMenu();
	void ApplyDisplaySettings(const FGolfDisplaySettings& S);
public:
	void OpenCreditsSection();
private:
	bool bSettingsOpen = false;
```
Add the member near `Panel` / `ManualDialog`:
```cpp
	UPROPERTY(Transient) TObjectPtr<USettingsMenu> SettingsMenu;
```
Add the include near the others (top of header, after `GolfRangePanel.h`): `#include "GolfDisplaySettings.h"` (for the `FGolfDisplaySettings` parameter type).

- [ ] **Step 2: Bind the toggle key**

In `GolfRangeHUD.cpp`, in `EnsureInputBound()`, after the `M` bind (line ~391), add:
```cpp
	// Settings/credits menu. Escape works in packaged builds; in PIE the editor may intercept Escape to
	// stop play, so also bind Tab for reliable in-editor toggling. (golfsim.Credits also opens it.)
	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AGolfRangeHUD::ToggleSettingsMenu);
	InputComponent->BindKey(EKeys::Tab,    IE_Pressed, this, &AGolfRangeHUD::ToggleSettingsMenu);
```

- [ ] **Step 3: Add the include + menu create/toggle/apply**

In `GolfRangeHUD.cpp`, add near the top includes: `#include "SettingsMenu.h"`. Then add these definitions (e.g. after `ToggleManualDialog`):
```cpp
void AGolfRangeHUD::EnsureSettingsMenu()
{
	if (SettingsMenu)
	{
		return;
	}
	APlayerController* PC = GetOwningPlayerController();
	if (!PC)
	{
		return;
	}
	SettingsMenu = CreateWidget<USettingsMenu>(PC, USettingsMenu::StaticClass());
	if (!SettingsMenu)
	{
		return;
	}
	SettingsMenu->SetResolutionOptions(GolfDisplay::SupportedResolutions());
	SettingsMenu->SetCreditsText(GolfDisplay::CreditsText());
	SettingsMenu->SetCurrent(GolfDisplay::ReadCurrent());

	TWeakObjectPtr<AGolfRangeHUD> WeakThis(this);
	SettingsMenu->OnApplyDisplay = [WeakThis](const FGolfDisplaySettings& S)
	{
		if (AGolfRangeHUD* HUD = WeakThis.Get()) { HUD->ApplyDisplaySettings(S); }
	};
	SettingsMenu->OnClose = [WeakThis]()
	{
		if (AGolfRangeHUD* HUD = WeakThis.Get()) { HUD->ToggleSettingsMenu(); }
	};
	SettingsMenu->AddToViewport(20);   // above the range panel + manual dialog
	SettingsMenu->SetVisibility(ESlateVisibility::Collapsed);
}

void AGolfRangeHUD::ToggleSettingsMenu()
{
	EnsureSettingsMenu();
	if (!SettingsMenu)
	{
		return;
	}
	bSettingsOpen = !bSettingsOpen;
	SettingsMenu->SetVisibility(bSettingsOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (bSettingsOpen)
	{
		SettingsMenu->SetCurrent(GolfDisplay::ReadCurrent());   // reseed in case values changed elsewhere
	}
	else if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();   // hand keys back to gameplay
	}
}

void AGolfRangeHUD::ApplyDisplaySettings(const FGolfDisplaySettings& S)
{
	GolfDisplay::Apply(S);
	UE_LOG(LogTemp, Display, TEXT("golfsim settings: applied %dx%d windowMode=%d quality=%d screen%%=%.0f"),
		S.Resolution.X, S.Resolution.Y, S.WindowModeIndex, S.QualityLevel, S.ScreenPercentage);
}

void AGolfRangeHUD::OpenCreditsSection()
{
	EnsureSettingsMenu();
	if (!SettingsMenu)
	{
		return;
	}
	if (!bSettingsOpen)
	{
		ToggleSettingsMenu();
	}
	SettingsMenu->ShowSection(1);
}
```
`FSlateApplication` is already included in `GolfRangeHUD.cpp` (used elsewhere); if the build complains, add `#include "Framework/Application/SlateApplication.h"`.

- [ ] **Step 4: Gate gameplay keys while the menu is open**

In `GolfRangeHUD.cpp`, at the **top** of `SelectClub(int32)` and `FireRandom()`, add the early return:
```cpp
	if (bSettingsOpen) { return; }
```
And in `TurnLeftPressed()` / `TurnRightPressed()` (in the header), change them to ignore input while open — edit the header inline bodies to:
```cpp
	void TurnLeftPressed()   { if (!bSettingsOpen) { bTurnLeft = true; } }
	void TurnRightPressed()  { if (!bSettingsOpen) { bTurnRight = true; } }
```
(Leave the `*Released` setters as-is so a key held before opening can't get stuck.)

- [ ] **Step 5: Build (editor closed — new `UPROPERTY`/`UFUNCTION` on the HUD)**

Run Build.bat. Expected: `Build succeeded`.

- [ ] **Step 6: PIE verify**

Launch PIE. Verify:
- **Tab** opens a centered, dimmed menu; **Tab** (or **Close**) closes it. (Try **Escape** too — may stop PIE; that's the known editor caveat.)
- While open, **Space** and **1-6** do nothing (gated); after closing, they fire/select again.
- Clicking **Display** / **Credits** swaps the section; the Credits text shows the OSM / Unreal / USGS / AGPL lines.
- The resolution / window / quality dropdowns are populated; the Screen-% slider moves.

- [ ] **Step 7: Commit**

```bash
git add engine/Golfsim/Source/Golfsim/GolfRangeHUD.h engine/Golfsim/Source/Golfsim/GolfRangeHUD.cpp
git commit -m "feat(settings): HUD owns settings menu, Esc/Tab toggle + key gating (GOL-52, GOL-59)"
```

---

## Task 4: Apply actually changes + persists (PIE)

**Files:** none (verification of Task 1's `Apply` through the Task 3 wiring).

- [ ] **Step 1: PIE — apply changes the viewport**

In PIE, open the menu (Tab) → Display → pick a different resolution + window mode + quality + drag Screen-% → **Apply**. Expected: the viewport resolution/quality visibly change; the log line `golfsim settings: applied …` prints.

- [ ] **Step 2: PIE — settings persist across a restart**

Stop PIE, relaunch PIE (or a packaged build). Open the menu → the controls should reflect the values you applied (read back from `UGameUserSettings` / `GameUserSettings.ini`).

- [ ] **Step 3: (no commit — verification only)**

If a value doesn't persist, check the `Apply` path called `ApplySettings(false)` + `SaveSettings()` (Task 1, Step 5) and that `ReadCurrent` reads the same fields.

---

## Task 5: Console commands

**Files:**
- Modify: `engine/Golfsim/Source/Golfsim/GolfsimConsole.cpp`

- [ ] **Step 1: Add the command functions**

In `GolfsimConsole.cpp`, add the include `#include "GolfDisplaySettings.h"`, `#include "GolfRangeHUD.h"`, and `#include "GameFramework/HUD.h"`. Inside the anonymous `namespace { … }`, add:
```cpp
	void SetResolutionCmd(const TArray<FString>& Args, UWorld* /*World*/)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("Usage: golfsim.SetResolution <W>x<H>  (e.g. 1920x1080)"));
			return;
		}
		const TOptional<FIntPoint> Res = GolfDisplay::ParseResolution(Args[0]);
		if (!Res.IsSet())
		{
			UE_LOG(LogTemp, Warning, TEXT("golfsim.SetResolution: could not parse '%s' (expected WxH)"), *Args[0]);
			return;
		}
		FGolfDisplaySettings S = GolfDisplay::ReadCurrent();
		S.Resolution = Res.GetValue();
		GolfDisplay::Apply(S);
		UE_LOG(LogTemp, Display, TEXT("golfsim.SetResolution: %dx%d"), Res.GetValue().X, Res.GetValue().Y);
	}

	void SetQualityCmd(const TArray<FString>& Args, UWorld* /*World*/)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("Usage: golfsim.SetQuality <0-3>  (0=Low 1=Medium 2=High 3=Epic)"));
			return;
		}
		FGolfDisplaySettings S = GolfDisplay::ReadCurrent();
		S.QualityLevel = GolfDisplay::ClampQualityLevel(FCString::Atoi(*Args[0]));
		GolfDisplay::Apply(S);
		UE_LOG(LogTemp, Display, TEXT("golfsim.SetQuality: %d"), S.QualityLevel);
	}

	void CreditsCmd(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (!World) { return; }
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (AGolfRangeHUD* HUD = Cast<AGolfRangeHUD>(PC->GetHUD()))
			{
				HUD->OpenCreditsSection();
				return;
			}
		}
		UE_LOG(LogTemp, Warning, TEXT("golfsim.Credits: no AGolfRangeHUD in this level"));
	}
```

- [ ] **Step 2: Register the commands**

At file scope (with the other `FAutoConsoleCommandWithWorldAndArgs` statics):
```cpp
static FAutoConsoleCommandWithWorldAndArgs GSetResolutionCmd(
	TEXT("golfsim.SetResolution"),
	TEXT("Set the window resolution: golfsim.SetResolution <W>x<H> (persists)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetResolutionCmd));

static FAutoConsoleCommandWithWorldAndArgs GSetQualityCmd(
	TEXT("golfsim.SetQuality"),
	TEXT("Set the overall scalability level: golfsim.SetQuality <0-3> (persists)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetQualityCmd));

static FAutoConsoleCommandWithWorldAndArgs GCreditsCmd(
	TEXT("golfsim.Credits"),
	TEXT("Open the settings menu to the Credits/Attributions section."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&CreditsCmd));
```

- [ ] **Step 3: Build (editor closed)**

Run Build.bat. Expected: `Build succeeded`.

- [ ] **Step 4: PIE verify the commands**

In the PIE `~` console: `golfsim.SetResolution 1280x720` (viewport resizes), `golfsim.SetQuality 1` (quality drops), `golfsim.Credits` (menu opens to Credits). Bad input logs the usage warning.

- [ ] **Step 5: Commit**

```bash
git add engine/Golfsim/Source/Golfsim/GolfsimConsole.cpp
git commit -m "feat(settings): golfsim.SetResolution / SetQuality / Credits console commands (GOL-52, GOL-59)"
```

---

## Task 6: Full-suite verification

**Files:** none.

- [ ] **Step 1: Build + run the whole suite**

Build (editor closed), then `Automation RunTests Golfsim`.
Expected: the prior **24** tests + the **3** new `Golfsim.Settings.*` tests all **PASS** (27 total).

- [ ] **Step 2: PIE end-to-end checklist**

Confirm the spec's acceptance: menu opens/closes; each Display control changes the viewport on Apply; **settings persist across a restart**; gameplay keys gated while open; Credits readable and carries the OSM/Unreal/USGS/AGPL attributions; `golfsim.Credits` opens to Credits.

- [ ] **Step 3: Update Linear**

Mark GOL-52 and GOL-59 Done with a brief outcome comment (files landed, tests count, PIE-verified). GOL-59 unblocks the in-app-attribution prerequisite of GOL-49.
