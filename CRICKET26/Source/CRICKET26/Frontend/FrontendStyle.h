// Design tokens for the CRICKET 26 frontend: one palette, one type scale, one spacing rhythm, and the
// brushes every screen is drawn with. Sizes are for the 1920x1080 DPI reference (landscape phone).
// Kept cheap for mobile: flat brushes and one small fade texture, no materials.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateBrush.h"

struct FCompositeFont;

class UTexture2D;

namespace FrontendStyle
{
	FLinearColor Hex(uint32 RGB, float Alpha = 1.f);

	// Surfaces, darkest to lightest: floodlit night navy.
	inline FLinearColor Bg() { return Hex(0x050913); }
	inline FLinearColor Surface() { return Hex(0x0B1222); }
	inline FLinearColor Card() { return Hex(0x131D33); }
	inline FLinearColor CardHi() { return Hex(0x1F2C4A); }
	inline FLinearColor Line() { return Hex(0xA9C2FF, 0.16f); }
	inline FLinearColor Scrim() { return Hex(0x02040A, 0.86f); }
	inline FLinearColor Glass() { return Hex(0x0B1426, 0.78f); } // panels laid over artwork

	// Accents. Gold is the one primary action per screen; cyan marks live/selected; home blue and away red are the teams.
	inline FLinearColor Gold() { return Hex(0xF2B632); }
	inline FLinearColor GoldHi() { return Hex(0xFFD56B); }
	inline FLinearColor GoldLo() { return Hex(0xC98A12); }
	inline FLinearColor GoldInk() { return Hex(0x1A1204); }
	inline FLinearColor Teal() { return Hex(0x3FD8FF); }
	inline FLinearColor Blue() { return Hex(0x2F6BFF); }
	inline FLinearColor Red() { return Hex(0xE0344B); }
	inline FLinearColor Danger() { return Hex(0xF0505A); }

	// Text.
	inline FLinearColor Ink() { return Hex(0xFFFFFF); }
	inline FLinearColor InkDim() { return Hex(0xB9C4DC); }
	inline FLinearColor InkFaint() { return Hex(0x7382A3); }

	// Type scale.
	constexpr int32 Display = 72, H1 = 44, H2 = 30, H3 = 22, Body = 18, Label = 15, Caption = 14;

	// 8-point spacing and radii.
	constexpr float S1 = 8.f, S2 = 16.f, S3 = 24.f, S4 = 32.f, S5 = 48.f;
constexpr float RCard = 12.f, RButton = 6.f, RPill = 999.f;

	enum class EWeight : uint8 { Light, Regular, Medium, Bold, Condensed, Black };
	FSlateFontInfo Font(int32 Size, EWeight Weight = EWeight::Regular, int32 LetterSpacing = 0);
	// Barlow Condensed has no rupee sign (every auction figure carries one); this maps U+20B9 to the engine's
	// Droid Sans Fallback, which has it, so it never draws as a missing-glyph box.
	void AddRupee(FCompositeFont& Font);
	// Google Material Icons (Apache 2.0); glyphs are addressed by codepoint, see the Glyph namespace.
	FSlateFontInfo IconFont(int32 Size);
	namespace Glyph
	{
		constexpr TCHAR Home = 0xe88a, Trophy = 0xea23, Cricket = 0xea27, Live = 0xe639, Store = 0xea12, Settings = 0xe8b8,
			Pause = 0xe034, Play = 0xe037, Back = 0xe5cb, Next = 0xe5cc, Lock = 0xe897, Groups = 0xf233, Stadium = 0xeb90,
			Star = 0xe838, Volume = 0xe050, Replay = 0xe042, Exit = 0xe9ba, Shield = 0xe9e0, Bolt = 0xea0b;
	}

	// A filled rounded box; Outline draws a hairline border of that colour when its alpha is non-zero.
	FSlateBrush Rounded(const FLinearColor& Fill, float Radius, const FLinearColor& Outline = FLinearColor::Transparent, float OutlineWidth = 1.f);
	FSlateBrush Flat(const FLinearColor& Fill);

	UTexture2D* FadeTexture(bool bVertical);
	UTexture2D* GlowTexture(); // soft radial falloff, white with alpha
	// Image brush drawn from one of the textures in /Game/UI (loaded on demand, cooked with that folder).
	FSlateBrush Art(const TCHAR* Name, const FLinearColor& Tint = FLinearColor::White);
	FSlateBrush TextureBrush(UTexture2D* Texture, const FLinearColor& Tint);
}
