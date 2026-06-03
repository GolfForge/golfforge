// GolfUITheme (GOL-138, epic GOL-137) — the single source of design tokens + reusable UMG atoms
// for the GolfForge UI. Resolved values come straight from Build/handoff/source/RESOLVED_TOKENS.md.
//
// Dark + fairway-green theme only this milestone, but everything is token-driven: call sites read
// GolfUI::Color::Accent() etc., never a literal, so the spec's light theme + 5 accent hues drop in
// later by swapping what these accessors return — zero widget changes.
//
// Colors: tokens are authored as sRGB hex. Slate paints brush tints in linear space, so the
// accessors return FLinearColor(FColor::FromHex(...)) — i.e. the sRGB value decoded to linear, so
// the pixel that lands on screen matches the design hex.
//
// Motion: UMG has no CSS transitions. Interaction states are FButtonStyle brush swaps
// (Normal/Hovered/Pressed/Disabled) built by the Make*/Style* helpers; hover "lift" is an instant
// render-transform via SetHoverLift (screens can lerp it from Tick). Per BUILD_SPEC §7 the resting
// state IS the base style — nothing here gates a control's visible/selected look on an animation.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"
#include "Fonts/SlateFontInfo.h"

class UWidgetTree;
class UBorder;
class UButton;
class UTextBlock;
class UWidget;
class UImage;

namespace GolfUI
{
	// ------------------------------------------------------------------ color tokens
	// Each returns a render-correct FLinearColor (sRGB hex decoded to linear). See file header.
	namespace Color
	{
		GOLFSIM_API FLinearColor Accent();       // #6FE276 fairway green — active states, live data, CTAs
		GOLFSIM_API FLinearColor AccentInk();    // #09260C — text/icon on top of accent fills
		GOLFSIM_API FLinearColor AccentSoft();   // accent @ 16% — soft fills behind selected items
		GOLFSIM_API FLinearColor AccentLine();   // accent @ 42% — accent hairlines / rings

		GOLFSIM_API FLinearColor Bg0();          // #0A0E0C — base canvas
		GOLFSIM_API FLinearColor Bg1();          // #101513 — solid panel cards
		GOLFSIM_API FLinearColor BgGlow();       // #203329 — radial glow accents
		GOLFSIM_API FLinearColor GlassFill();    // rgba(3,7,5,0.72) — glass HUD overlays (pair with blur)

		GOLFSIM_API FLinearColor Text();         // #F3F6F4 — primary
		GOLFSIM_API FLinearColor TextDim();      // #A7ACA9 — secondary
		GOLFSIM_API FLinearColor TextFaint();    // #707672 — labels / eyebrows

		GOLFSIM_API FLinearColor Caution();      // #FFBC56 amber — game-mode pill, unsaved warnings
		GOLFSIM_API FLinearColor DangerFill();   // #D74745 — destructive fills
		GOLFSIM_API FLinearColor DangerText();   // #FF958D — destructive text

		// White-over-canvas alpha ramps (insets, tracks, chips, hairlines).
		GOLFSIM_API FLinearColor Surface();      // white 4.5%
		GOLFSIM_API FLinearColor Surface2();     // white 7%
		GOLFSIM_API FLinearColor Surface3();     // white 10%
		GOLFSIM_API FLinearColor Border();       // white 10%
		GOLFSIM_API FLinearColor BorderStrong(); // white 20%
	}

	// ------------------------------------------------------------------ radius tokens (px)
	namespace Radius
	{
		constexpr float Lg = 18.f;   // --r-lg : cards, panels
		constexpr float Md = 12.f;   // --r-md : buttons, tiles
		constexpr float Sm = 8.f;    // --r-sm : chips, insets
	}

	// ------------------------------------------------------------------ fonts
	// Weight is a typeface FName inside the imported font asset (Regular / Medium / SemiBold / Bold).
	// Until the .ttf assets are imported under /Game/UI/Fonts these gracefully fall back to the engine
	// default face at the requested size, so everything compiles and renders before the import lands.
	GOLFSIM_API FSlateFontInfo Display(int32 Size, FName Weight = FName(TEXT("SemiBold"))); // Barlow Condensed
	GOLFSIM_API FSlateFontInfo Body(int32 Size, FName Weight = FName(TEXT("Regular")));     // Barlow
	GOLFSIM_API FSlateFontInfo Mono(int32 Size, FName Weight = FName(TEXT("Regular")));     // JetBrains Mono

	// ------------------------------------------------------------------ brushes
	// Rounded fill, optional outline. Radius is one of GolfUI::Radius::*.
	GOLFSIM_API FSlateBrush RoundedBrush(const FLinearColor& Fill, float Radius,
		const FLinearColor& Outline = FLinearColor(0, 0, 0, 0), float OutlineWidth = 0.f);
	GOLFSIM_API FSlateBrush GlassPanelBrush(); // glass fill + hairline border, --r-lg
	GOLFSIM_API FSlateBrush CardBrush();       // solid Bg1 card + hairline border, --r-lg

	// ------------------------------------------------------------------ atoms
	// All constructed into the caller's WidgetTree (matches the codebase's BuildTree() idiom).
	GOLFSIM_API UTextBlock* MakeEyebrow(UWidgetTree* Tree, const FString& Text);                       // mono, tracked, UPPER, faint
	GOLFSIM_API UTextBlock* MakeTitle(UWidgetTree* Tree, const FString& Text, int32 Size = 28);        // Barlow Condensed display
	GOLFSIM_API UTextBlock* MakeMonoNumber(UWidgetTree* Tree, const FString& Text, int32 Size,
		const FLinearColor& Col = Color::Text());                                                      // tabular telemetry number
	GOLFSIM_API UBorder*    MakeGlassPanel(UWidgetTree* Tree);                                         // glass border, fill content via SetContent
	GOLFSIM_API UBorder*    MakeCard(UWidgetTree* Tree);                                               // solid card
	GOLFSIM_API UBorder*    MakeKbd(UWidgetTree* Tree, const FString& Key);                            // keycap chip
	GOLFSIM_API UBorder*    MakeStatusDot(UWidgetTree* Tree, const FLinearColor& Col = Color::Accent());// small round status dot
	GOLFSIM_API UButton*    MakeAccentButton(UWidgetTree* Tree, const FString& Label);                 // filled fairway, ink text
	GOLFSIM_API UButton*    MakeGhostButton(UWidgetTree* Tree, const FString& Label);                  // outline, accent border on hover

	// ------------------------------------------------------------------ gradients (GOL-150)
	// UImage brushes backed by /Game/UI/Materials/M_UIGradient{Linear,Radial} (each gets its own MID, so
	// callers can place several with different colours). Linear is vertical (Bottom at V=1, Top at V=0);
	// radial fades Inner->Outer from Center over Radius, in 0..1 UV space. Slate has no native gradient
	// brush, so this is how the bg ambiance + tile hover-wash are drawn. Falls back to a flat-colour
	// image if the material asset isn't present (so it compiles/renders before the asset lands).
	GOLFSIM_API UImage* MakeLinearGradient(UWidgetTree* Tree, const FLinearColor& Bottom, const FLinearColor& Top);
	GOLFSIM_API UImage* MakeRadialGradient(UWidgetTree* Tree, const FLinearColor& Inner, const FLinearColor& Outer,
		FVector2D Center = FVector2D(0.5f, 0.5f), float Radius = 0.7f);

	// ------------------------------------------------------------------ interaction / motion
	// Apply a rounded FButtonStyle with Normal/Hovered/Pressed/Disabled fills (hover/pressed derive
	// from Fill if not given). Text color is the caller's concern.
	GOLFSIM_API void StyleButton(UButton* Button, const FLinearColor& Fill, float Radius = Radius::Md,
		const FLinearColor& Outline = FLinearColor(0, 0, 0, 0), float OutlineWidth = 0.f);

	// Instant render-transform lift used on hover-enter / leave. Pixels < 0 lifts up (negative Y).
	// Screens wanting smooth motion lerp toward this from their Tick; per §7 the lift is decorative —
	// never required for the control to read as hovered/selected.
	GOLFSIM_API void SetHoverLift(UWidget* Widget, bool bHovered, float Pixels = -4.f, float Scale = 1.f);
}
