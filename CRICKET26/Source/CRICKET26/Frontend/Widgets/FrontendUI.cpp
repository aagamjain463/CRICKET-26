#include "Widgets/FrontendUI.h"
#include "Components/BorderSlot.h"
#include "Components/ButtonSlot.h"
#include "FrontendData.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/ScaleBox.h"
#include "Engine/Texture2D.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

using namespace FrontendStyle;

void UFrontendButton::Bind()
{
	OnClicked.AddUniqueDynamic(this, &UFrontendButton::HandleClicked);
	OnPressed.AddUniqueDynamic(this, &UFrontendButton::HandlePressed);
	OnReleased.AddUniqueDynamic(this, &UFrontendButton::HandleReleased);
}

void UFrontendButton::HandleClicked()
{
	if (OnTap) OnTap();
}

void UFrontendButton::HandlePressed() { SetRenderScale(FVector2D(0.97f)); }
void UFrontendButton::HandleReleased() { SetRenderScale(FVector2D(1.f)); }

void UFrontendSlider::Bind()
{
	OnValueChanged.AddUniqueDynamic(this, &UFrontendSlider::HandleChanged);
	OnMouseCaptureEnd.AddUniqueDynamic(this, &UFrontendSlider::HandleCommitted);
}

void UFrontendSlider::HandleChanged(float NewValue)
{
	if (OnChange) OnChange(NewValue);
}

void UFrontendSlider::HandleCommitted()
{
	if (OnChange) OnChange(GetValue());
}

namespace FrontendUI
{
	UVerticalBox* VBox(UWidgetTree* T) { return T->ConstructWidget<UVerticalBox>(); }
	UHorizontalBox* HBox(UWidgetTree* T) { return T->ConstructWidget<UHorizontalBox>(); }
	UOverlay* Stack(UWidgetTree* T) { return T->ConstructWidget<UOverlay>(); }

	UVerticalBoxSlot* Add(UVerticalBox* Box, UWidget* W, const FMargin& Pad, float Fill, EHorizontalAlignment H)
	{
		UVerticalBoxSlot* S = Box->AddChildToVerticalBox(W);
		S->SetPadding(Pad);
		S->SetHorizontalAlignment(H);
		if (Fill > 0.f)
		{
			FSlateChildSize Size(ESlateSizeRule::Fill);
			Size.Value = Fill;
			S->SetSize(Size);
		}
		return S;
	}

	UHorizontalBoxSlot* Add(UHorizontalBox* Box, UWidget* W, const FMargin& Pad, float Fill, EVerticalAlignment V)
	{
		UHorizontalBoxSlot* S = Box->AddChildToHorizontalBox(W);
		S->SetPadding(Pad);
		S->SetVerticalAlignment(V);
		if (Fill > 0.f)
		{
			FSlateChildSize Size(ESlateSizeRule::Fill);
			Size.Value = Fill;
			S->SetSize(Size);
		}
		return S;
	}

	UOverlaySlot* Add(UOverlay* Box, UWidget* W, EHorizontalAlignment H, EVerticalAlignment V, const FMargin& Pad)
	{
		UOverlaySlot* S = Box->AddChildToOverlay(W);
		S->SetHorizontalAlignment(H);
		S->SetVerticalAlignment(V);
		S->SetPadding(Pad);
		return S;
	}

	UWidget* Space(UWidgetTree* T, float W, float H)
	{
		USpacer* S = T->ConstructWidget<USpacer>();
		S->SetSize(FVector2D(W, H));
		return S;
	}

	UWidget* Sized(UWidgetTree* T, UWidget* Content, float W, float H)
	{
		USizeBox* S = T->ConstructWidget<USizeBox>();
		if (W > 0.f) S->SetWidthOverride(W);
		if (H > 0.f) S->SetHeightOverride(H);
		S->SetContent(Content);
		return S;
	}

	UTextBlock* Text(UWidgetTree* T, const FString& S, int32 Size, const FLinearColor& Colour, EWeight Weight, int32 Spacing)
	{
		UTextBlock* Txt = T->ConstructWidget<UTextBlock>();
		Txt->SetText(FText::FromString(S));
		Txt->SetFont(Font(Size, Weight, Spacing));
		Txt->SetColorAndOpacity(FSlateColor(Colour));
		return Txt;
	}

	UTextBlock* Icon(UWidgetTree* T, TCHAR Glyph, int32 Size, const FLinearColor& Colour)
	{
		UTextBlock* Txt = T->ConstructWidget<UTextBlock>();
		Txt->SetText(FText::FromString(FString::Chr(Glyph)));
		Txt->SetFont(IconFont(Size));
		Txt->SetColorAndOpacity(FSlateColor(Colour));
		return Txt;
	}

	UTextBlock* Eyebrow(UWidgetTree* T, const FString& S, const FLinearColor& Colour)
	{
		return Text(T, S.ToUpper(), Label, Colour, EWeight::Condensed, 180);
	}

	UTextBlock* Para(UWidgetTree* T, const FString& S, int32 Size, const FLinearColor& Colour)
	{
		UTextBlock* Txt = Text(T, S, Size, Colour, EWeight::Regular);
		Txt->SetAutoWrapText(true);
		Txt->SetLineHeightPercentage(1.15f);
		return Txt;
	}

	UBorder* Box(UWidgetTree* T, const FSlateBrush& Brush, const FMargin& Pad, UWidget* Content)
	{
		UBorder* B = T->ConstructWidget<UBorder>();
		B->SetBrush(Brush);
		B->SetPadding(Pad);
		if (Content) B->SetContent(Content);
		return B;
	}

	void Align(UBorder* B, EHorizontalAlignment H, EVerticalAlignment V)
	{
		B->SetHorizontalAlignment(H);
		B->SetVerticalAlignment(V);
		if (UBorderSlot* Slot = Cast<UBorderSlot>(B->GetContentSlot()))
		{
			Slot->SetHorizontalAlignment(H);
			Slot->SetVerticalAlignment(V);
		}
	}

	UBorder* Panel(UWidgetTree* T, UWidget* Content, const FMargin& Pad, const FLinearColor& Fill)
	{
		return Box(T, Rounded(Fill, RCard, Line()), Pad, Content);
	}

	UImage* Glow(UWidgetTree* T, const FLinearColor& Tint, float Size)
	{
		UImage* I = T->ConstructWidget<UImage>();
		FSlateBrush B = TextureBrush(GlowTexture(), Tint);
		B.ImageSize = FVector2D(Size);
		I->SetBrush(B);
		I->SetVisibility(ESlateVisibility::HitTestInvisible);
		return I;
	}

	UImage* Fade(UWidgetTree* T, const FLinearColor& Tint, bool bVertical)
	{
		UImage* I = T->ConstructWidget<UImage>();
		I->SetBrush(TextureBrush(FadeTexture(bVertical), Tint));
		I->SetVisibility(ESlateVisibility::HitTestInvisible);
		return I;
	}

	UWidget* Divider(UWidgetTree* T)
	{
		return Sized(T, Box(T, Flat(Line())), 0.f, 1.f);
	}

	namespace
	{
		FButtonStyle StyleFor(EButtonKind Kind, float Radius)
		{
			FLinearColor N, H, P, O = FLinearColor::Transparent;
			switch (Kind)
			{
			case EButtonKind::Primary: N = Gold(); H = GoldHi(); P = GoldLo(); break;
		case EButtonKind::Secondary: N = Glass(); H = CardHi(); P = Card(); O = Hex(0xA9C2FF, 0.28f); break;
			case EButtonKind::Ghost: N = FLinearColor::Transparent; H = FLinearColor(1.f, 1.f, 1.f, 0.06f); P = FLinearColor(1.f, 1.f, 1.f, 0.03f); O = FLinearColor(1.f, 1.f, 1.f, 0.22f); break;
			case EButtonKind::Quiet: N = FLinearColor::Transparent; H = FLinearColor(1.f, 1.f, 1.f, 0.05f); P = FLinearColor(1.f, 1.f, 1.f, 0.08f); break;
		case EButtonKind::Card: N = Card(); H = CardHi(); P = Surface(); O = Line(); break;
			}
			FButtonStyle S;
			S.Normal = Rounded(N, Radius, O);
			S.Hovered = Rounded(H, Radius, Kind == EButtonKind::Card ? FLinearColor(1.f, 1.f, 1.f, 0.14f) : O);
			S.Pressed = Rounded(P, Radius, O);
			S.Disabled = Rounded(FLinearColor(1.f, 1.f, 1.f, 0.04f), Radius);
			S.NormalPadding = FMargin(0.f);
			S.PressedPadding = FMargin(0.f);
			return S;
		}
	}

	UFrontendButton* Button(UWidgetTree* T, UWidget* Content, EButtonKind Kind, TFunction<void()> OnTap, float Radius)
	{
		UFrontendButton* B = T->ConstructWidget<UFrontendButton>();
		B->SetStyle(StyleFor(Kind, Radius));
		B->SetClickMethod(EButtonClickMethod::MouseUp);
		B->SetTouchMethod(EButtonTouchMethod::PreciseTap); // a scroll drag that starts on a card must not tap it
		B->SetRenderTransformPivot(FVector2D(0.5f));
		B->OnTap = MoveTemp(OnTap);
		B->Bind();
		if (UButtonSlot* Slot = Content ? Cast<UButtonSlot>(B->SetContent(Content)) : nullptr)
		{
			// UButton centres its content by default; cards and nav rows lay themselves out edge to edge.
			Slot->SetHorizontalAlignment(HAlign_Fill);
			Slot->SetVerticalAlignment(VAlign_Fill);
			Slot->SetPadding(FMargin(0.f));
		}
		return B;
	}

	UFrontendButton* CTA(UWidgetTree* T, const FString& Label, EButtonKind Kind, TFunction<void()> OnTap, float Height)
	{
		// The primary action is a sheared gold blade, the sports-broadcast signature; its label is sheared back upright.
		const bool bPrimary = Kind == EButtonKind::Primary;
		UTextBlock* L = Text(T, Label.ToUpper(), Height >= 64.f ? 26 : 19, bPrimary ? GoldInk() : FrontendStyle::Ink(),
			bPrimary ? EWeight::Black : EWeight::Condensed, 60);
		UOverlay* Layers = Stack(T);
		if (bPrimary)
		{
			Add(Layers, Fade(T, Hex(0x7A4A00, 0.55f), true)); // gold deepens toward the bottom edge
			Add(Layers, Sized(T, Box(T, Flat(Hex(0xFFF1C2, 0.75f))), 0.f, 2.f), HAlign_Fill, VAlign_Top);
			UImage* Shine = T->ConstructWidget<UImage>();
			Shine->SetBrush(Art(TEXT("T_CRICKET26_Shine"), Hex(0xFFFFFF, 0.55f)));
			Shine->SetVisibility(ESlateVisibility::HitTestInvisible);
			Add(Layers, Sized(T, Shine, 70.f, 0.f), HAlign_Left, VAlign_Fill);
			Layers->SetClipping(EWidgetClipping::ClipToBounds);
			Animate(Shine, EMotion::Shine, FMath::FRand() * 2.f);
		}
		UBorder* Pad = Box(T, Flat(FLinearColor::Transparent), FMargin(S4 + (bPrimary ? S2 : 0.f), 0.f));
		Pad->SetHorizontalAlignment(HAlign_Center);
		Pad->SetVerticalAlignment(VAlign_Center);
		Pad->SetContent(L);
		Add(Layers, Pad);
		UFrontendButton* B = Button(T, Sized(T, Layers, 0.f, Height), Kind, MoveTemp(OnTap), bPrimary ? 0.f : RButton);
		if (bPrimary)
		{
			B->SetRenderShear(FVector2D(-16.f, 0.f));
			L->SetRenderShear(FVector2D(16.f, 0.f));
		}
		return B;
	}

	UFrontendButton* PendingCTA(UWidgetTree* T, EFeatureStatus Status, TFunction<void()> OnTap, float Height)
	{
		UHorizontalBox* Row = HBox(T);
		Add(Row, Dot(T, FrontendData::StatusTint(Status), 8.f), FMargin(0.f, 0.f, S1, 0.f), 0.f, VAlign_Center);
		Add(Row, Text(T, FrontendData::StatusLabel(Status), 15, InkDim(), EWeight::Condensed, 140), FMargin(0.f), 0.f, VAlign_Center);
		UBorder* Pad = Box(T, Flat(FLinearColor::Transparent), FMargin(S3, 0.f), Row);
		Pad->SetHorizontalAlignment(HAlign_Center);
		Pad->SetVerticalAlignment(VAlign_Center);
		return Button(T, Sized(T, Pad, 0.f, Height), EButtonKind::Ghost, MoveTemp(OnTap));
	}

	UWidget* Pill(UWidgetTree* T, const FString& S, const FLinearColor& Tint, bool bSolid)
	{
		FLinearColor Fill = Tint;
		Fill.A = bSolid ? 1.f : 0.14f;
		UTextBlock* L = Text(T, S.ToUpper(), 13, bSolid ? GoldInk() : Tint, EWeight::Condensed, 160);
		return Box(T, Rounded(Fill, RPill), FMargin(12.f, 5.f), L);
	}

	UWidget* Badge(UWidgetTree* T, EFeatureStatus Status)
	{
		return Pill(T, FrontendData::StatusLabel(Status), FrontendData::StatusTint(Status));
	}

	UWidget* Dot(UWidgetTree* T, const FLinearColor& Colour, float Size)
	{
		return Sized(T, Box(T, Rounded(Colour, RPill)), Size, Size);
	}

	UWidget* Meter(UWidgetTree* T, float Value01, const FLinearColor& Colour, float Height)
	{
		UHorizontalBox* Track = HBox(T);
		const float V = FMath::Clamp(Value01, 0.f, 1.f);
		if (V > 0.f) Add(Track, Box(T, Rounded(Colour, RPill)), FMargin(0.f), V);
		if (V < 1.f) Add(Track, Space(T, 0.f, 0.f), FMargin(0.f), 1.f - V);
		UBorder* Back = Box(T, Rounded(FLinearColor(1.f, 1.f, 1.f, 0.08f), RPill), FMargin(0.f), Track);
		return Sized(T, Back, 0.f, Height);
	}

	UWidget* SectionHeader(UWidgetTree* T, const FString& EyebrowText, const FString& Title, UWidget* Trailing)
	{
		UHorizontalBox* Row = HBox(T);
		UVerticalBox* Col = VBox(T);
		if (!EyebrowText.IsEmpty()) Add(Col, Eyebrow(T, EyebrowText), FMargin(0.f, 0.f, 0.f, 4.f));
		Add(Col, Text(T, Title, H3, Ink(), EWeight::Bold));
		Add(Row, Col, FMargin(0.f), 1.f, VAlign_Bottom);
		if (Trailing) Add(Row, Trailing, FMargin(S2, 0.f, 0.f, 0.f), 0.f, VAlign_Bottom);
		return Row;
	}

	UWidget* Segmented(UWidgetTree* T, const TArray<FString>& Options, int32 Selected, TFunction<void(int32)> OnChange)
	{
		UHorizontalBox* Row = HBox(T);
		struct FSeg { UFrontendButton* Button; UTextBlock* Label; };
		TSharedRef<TArray<FSeg>> Segs = MakeShared<TArray<FSeg>>();
		TSharedRef<TFunction<void(int32)>> Handler = MakeShared<TFunction<void(int32)>>(MoveTemp(OnChange));
		auto Restyle = [Segs](int32 Sel)
		{
			for (int32 I = 0; I < Segs->Num(); ++I)
			{
				const bool bOn = I == Sel;
				FButtonStyle S = StyleFor(bOn ? EButtonKind::Primary : EButtonKind::Quiet, RButton);
				(*Segs)[I].Button->SetStyle(S);
				(*Segs)[I].Label->SetColorAndOpacity(FSlateColor(bOn ? GoldInk() : InkDim()));
			}
		};
		for (int32 I = 0; I < Options.Num(); ++I)
		{
			UTextBlock* L = Text(T, Options[I].ToUpper(), 17, InkDim(), EWeight::Condensed, 80);
			UBorder* Pad = Box(T, Flat(FLinearColor::Transparent), FMargin(S2, 0.f));
			Pad->SetHorizontalAlignment(HAlign_Center);
			Pad->SetVerticalAlignment(VAlign_Center);
			Pad->SetContent(L);
			UFrontendButton* B = Button(T, Sized(T, Pad, 0.f, 48.f), EButtonKind::Quiet, [Restyle, Handler, I]()
			{
				Restyle(I);
				if (*Handler) (*Handler)(I);
			}, 10.f);
			Segs->Add({ B, L });
			Add(Row, B, FMargin(I == 0 ? 0.f : 4.f, 0.f, 0.f, 0.f), 1.f);
		}
		Restyle(Selected);
		return Box(T, Rounded(Hex(0x050913, 0.72f), RButton + 4.f, Line()), FMargin(4.f), Row);
	}

	UWidget* Toggle(UWidgetTree* T, bool bOn, TFunction<void(bool)> OnChange)
	{
		TSharedRef<bool> State = MakeShared<bool>(bOn);
		UBorder* Track = Box(T, Rounded(Card(), RPill), FMargin(4.f), Dot(T, Ink(), 24.f));
		auto Paint = [Track, State]()
		{
			Track->SetBrush(Rounded(*State ? Teal() : CardHi(), RPill));
			Track->SetHorizontalAlignment(*State ? HAlign_Right : HAlign_Left);
		};
		Paint();
		return Button(T, Sized(T, Track, 60.f, 32.f), EButtonKind::Quiet, [State, Paint, OnChange = MoveTemp(OnChange)]()
		{
			*State = !*State;
			Paint();
			if (OnChange) OnChange(*State);
		}, RPill);
	}

	UWidget* Slider(UWidgetTree* T, float Value01, TFunction<void(float)> OnChange)
	{
		UFrontendSlider* S = T->ConstructWidget<UFrontendSlider>();
		FSliderStyle Style;
		Style.NormalBarImage = Rounded(Hex(0x3A4C84), RPill);
		Style.HoveredBarImage = Style.NormalBarImage;
		Style.DisabledBarImage = Style.NormalBarImage;
		FSlateBrush Thumb = Rounded(GoldHi(), RPill);
		Thumb.ImageSize = FVector2D(30.f);
		Style.NormalThumbImage = Thumb;
		Style.HoveredThumbImage = Rounded(Ink(), RPill);
		Style.HoveredThumbImage.ImageSize = FVector2D(30.f);
		Style.DisabledThumbImage = Thumb;
		Style.BarThickness = 8.f;
		S->SetWidgetStyle(Style);
		S->SetSliderBarColor(FLinearColor::White);
		S->SetSliderHandleColor(FLinearColor::White);
		S->SetStepSize(0.05f);
		S->SetValue(FMath::Clamp(Value01, 0.f, 1.f));

		UTextBlock* Readout = Text(T, FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Value01 * 100.f)), 16, InkDim(), EWeight::Medium);
		S->OnChange = [Readout, OnChange = MoveTemp(OnChange)](float V)
		{
			Readout->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(V * 100.f))));
			if (OnChange) OnChange(V);
		};
		S->Bind();
		UHorizontalBox* Row = HBox(T);
		Add(Row, Sized(T, S, 0.f, 44.f), FMargin(0.f), 1.f, VAlign_Center);
		Add(Row, Sized(T, Readout, 64.f, 0.f), FMargin(S2, 0.f, 0.f, 0.f), 0.f, VAlign_Center);
		return Row;
	}

	UWidget* SettingRow(UWidgetTree* T, const FString& Title, const FString& Subtitle, UWidget* Control, bool bWideControl)
	{
		UHorizontalBox* Row = HBox(T);
		UVerticalBox* Col = VBox(T);
		Add(Col, Text(T, Title, 19, Ink(), EWeight::Medium));
		if (!Subtitle.IsEmpty()) Add(Col, Para(T, Subtitle, 15, InkFaint()), FMargin(0.f, 4.f, 0.f, 0.f));
		Add(Row, Col, FMargin(0.f, 0.f, S3, 0.f), 1.f, VAlign_Center);
		Add(Row, bWideControl ? Sized(T, Control, 520.f, 0.f) : Control, FMargin(0.f), 0.f, VAlign_Center);
		return Box(T, Flat(FLinearColor::Transparent), FMargin(0.f, S2), Row);
	}

	UWidget* FeatureCard(UWidgetTree* T, const FFrontendFeature& F, TFunction<void()> OnTap, float MinHeight)
	{
		const FLinearColor Tint = FrontendData::StatusTint(F.Status);
		UOverlay* Layers = Stack(T);
		Layers->SetClipping(EWidgetClipping::ClipToBounds);
		FLinearColor GlowTint = Tint;
		GlowTint.A = 0.10f;
		Add(Layers, Glow(T, GlowTint, 420.f), HAlign_Right, VAlign_Top, FMargin(0.f, -180.f, -160.f, 0.f));

		UVerticalBox* Col = VBox(T);
		UHorizontalBox* Top = HBox(T);
		Add(Top, Eyebrow(T, F.Eyebrow), FMargin(0.f), 1.f, VAlign_Center);
		Add(Top, Badge(T, F.Status), FMargin(0.f), 0.f, VAlign_Center);
		Add(Col, Top);
		Add(Col, Space(T, 0.f, 0.f), FMargin(0.f), 1.f);
		Add(Col, Text(T, F.Title, H2, Ink(), EWeight::Black), FMargin(0.f, S2, 0.f, 6.f));
		Add(Col, Para(T, F.Subtitle, 16));
		UHorizontalBox* Foot = HBox(T);
		Add(Foot, Text(T, F.Status == EFeatureStatus::Available ? TEXT("OPEN") : TEXT("SEE WHAT'S COMING"), 14, F.Status == EFeatureStatus::Available ? Gold() : InkDim(), EWeight::Condensed, 160), FMargin(0.f), 0.f, VAlign_Center);
		Add(Foot, Text(T, TEXT("  ›"), 20, F.Status == EFeatureStatus::Available ? Gold() : InkDim(), EWeight::Bold), FMargin(0.f), 0.f, VAlign_Center);
		Add(Col, Foot, FMargin(0.f, S2, 0.f, 0.f));
		Add(Layers, Col, HAlign_Fill, VAlign_Fill, FMargin(S3));

		USizeBox* Min = T->ConstructWidget<USizeBox>();
		Min->SetMinDesiredHeight(MinHeight);
		Min->SetContent(Layers);
		return Button(T, Min, EButtonKind::Card, MoveTemp(OnTap), RCard);
	}

	UWidget* PlayerTile(UWidgetTree* T, const FFranchisePlayerRow& P, const FLinearColor& TeamColour)
	{
		UOverlay* Layers = Stack(T);
		Layers->SetClipping(EWidgetClipping::ClipToBounds);
		FLinearColor GlowTint = TeamColour;
		GlowTint.A = 0.35f;
		Add(Layers, Glow(T, GlowTint, 360.f), HAlign_Left, VAlign_Top, FMargin(-140.f, -160.f, 0.f, 0.f));
		UVerticalBox* Col = VBox(T);
		UHorizontalBox* Top = HBox(T);
		Add(Top, Pill(T, P.Role, P.Role == TEXT("BOWL") ? Hex(0xFF8A4C) : Hex(0x6FA8FF)), FMargin(0.f), 0.f, VAlign_Center);
		Add(Top, Space(T, 0.f, 0.f), FMargin(0.f), 1.f);
		if (P.bInXI) Add(Top, Pill(T, TEXT("XI"), Teal()), FMargin(0.f), 0.f, VAlign_Center);
		Add(Col, Top);
		Add(Col, Text(T, FString::FromInt(P.Rating), 56, Ink(), EWeight::Black), FMargin(0.f, S3, 0.f, 0.f));
		Add(Col, Eyebrow(T, TEXT("OVERALL")), FMargin(0.f, 0.f, 0.f, S2));
		Add(Col, Text(T, P.Name, H3, Ink(), EWeight::Bold));
		Add(Col, Text(T, P.Detail, 15, InkDim()), FMargin(0.f, 4.f, 0.f, S2));
		Add(Col, Meter(T, P.Rating / 100.f, P.Rating >= 80 ? Gold() : Teal(), 4.f));
		Add(Layers, Col, HAlign_Fill, VAlign_Fill, FMargin(S3));
		return Box(T, Rounded(Card(), RCard, Line()), FMargin(0.f), Layers);
	}

	UWidget* OpenSlotTile(UWidgetTree* T, const FString& Title, const FString& Subtitle)
	{
		UVerticalBox* Col = VBox(T);
		Add(Col, Text(T, TEXT("+"), 44, InkFaint(), EWeight::Light), FMargin(0.f), 0.f, HAlign_Center);
		Add(Col, Text(T, Title.ToUpper(), 15, InkDim(), EWeight::Condensed, 160), FMargin(0.f, S1, 0.f, 4.f), 0.f, HAlign_Center);
		UTextBlock* Sub = Para(T, Subtitle, 14, InkFaint());
		Sub->SetJustification(ETextJustify::Center);
		Add(Col, Sub, FMargin(0.f), 0.f, HAlign_Center);
		UBorder* B = Box(T, Rounded(FLinearColor(1.f, 1.f, 1.f, 0.015f), RCard, FLinearColor(1.f, 1.f, 1.f, 0.10f)), FMargin(S3), Col);
		Align(B, HAlign_Fill, VAlign_Center);
		return B;
	}

	UWidget* EmptyState(UWidgetTree* T, const FString& Title, const FString& Body, EFeatureStatus Status)
	{
		UVerticalBox* Col = VBox(T);
		Add(Col, Badge(T, Status), FMargin(0.f, 0.f, 0.f, S2), 0.f, HAlign_Center);
		UTextBlock* Head = Text(T, Title, H3, Ink(), EWeight::Bold);
		Head->SetJustification(ETextJustify::Center);
		Add(Col, Head, FMargin(0.f, 0.f, 0.f, S1), 0.f, HAlign_Center);
		UTextBlock* Txt = Para(T, Body, 16, InkFaint());
		Txt->SetJustification(ETextJustify::Center);
		Add(Col, Sized(T, Txt, 420.f, 0.f), FMargin(0.f), 0.f, HAlign_Center);
		UBorder* B = Box(T, Rounded(FLinearColor(1.f, 1.f, 1.f, 0.02f), RCard, FLinearColor(1.f, 1.f, 1.f, 0.08f)), FMargin(S4), Col);
		Align(B, HAlign_Center, VAlign_Center);
		return B;
	}

	UWidget* Backdrop(UWidgetTree* T, const TCHAR* Name, const FLinearColor& Tint)
	{
		UImage* Image = T->ConstructWidget<UImage>();
		Image->SetBrush(Art(Name, Tint));
		Image->SetVisibility(ESlateVisibility::HitTestInvisible);
		UScaleBox* Crop = T->ConstructWidget<UScaleBox>();
		Crop->SetStretch(EStretch::ScaleToFill);
		Crop->SetContent(Image);
		Crop->SetVisibility(ESlateVisibility::HitTestInvisible);
		return Crop;
	}

	UWidget* Fit(UWidgetTree* T, UWidget* Content)
	{
		UScaleBox* Box = T->ConstructWidget<UScaleBox>();
		Box->SetStretch(EStretch::ScaleToFit);
		Box->SetStretchDirection(EStretchDirection::DownOnly);
		Box->SetContent(Content);
		return Box;
	}

	UWidget* Picture(UWidgetTree* T, const TCHAR* Name, float Height, const FLinearColor& Tint)
	{
		const FSlateBrush B = Art(Name, Tint);
		const float Aspect = B.ImageSize.Y > 0.f ? B.ImageSize.X / B.ImageSize.Y : 1.f;
		UImage* Image = T->ConstructWidget<UImage>();
		Image->SetBrush(B);
		Image->SetVisibility(ESlateVisibility::HitTestInvisible);
		return Sized(T, Image, Height * Aspect, Height);
	}

	UWidget* Logo(UWidgetTree* T, float Scale)
	{
		// The baked lockup (metallic gold 26 blade, chrome wordmark); type only if the artwork has not been imported.
		if (Art(TEXT("T_CRICKET26_Logo")).DrawAs != ESlateBrushDrawType::NoDrawType) return Picture(T, TEXT("T_CRICKET26_Logo"), 58.f * Scale);
		UHorizontalBox* Row = HBox(T);
		UTextBlock* Num = Text(T, TEXT("26"), FMath::RoundToInt(30 * Scale), GoldInk(), EWeight::Black);
		UBorder* Tile = Box(T, Flat(Gold()), FMargin(10.f * Scale, 0.f), Num);
		Align(Tile, HAlign_Center, VAlign_Center);
		Tile->SetRenderShear(FVector2D(-16.f, 0.f));
		Num->SetRenderShear(FVector2D(16.f, 0.f));
		Add(Row, Sized(T, Tile, 0.f, 44.f * Scale), FMargin(0.f, 0.f, 12.f * Scale, 0.f), 0.f, VAlign_Center);
		UVerticalBox* Word = VBox(T);
		Add(Word, Text(T, TEXT("CRICKET"), FMath::RoundToInt(32 * Scale), Ink(), EWeight::Black, 40));
		Add(Word, Text(T, TEXT("SUPER OVER EDITION"), FMath::RoundToInt(11 * Scale), Gold(), EWeight::Bold, 260), FMargin(2.f, -6.f * Scale, 0.f, 0.f));
		Add(Row, Word, FMargin(0.f), 0.f, VAlign_Center);
		return Row;
	}

	UWidget* NamePlate(UWidgetTree* T, const FString& Name, const FString& Caption, const FLinearColor& Colour, bool bRight)
	{
		UVerticalBox* Col = VBox(T);
		const EHorizontalAlignment H = bRight ? HAlign_Right : HAlign_Left;
		Add(Col, Text(T, Caption.ToUpper(), 15, InkDim(), EWeight::Bold, 220), FMargin(0.f, 0.f, 0.f, 2.f), 0.f, H);
		UTextBlock* Title = Text(T, Name.ToUpper(), 54, Ink(), EWeight::Black, 20);
		UBorder* Band = Box(T, Flat(Colour), FMargin(S3, 2.f), Title);
		Band->SetRenderShear(FVector2D(-16.f, 0.f));
		Title->SetRenderShear(FVector2D(16.f, 0.f));
		Add(Col, Band, FMargin(0.f), 0.f, H);
		return Col;
	}

	UWidget* Versus(UWidgetTree* T, float Height)
	{
		// Home batter left, away bowler right, each lit by its team colour and backed by a ghosted HOME / AWAY; between
		// them the six-ball ring with VS and both crests, the football-game match-up card in cricket dress.
		UOverlay* Stage = Stack(T);
		const int32 Ghost = FMath::RoundToInt(Height * 0.34f);
		Add(Stage, Text(T, TEXT("HOME"), Ghost, Hex(0x5A8CFF, 0.20f), EWeight::Black), HAlign_Left, VAlign_Top, FMargin(0.f, -Height * 0.04f, 0.f, 0.f));
		Add(Stage, Text(T, TEXT("AWAY"), Ghost, Hex(0xFF5A6E, 0.20f), EWeight::Black), HAlign_Right, VAlign_Top, FMargin(0.f, -Height * 0.04f, 0.f, 0.f));
		Add(Stage, Glow(T, Hex(0x2F6BFF, 0.55f), Height * 1.3f), HAlign_Left, VAlign_Center, FMargin(-Height * 0.2f, 0.f, 0.f, 0.f));
		Add(Stage, Glow(T, Hex(0xE0344B, 0.50f), Height * 1.3f), HAlign_Right, VAlign_Center, FMargin(0.f, 0.f, -Height * 0.2f, 0.f));
		Add(Stage, Picture(T, TEXT("T_CRICKET26_Cutout_Batter"), Height), HAlign_Left, VAlign_Bottom, FMargin(Height * 0.05f, 0.f, 0.f, 0.f));
		Add(Stage, Picture(T, TEXT("T_CRICKET26_Cutout_Bowler"), Height * 0.98f), HAlign_Right, VAlign_Bottom, FMargin(0.f, 0.f, Height * 0.05f, 0.f));

		const float Ring = Height * 0.46f;
		UOverlay* Centre = Stack(T);
		Add(Centre, Glow(T, Hex(0xF2B632, 0.40f), Ring * 1.9f), HAlign_Center, VAlign_Center);
		Add(Centre, Picture(T, TEXT("T_CRICKET26_Ring"), Ring), HAlign_Center, VAlign_Center);
		UTextBlock* Vs = Text(T, TEXT("VS"), FMath::RoundToInt(Ring * 0.34f), GoldHi(), EWeight::Black);
		Vs->SetShadowOffset(FVector2D(0.f, 5.f));
		Vs->SetShadowColorAndOpacity(Hex(0x000000, 0.6f));
		Add(Centre, Vs, HAlign_Center, VAlign_Center);
		UHorizontalBox* Crests = HBox(T);
		Add(Crests, Picture(T, TEXT("T_CRICKET26_Crest_Home"), Ring * 0.5f), FMargin(0.f, 0.f, Ring * 1.05f, 0.f), 0.f, VAlign_Center);
		Add(Crests, Picture(T, TEXT("T_CRICKET26_Crest_Away"), Ring * 0.5f), FMargin(0.f), 0.f, VAlign_Center);
		Add(Centre, Crests, HAlign_Center, VAlign_Center);
		Add(Stage, Centre, HAlign_Center, VAlign_Center, FMargin(0.f, 0.f, 0.f, Height * 0.1f));
		return Sized(T, Stage, 0.f, Height);
	}

	UWidget* PlayerCard(UWidgetTree* T, const FFranchisePlayerRow& P, const TCHAR* Render, bool bElite, float Height)
	{
		// Laid out on the 600 x 860 frame texture and scaled by U so every card size keeps its proportions.
		const float U = Height / 860.f;
		const FLinearColor InkOn = bElite ? GoldHi() : Hex(0x2A1B02);
		const FLinearColor Sub = bElite ? Ink() : Hex(0x3D2A08);
		UOverlay* Card = Stack(T);
		UImage* Frame = T->ConstructWidget<UImage>();
		Frame->SetBrush(Art(bElite ? TEXT("T_CRICKET26_Card_Elite") : TEXT("T_CRICKET26_Card_Gold")));
		Add(Card, Frame);
		UOverlay* Figure = Stack(T);
		Figure->SetClipping(EWidgetClipping::ClipToBounds);
		if (bElite) Add(Figure, Glow(T, Hex(0x2F6BFF, 0.5f), 360.f * U), HAlign_Right, VAlign_Top, FMargin(0.f, 30.f * U, 20.f * U, 0.f));
		// Head and shoulders, the way trading cards frame a player: the render runs past the window and is clipped.
		Add(Figure, Picture(T, Render, 900.f * U), HAlign_Center, VAlign_Top, FMargin(170.f * U, 10.f * U, 0.f, 0.f));
		Add(Card, Sized(T, Figure, 0.f, 540.f * U), HAlign_Fill, VAlign_Top, FMargin(40.f * U, 40.f * U, 40.f * U, 0.f));
		// Blend the cut edge of the render into the name panel, in the frame's own colour at that height.
		Add(Card, Sized(T, Fade(T, bElite ? Hex(0x0D152B) : Hex(0xC59C46), true), 0.f, 150.f * U), HAlign_Fill, VAlign_Top,
			FMargin(40.f * U, 432.f * U, 40.f * U, 0.f));

		UVerticalBox* Badge = VBox(T);
		Add(Badge, Text(T, FString::FromInt(P.Rating), FMath::RoundToInt(92.f * U), InkOn, EWeight::Black), FMargin(0.f), 0.f, HAlign_Center);
		Add(Badge, Text(T, P.Role, FMath::RoundToInt(30.f * U), InkOn, EWeight::Condensed, 120), FMargin(0.f, -14.f * U, 0.f, 8.f * U), 0.f, HAlign_Center);
		Add(Badge, Sized(T, Box(T, Flat(InkOn * FLinearColor(1.f, 1.f, 1.f, 0.5f))), 56.f * U, 2.f * U), FMargin(0.f, 0.f, 0.f, 10.f * U), 0.f, HAlign_Center);
		Add(Badge, Icon(T, Glyph::Cricket, FMath::RoundToInt(44.f * U), InkOn), FMargin(0.f), 0.f, HAlign_Center);
		Add(Card, Badge, HAlign_Left, VAlign_Top, FMargin(62.f * U, 70.f * U, 0.f, 0.f));

		UVerticalBox* Plate = VBox(T);
		UTextBlock* Name = Text(T, P.Name.ToUpper(), FMath::RoundToInt(54.f * U), InkOn, EWeight::Black, 20);
		Add(Plate, Name, FMargin(0.f), 0.f, HAlign_Center);
		Add(Plate, Sized(T, Box(T, Flat(InkOn * FLinearColor(1.f, 1.f, 1.f, 0.35f))), 400.f * U, 2.f * U), FMargin(0.f, 6.f * U), 0.f, HAlign_Center);
		Add(Plate, Text(T, P.Detail.ToUpper(), FMath::RoundToInt(24.f * U), Sub, EWeight::Bold, 140), FMargin(0.f), 0.f, HAlign_Center);
		Add(Card, Plate, HAlign_Center, VAlign_Top, FMargin(0.f, 560.f * U, 0.f, 0.f));
		return Sized(T, Card, 600.f * U, Height);
	}

	namespace
	{
		struct FMotion
		{
			TWeakObjectPtr<UWidget> Widget;
			EMotion Motion;
			float Param;
		};
		TArray<FMotion>& Motions()
		{
			static TArray<FMotion> List;
			return List;
		}
	}

	void Animate(UWidget* W, EMotion Motion, float Param)
	{
		if (!W) return;
		if (Motion == EMotion::Enter) W->SetRenderOpacity(0.f);
		Motions().Add({ W, Motion, Param });
	}

	void TickMotion(float Clock, float SincePageOpened)
	{
		Motions().RemoveAllSwap([](const FMotion& M) { return !M.Widget.IsValid(); });
		for (const FMotion& M : Motions())
		{
			UWidget* W = M.Widget.Get();
			switch (M.Motion)
			{
			case EMotion::Shine:
			{
				// One pass every 3.5 s: slide from off the left edge to off the right edge of the clipped parent.
				const UWidget* Parent = W->GetParent();
				const float Width = Parent ? Parent->GetCachedGeometry().GetLocalSize().X : 400.f;
				const float T = FMath::Fmod(Clock + M.Param, 3.5f) / 0.9f;
				W->SetRenderTranslation(FVector2D(T < 1.f ? FMath::Lerp(-120.f, Width + 40.f, T) : -200.f, 0.f));
				W->SetRenderShear(FVector2D(-20.f, 0.f));
				break;
			}
			case EMotion::Pulse:
				W->SetRenderOpacity(0.6f + 0.4f * FMath::Sin(Clock * 2.2f + M.Param));
				break;
			case EMotion::Enter:
			{
				const float T = FMath::Clamp((SincePageOpened - M.Param) / 0.42f, 0.f, 1.f);
				const float E = 1.f - FMath::Cube(1.f - T);
				W->SetRenderOpacity(E);
				W->SetRenderTranslation(FVector2D(60.f * (1.f - E), 0.f));
				break;
			}
			}
		}
	}
}
