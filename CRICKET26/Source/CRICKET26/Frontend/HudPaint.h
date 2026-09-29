// The immediate-mode Slate painter the painted HUDs share: boxes, circles, lines, text and pills, each primitive
// kind on its own layer so Slate's batching keeps the call order. Same painter as the match HUD's (MatchHUDWidget).

#pragma once

#include "CoreMinimal.h"
#include "FrontendStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"

namespace HudPaint
{
	using namespace FrontendStyle;

	/** Text Px tall in local units (Slate sizes fonts in points at 96 DPI). */
	inline FSlateFontInfo HudFont(float Px, EWeight W = EWeight::Bold, int32 Spacing = 0)
	{
		FSlateFontInfo Info = Font(1, W, Spacing);
		Info.Size = FMath::Max(1.f, Px * 0.75f);
		return Info;
	}


	/**
	 * Paints in call order: consecutive primitives of one kind share a layer, and a change of kind starts a new one
	 * (Slate draws a layer's boxes, lines and text as separate batches, so sharing layers across kinds would reorder
	 * them). Alpha fades whatever is drawn while it is set.
	 */
	struct FPaint
	{
		const FGeometry& G;
		FSlateWindowElementList& Out;
		int32 Layer;
		int32 Kind = -1;
		float Alpha = 1.f;

		int32 On(int32 K) { if (K != Kind) { ++Layer; Kind = K; } return Layer; }
		FPaintGeometry At(const FVector2D& Pos, const FVector2D& Size) const
		{
			return G.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(Pos)));
		}
		FLinearColor A(FLinearColor C) const { C.A *= Alpha; return C; }

		void Box(const FVector2D& Pos, const FVector2D& Size, const FLinearColor& Fill, float Radius = 0.f,
			const FLinearColor& Outline = FLinearColor::Transparent, float Width = 1.f)
		{
			const FSlateBrush B = Rounded(FLinearColor::White, Radius, A(Outline), Width);
			FSlateDrawElement::MakeBox(Out, On(0), At(Pos, Size), &B, ESlateDrawEffect::None, A(Fill));
		}
		void Box(const FBox2D& R, const FLinearColor& Fill, float Radius = 0.f, const FLinearColor& Outline = FLinearColor::Transparent, float Width = 1.f)
		{
			Box(R.Min, R.GetSize(), Fill, Radius, Outline, Width);
		}
		void Circle(const FVector2D& C, float R, const FLinearColor& Fill, const FLinearColor& Outline = FLinearColor::Transparent, float Width = 1.f)
		{
			Box(C - FVector2D(R), FVector2D(2.f * R), Fill, R, Outline, Width);
		}
		void Lines(TArray<FVector2f> Points, const FLinearColor& C, float Thick)
		{
			FSlateDrawElement::MakeLines(Out, On(1), G.ToPaintGeometry(), MoveTemp(Points), ESlateDrawEffect::None, A(C), true, Thick);
		}
		void Line(const FVector2D& P0, const FVector2D& P1, const FLinearColor& C, float Thick)
		{
			Lines({ FVector2f(P0), FVector2f(P1) }, C, Thick);
		}
		static FVector2D Measure(const FString& S, const FSlateFontInfo& Font)
		{
			return FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(S, Font);
		}
		/** Text with its middle at P.Y, placed at P.X by Align (0 left, 0.5 centre, 1 right). Returns its width. */
		float Text(const FString& S, const FVector2D& P, const FSlateFontInfo& Font, const FLinearColor& C, float Align = 0.f)
		{
			const FVector2D Sz = Measure(S, Font);
			FSlateDrawElement::MakeText(Out, On(2), At(FVector2D(P.X - Align * Sz.X, P.Y - 0.5f * Sz.Y), Sz), S, Font, ESlateDrawEffect::None, A(C));
			return Sz.X;
		}
		/** A pill sized to its text, centred on C. Returns its rectangle. */
		FBox2D Pill(const FString& S, const FVector2D& C, const FSlateFontInfo& Font, const FLinearColor& Fill, const FLinearColor& Ink,
			float PadX, float Height, float Radius, const FLinearColor& Outline = FLinearColor::Transparent)
		{
			const float W = Measure(S, Font).X + 2.f * PadX;
			const FBox2D R(C - FVector2D(0.5f * W, 0.5f * Height), C + FVector2D(0.5f * W, 0.5f * Height));
			Box(R, Fill, Radius, Outline, 1.f);
			Text(S, C, Font, Ink, 0.5f);
			return R;
		}
	};
}
