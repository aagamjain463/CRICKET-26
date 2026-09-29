#include "FrontendStyle.h"
#include "Engine/Texture2D.h"
#include "Fonts/CompositeFont.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"

namespace FrontendStyle
{
	FLinearColor Hex(uint32 RGB, float Alpha)
	{
		FLinearColor C = FLinearColor::FromSRGBColor(FColor((RGB >> 16) & 0xFF, (RGB >> 8) & 0xFF, RGB & 0xFF));
		C.A = Alpha;
		return C;
	}

	void AddRupee(FCompositeFont& Font)
	{
		FCompositeSubFont& Sub = Font.SubTypefaces.AddDefaulted_GetRef();
		Sub.CharacterRanges.Add(FInt32Range(FInt32Range::BoundsType::Inclusive(0x20B9), FInt32Range::BoundsType::Inclusive(0x20B9)));
		Sub.Typeface.AppendFont(TEXT("Rupee"), FPaths::EngineContentDir() / TEXT("Slate/Fonts/DroidSansFallback.ttf"), EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
	}

	FSlateFontInfo Font(int32 Size, EWeight Weight, int32 LetterSpacing)
	{
		// Barlow Condensed (SIL OFL, Content/UI/Fonts, staged as loose files) is the broadcast face; each weight is
		// its own typeface so the black italic headline and the medium body share one family. Roboto is the fallback.
		static const TCHAR* Files[] = { TEXT("Medium"), TEXT("Medium"), TEXT("SemiBold"), TEXT("Bold"), TEXT("ExtraBold"), TEXT("BlackItalic") };
		static TSharedPtr<const FCompositeFont> Family[UE_ARRAY_COUNT(Files)];
		static bool bLoaded = false;
		if (!bLoaded)
		{
			bLoaded = true;
			for (int32 I = 0; I < UE_ARRAY_COUNT(Files); ++I)
			{
				const FString Path = FPaths::ProjectContentDir() / TEXT("UI/Fonts/BarlowCondensed-") + Files[I] + TEXT(".ttf");
				if (!FPaths::FileExists(Path)) continue;
				TSharedRef<FStandaloneCompositeFont> Face = MakeShared<FStandaloneCompositeFont>(TEXT("Default"), Path, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
				AddRupee(*Face);
				Family[I] = Face;
			}
		}
		const uint8 W = uint8(Weight);
		FSlateFontInfo F;
		if (Family[W].IsValid())
		{
			// Barlow Condensed sets narrower and smaller than Roboto at the same size; 1.12 keeps existing layouts.
			F = FSlateFontInfo(Family[W], FMath::RoundToInt(Size * 1.12f), TEXT("Default"));
		}
		else
		{
			static const FName Faces[] = { TEXT("Light"), TEXT("Regular"), TEXT("Medium"), TEXT("Bold"), TEXT("BoldCondensed"), TEXT("Black") };
			F = FCoreStyle::GetDefaultFontStyle(Faces[W], Size);
		}
		F.LetterSpacing = LetterSpacing;
		return F;
	}

	FSlateFontInfo IconFont(int32 Size)
	{
		static TSharedPtr<const FCompositeFont> Icons;
		if (!Icons.IsValid())
		{
			const FString Path = FPaths::ProjectContentDir() / TEXT("UI/Fonts/MaterialIcons-Regular.ttf");
			if (FPaths::FileExists(Path))
				Icons = MakeShared<FStandaloneCompositeFont>(TEXT("Default"), Path, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
		}
		return Icons.IsValid() ? FSlateFontInfo(Icons, Size, TEXT("Default")) : FCoreStyle::GetDefaultFontStyle("Regular", Size);
	}

	FSlateBrush Rounded(const FLinearColor& Fill, float Radius, const FLinearColor& Outline, float OutlineWidth)
	{
		FSlateBrush B;
		B.DrawAs = ESlateBrushDrawType::RoundedBox;
		B.TintColor = Fill;
		B.OutlineSettings = FSlateBrushOutlineSettings(Radius, Outline, Outline.A > 0.f ? OutlineWidth : 0.f);
		// A fixed radius larger than the box makes the rounded-box shader collapse the shape; pills follow the height.
		if (Radius >= RPill) B.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
		return B;
	}

	FSlateBrush Flat(const FLinearColor& Fill)
	{
		FSlateBrush B;
		B.DrawAs = ESlateBrushDrawType::Box;
		B.TintColor = Fill;
		return B;
	}

	namespace
	{
		// Rooted because the cache is static and outlives every widget and world.
		UTexture2D* MakeAlphaTexture(int32 W, int32 H, TFunctionRef<float(float, float)> Alpha)
		{
			UTexture2D* T = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
			if (!T) return nullptr;
			T->SRGB = false;
			T->Filter = TF_Bilinear;
			T->AddressX = T->AddressY = TA_Clamp;
			FColor* Px = static_cast<FColor*>(T->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
			for (int32 Y = 0; Y < H; ++Y)
			{
				for (int32 X = 0; X < W; ++X)
				{
					const float A = FMath::Clamp(Alpha((X + 0.5f) / W, (Y + 0.5f) / H), 0.f, 1.f);
					Px[Y * W + X] = FColor(255, 255, 255, uint8(A * 255.f + 0.5f));
				}
			}
			T->GetPlatformData()->Mips[0].BulkData.Unlock();
			T->UpdateResource();
			T->AddToRoot();
			return T;
		}
	}

	UTexture2D* FadeTexture(bool bVertical)
	{
		static UTexture2D* H = MakeAlphaTexture(256, 2, [](float U, float) { return 1.f - FMath::SmoothStep(0.f, 1.f, U); });
		static UTexture2D* V = MakeAlphaTexture(2, 256, [](float, float Vv) { return FMath::SmoothStep(0.f, 1.f, Vv); });
		return bVertical ? V : H;
	}

	UTexture2D* GlowTexture()
	{
		static UTexture2D* G = MakeAlphaTexture(128, 128, [](float U, float V)
		{
			const float D = FMath::Min(1.f, 2.f * FMath::Sqrt(FMath::Square(U - 0.5f) + FMath::Square(V - 0.5f)));
			return FMath::Square(1.f - FMath::SmoothStep(0.f, 1.f, D));
		});
		return G;
	}

	FSlateBrush Art(const TCHAR* Name, const FLinearColor& Tint)
	{
		const FString Path = FString::Printf(TEXT("/Game/UI/%s.%s"), Name, Name);
		UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *Path);
		FSlateBrush B = TextureBrush(Texture, Tint);
		if (Texture)
		{
			B.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
#if WITH_EDITORONLY_DATA
			// An editor-hosted run may not have built the texture yet, and until it has GetSizeX reports the
			// square placeholder's size; every picture sized from that collapsed to a square. The source always
			// has the real proportions.
			if (Texture->Source.IsValid()) B.ImageSize = FVector2D(Texture->Source.GetSizeX(), Texture->Source.GetSizeY());
#endif
		}
		else B.DrawAs = ESlateBrushDrawType::NoDrawType; // a missing texture must not paint a white slab
		return B;
	}

	FSlateBrush TextureBrush(UTexture2D* Texture, const FLinearColor& Tint)
	{
		FSlateBrush B;
		B.DrawAs = ESlateBrushDrawType::Image;
		B.SetResourceObject(Texture);
		B.TintColor = Tint;
		B.ImageSize = FVector2D(1.f, 1.f);
		return B;
	}
}
