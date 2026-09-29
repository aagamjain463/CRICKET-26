// Reusable frontend components, built in C++ so the shell needs no widget Blueprints. Every screen is composed
// from these, which is what keeps buttons, cards, badges and controls identical across the product.
// Callbacks are TFunctions; the widgets that own them live in the screen's WidgetTree.

#pragma once

#include "CoreMinimal.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/Slider.h"
#include "FrontendStyle.h"
#include "FrontendTypes.h"
#include "Layout/Margin.h"
#include "FrontendUI.generated.h"

class UBorder;
class UHorizontalBox;
class UHorizontalBoxSlot;
class UOverlay;
class UOverlaySlot;
class UTextBlock;
class UVerticalBox;
class UVerticalBoxSlot;
class UWidgetTree;

// A button with a native tap callback and a small press-scale for touch feedback.
UCLASS()
class UFrontendButton : public UButton
{
	GENERATED_BODY()

public:
	TFunction<void()> OnTap;
	void Bind();

private:
	UFUNCTION() void HandleClicked();
	UFUNCTION() void HandlePressed();
	UFUNCTION() void HandleReleased();
};

UCLASS()
class UFrontendSlider : public USlider
{
	GENERATED_BODY()

public:
	TFunction<void(float)> OnChange;
	void Bind();

private:
	UFUNCTION() void HandleChanged(float NewValue);
	UFUNCTION() void HandleCommitted();
};

namespace FrontendUI
{
	using FrontendStyle::EWeight;

	enum class EButtonKind : uint8
	{
		Primary,   // gold: the one main action on a screen
		Secondary, // raised surface
		Ghost,     // outline only
		Quiet,     // invisible until hovered/pressed (nav items, list rows)
		Card       // a whole card that is tappable
	};

	// Layout. Fill 0 sizes to content; above 0 is the share of the remaining space.
	UVerticalBox* VBox(UWidgetTree* T);
	UHorizontalBox* HBox(UWidgetTree* T);
	UOverlay* Stack(UWidgetTree* T);
	UVerticalBoxSlot* Add(UVerticalBox* Box, UWidget* W, const FMargin& Pad = FMargin(0.f), float Fill = 0.f, EHorizontalAlignment H = HAlign_Fill);
	UHorizontalBoxSlot* Add(UHorizontalBox* Box, UWidget* W, const FMargin& Pad = FMargin(0.f), float Fill = 0.f, EVerticalAlignment V = VAlign_Fill);
	UOverlaySlot* Add(UOverlay* Box, UWidget* W, EHorizontalAlignment H = HAlign_Fill, EVerticalAlignment V = VAlign_Fill, const FMargin& Pad = FMargin(0.f));
	UWidget* Space(UWidgetTree* T, float W, float H);
	UWidget* Sized(UWidgetTree* T, UWidget* Content, float W, float H); // 0 leaves that axis free

	// Type.
	UTextBlock* Text(UWidgetTree* T, const FString& S, int32 Size, const FLinearColor& Colour, EWeight Weight = EWeight::Regular, int32 Spacing = 0);
	UTextBlock* Eyebrow(UWidgetTree* T, const FString& S, const FLinearColor& Colour = FrontendStyle::InkFaint());
	UTextBlock* Icon(UWidgetTree* T, TCHAR Glyph, int32 Size, const FLinearColor& Colour); // FrontendStyle::Glyph codepoints
	UTextBlock* Para(UWidgetTree* T, const FString& S, int32 Size = FrontendStyle::Body, const FLinearColor& Colour = FrontendStyle::InkDim());

	// Surfaces.
	UBorder* Box(UWidgetTree* T, const FSlateBrush& Brush, const FMargin& Pad = FMargin(0.f), UWidget* Content = nullptr);
	// Aligns a border's content. UBorder's own setters skip a slot that already exists, so this sets both.
	void Align(UBorder* B, EHorizontalAlignment H, EVerticalAlignment V);
	UBorder* Panel(UWidgetTree* T, UWidget* Content, const FMargin& Pad = FMargin(FrontendStyle::S3), const FLinearColor& Fill = FrontendStyle::Card());
	UImage* Glow(UWidgetTree* T, const FLinearColor& Tint, float Size);
	UImage* Fade(UWidgetTree* T, const FLinearColor& Tint, bool bVertical);
	UWidget* Divider(UWidgetTree* T);

	// Actions.
	UFrontendButton* Button(UWidgetTree* T, UWidget* Content, EButtonKind Kind, TFunction<void()> OnTap, float Radius = FrontendStyle::RButton);
	UFrontendButton* CTA(UWidgetTree* T, const FString& Label, EButtonKind Kind, TFunction<void()> OnTap, float Height = 64.f);
	// A disabled-looking button for features that are not built yet; still tappable so it can explain itself.
	UFrontendButton* PendingCTA(UWidgetTree* T, EFeatureStatus Status, TFunction<void()> OnTap, float Height = 56.f);

	// Status and data.
	UWidget* Pill(UWidgetTree* T, const FString& S, const FLinearColor& Tint, bool bSolid = false);
	UWidget* Badge(UWidgetTree* T, EFeatureStatus Status);
	UWidget* Dot(UWidgetTree* T, const FLinearColor& Colour, float Size = 10.f);
	UWidget* Meter(UWidgetTree* T, float Value01, const FLinearColor& Colour, float Height = 6.f);
	UWidget* SectionHeader(UWidgetTree* T, const FString& Eyebrow, const FString& Title, UWidget* Trailing = nullptr);

	// Controls.
	UWidget* Segmented(UWidgetTree* T, const TArray<FString>& Options, int32 Selected, TFunction<void(int32)> OnChange);
	UWidget* Toggle(UWidgetTree* T, bool bOn, TFunction<void(bool)> OnChange);
	UWidget* Slider(UWidgetTree* T, float Value01, TFunction<void(float)> OnChange);
	UWidget* SettingRow(UWidgetTree* T, const FString& Title, const FString& Subtitle, UWidget* Control, bool bWideControl = false);

	// Composites.
	UWidget* FeatureCard(UWidgetTree* T, const FFrontendFeature& F, TFunction<void()> OnTap, float MinHeight = 220.f);
	UWidget* PlayerTile(UWidgetTree* T, const FFranchisePlayerRow& P, const FLinearColor& TeamColour);
	UWidget* OpenSlotTile(UWidgetTree* T, const FString& Title, const FString& Subtitle);
	// Artwork and brand. Names are textures in /Game/UI.
	UWidget* Backdrop(UWidgetTree* T, const TCHAR* Name, const FLinearColor& Tint = FLinearColor::White); // crops to fill
	UWidget* Picture(UWidgetTree* T, const TCHAR* Name, float Height, const FLinearColor& Tint = FLinearColor::White); // keeps aspect
	UWidget* Fit(UWidgetTree* T, UWidget* Content); // shrinks to the space it gets, never grows
	UWidget* Logo(UWidgetTree* T, float Scale = 1.f);
	UWidget* NamePlate(UWidgetTree* T, const FString& Name, const FString& Caption, const FLinearColor& Colour, bool bRight);
	UWidget* Versus(UWidgetTree* T, float Height);

	// A football-game style player card: foil frame, render, rating and role stacked top-left, name plate below.
	UWidget* PlayerCard(UWidgetTree* T, const FFranchisePlayerRow& P, const TCHAR* Render, bool bElite, float Height);

	// Looping and entrance motion. Widgets register once; whichever user widget owns them calls TickMotion each frame.
	enum class EMotion : uint8
	{
		Shine, // slides across its (clipped) parent every few seconds; Param staggers the start
		Pulse, // breathes its opacity; Param is the phase
		Enter  // slides in from the right when a page opens; Param is the delay in seconds
	};
	void Animate(UWidget* W, EMotion Motion, float Param = 0.f);
	void TickMotion(float Clock, float SincePageOpened);

	UWidget* EmptyState(UWidgetTree* T, const FString& Title, const FString& Body, EFeatureStatus Status);
}
