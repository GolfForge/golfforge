#include "UI/GolfUITheme.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"          // UWidget::SetRenderTransform + FWidgetTransform
#include "Engine/Font.h"                // UFont (font assets)
#include "Styling/CoreStyle.h"          // fallback font face before the .ttf assets are imported
#include "Brushes/SlateRoundedBoxBrush.h"

namespace
{
	// sRGB hex -> render-correct FLinearColor (FColor->FLinearColor decodes sRGB to linear). See header.
	FORCEINLINE FLinearColor Hex(const TCHAR* H) { return FLinearColor(FColor::FromHex(FString(H))); }

	// White at a given alpha — the surface/border ramps. White is identical in sRGB and linear.
	FORCEINLINE FLinearColor WhiteA(float A) { return FLinearColor(1.f, 1.f, 1.f, A); }

	// One token-color with a custom alpha (accent-soft / accent-line / glass).
	FORCEINLINE FLinearColor WithAlpha(FLinearColor C, float A) { C.A = A; return C; }

	// Load a family's font asset; null until the import lands (GolfUI::Display/Body/Mono fall back).
	UFont* LoadFamily(const TCHAR* AssetPath) { return LoadObject<UFont>(nullptr, AssetPath); }

	FSlateFontInfo FontOrFallback(const TCHAR* AssetPath, int32 Size, FName Weight)
	{
		if (UFont* Font = LoadFamily(AssetPath))
		{
			return FSlateFontInfo(Font, Size, Weight);
		}
		// Engine default face (Roboto) at the requested size, so widgets render before the import.
		return FCoreStyle::GetDefaultFontStyle("Regular", Size);
	}
}

namespace GolfUI
{
	namespace Color
	{
		FLinearColor Accent()       { return Hex(TEXT("6FE276")); }
		FLinearColor AccentInk()    { return Hex(TEXT("09260C")); }
		FLinearColor AccentSoft()   { return WithAlpha(Accent(), 0.16f); }
		FLinearColor AccentLine()   { return WithAlpha(Accent(), 0.42f); }

		FLinearColor Bg0()          { return Hex(TEXT("0A0E0C")); }
		FLinearColor Bg1()          { return Hex(TEXT("101513")); }
		FLinearColor BgGlow()       { return Hex(TEXT("203329")); }
		FLinearColor GlassFill()    { return WithAlpha(Hex(TEXT("030705")), 0.72f); }

		FLinearColor Text()         { return Hex(TEXT("F3F6F4")); }
		FLinearColor TextDim()      { return Hex(TEXT("A7ACA9")); }
		FLinearColor TextFaint()    { return Hex(TEXT("707672")); }

		FLinearColor Caution()      { return Hex(TEXT("FFBC56")); }
		FLinearColor DangerFill()   { return Hex(TEXT("D74745")); }
		FLinearColor DangerText()   { return Hex(TEXT("FF958D")); }

		FLinearColor Surface()      { return WhiteA(0.045f); }
		FLinearColor Surface2()     { return WhiteA(0.07f); }
		FLinearColor Surface3()     { return WhiteA(0.10f); }
		FLinearColor Border()       { return WhiteA(0.10f); }
		FLinearColor BorderStrong() { return WhiteA(0.20f); }
	}

	FSlateFontInfo Display(int32 Size, FName Weight)
	{
		return FontOrFallback(TEXT("/Game/UI/Fonts/BarlowCondensed.BarlowCondensed"), Size, Weight);
	}
	FSlateFontInfo Body(int32 Size, FName Weight)
	{
		return FontOrFallback(TEXT("/Game/UI/Fonts/Barlow.Barlow"), Size, Weight);
	}
	FSlateFontInfo Mono(int32 Size, FName Weight)
	{
		return FontOrFallback(TEXT("/Game/UI/Fonts/JetBrainsMono.JetBrainsMono"), Size, Weight);
	}

	FSlateBrush RoundedBrush(const FLinearColor& Fill, float Radius, const FLinearColor& Outline, float OutlineWidth)
	{
		// FSlateRoundedBoxBrush sets DrawAs=RoundedBox + the corner radii + outline; slicing to the
		// FSlateBrush base is safe (it adds no members, only a constructor).
		return FSlateRoundedBoxBrush(FSlateColor(Fill), Radius, FSlateColor(Outline), OutlineWidth);
	}

	FSlateBrush GlassPanelBrush()
	{
		return RoundedBrush(Color::GlassFill(), Radius::Lg, Color::Border(), 1.f);
	}
	FSlateBrush CardBrush()
	{
		return RoundedBrush(Color::Bg1(), Radius::Lg, Color::Border(), 1.f);
	}

	UTextBlock* MakeEyebrow(UWidgetTree* Tree, const FString& Text)
	{
		UTextBlock* T = Tree->ConstructWidget<UTextBlock>();
		T->SetText(FText::FromString(Text.ToUpper()));
		FSlateFontInfo F = Mono(11);
		F.LetterSpacing = 220;   // ~0.22em (thousandths of em)
		T->SetFont(F);
		T->SetColorAndOpacity(FSlateColor(Color::TextFaint()));
		return T;
	}

	UTextBlock* MakeTitle(UWidgetTree* Tree, const FString& Text, int32 Size)
	{
		UTextBlock* T = Tree->ConstructWidget<UTextBlock>();
		T->SetText(FText::FromString(Text));
		T->SetFont(Display(Size, FName(TEXT("SemiBold"))));
		T->SetColorAndOpacity(FSlateColor(Color::Text()));
		return T;
	}

	UTextBlock* MakeMonoNumber(UWidgetTree* Tree, const FString& Text, int32 Size, const FLinearColor& Col)
	{
		UTextBlock* T = Tree->ConstructWidget<UTextBlock>();
		T->SetText(FText::FromString(Text));
		T->SetFont(Mono(Size, FName(TEXT("Medium"))));
		T->SetColorAndOpacity(FSlateColor(Col));
		return T;
	}

	UBorder* MakeGlassPanel(UWidgetTree* Tree)
	{
		UBorder* B = Tree->ConstructWidget<UBorder>();
		B->SetBrush(GlassPanelBrush());
		B->SetPadding(FMargin(16.f));
		return B;
	}

	UBorder* MakeCard(UWidgetTree* Tree)
	{
		UBorder* B = Tree->ConstructWidget<UBorder>();
		B->SetBrush(CardBrush());
		B->SetPadding(FMargin(16.f));
		return B;
	}

	UBorder* MakeKbd(UWidgetTree* Tree, const FString& Key)
	{
		UBorder* B = Tree->ConstructWidget<UBorder>();
		B->SetBrush(RoundedBrush(Color::Surface(), 6.f, Color::BorderStrong(), 1.f));
		B->SetPadding(FMargin(7.f, 2.f));
		UTextBlock* T = Tree->ConstructWidget<UTextBlock>();
		T->SetText(FText::FromString(Key));
		T->SetFont(Mono(11));
		T->SetColorAndOpacity(FSlateColor(Color::TextDim()));
		B->SetContent(T);
		return B;
	}

	UBorder* MakeStatusDot(UWidgetTree* Tree, const FLinearColor& Col)
	{
		// A small fully-rounded fill. Caller sizes it via its slot (~8x8); pulse animation is the
		// status-pill ticket's concern (GOL-145).
		UBorder* B = Tree->ConstructWidget<UBorder>();
		B->SetBrush(RoundedBrush(Col, 999.f));
		return B;
	}

	void StyleButton(UButton* Button, const FLinearColor& Fill, float Radius,
		const FLinearColor& Outline, float OutlineWidth)
	{
		if (!Button) { return; }

		FLinearColor Hover = FMath::Lerp(Fill, FLinearColor::White, 0.10f); Hover.A = Fill.A;
		FLinearColor Press = FMath::Lerp(Fill, FLinearColor::Black, 0.12f); Press.A = Fill.A;
		FLinearColor Disabled = WithAlpha(Fill, Fill.A * 0.4f);

		FButtonStyle S;
		S.SetNormal(RoundedBrush(Fill, Radius, Outline, OutlineWidth));
		S.SetHovered(RoundedBrush(Hover, Radius, Outline, OutlineWidth));
		S.SetPressed(RoundedBrush(Press, Radius, Outline, OutlineWidth));
		S.SetDisabled(RoundedBrush(Disabled, Radius, Outline, OutlineWidth));
		Button->SetStyle(S);
	}

	UButton* MakeAccentButton(UWidgetTree* Tree, const FString& Label)
	{
		UButton* B = Tree->ConstructWidget<UButton>();
		StyleButton(B, Color::Accent(), Radius::Md);
		UTextBlock* T = Tree->ConstructWidget<UTextBlock>();
		T->SetText(FText::FromString(Label));
		T->SetFont(Body(14, FName(TEXT("SemiBold"))));
		T->SetColorAndOpacity(FSlateColor(Color::AccentInk()));
		T->SetJustification(ETextJustify::Center);
		B->SetContent(T);
		return B;
	}

	UButton* MakeGhostButton(UWidgetTree* Tree, const FString& Label)
	{
		UButton* B = Tree->ConstructWidget<UButton>();
		// Transparent resting fill + hairline; hover lifts to a faint surface with an accent line.
		FButtonStyle S;
		S.SetNormal(RoundedBrush(FLinearColor(0, 0, 0, 0), Radius::Md, Color::Border(), 1.f));
		S.SetHovered(RoundedBrush(Color::Surface2(), Radius::Md, Color::AccentLine(), 1.f));
		S.SetPressed(RoundedBrush(Color::Surface(), Radius::Md, Color::AccentLine(), 1.f));
		S.SetDisabled(RoundedBrush(FLinearColor(0, 0, 0, 0), Radius::Md, WithAlpha(Color::Border(), 0.04f), 1.f));
		B->SetStyle(S);

		UTextBlock* T = Tree->ConstructWidget<UTextBlock>();
		T->SetText(FText::FromString(Label));
		T->SetFont(Body(14, FName(TEXT("SemiBold"))));
		T->SetColorAndOpacity(FSlateColor(Color::Text()));
		T->SetJustification(ETextJustify::Center);
		B->SetContent(T);
		return B;
	}

	void SetHoverLift(UWidget* Widget, bool bHovered, float Pixels, float Scale)
	{
		if (!Widget) { return; }
		FWidgetTransform T;
		T.Translation = FVector2D(0.f, bHovered ? Pixels : 0.f);
		T.Scale = FVector2D(bHovered ? Scale : 1.f, bHovered ? Scale : 1.f);
		T.Shear = FVector2D::ZeroVector;
		T.Angle = 0.f;
		Widget->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		Widget->SetRenderTransform(T);
	}
}
