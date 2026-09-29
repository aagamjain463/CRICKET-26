// CRICKET 26 frontend shell: navigation vocabulary and UI-only data models.
// Gameplay/simulation code must never depend on this; the frontend reads game data, never writes it.

#pragma once

#include "CoreMinimal.h"
#include "FrontendTypes.generated.h"

UENUM(BlueprintType)
enum class EFrontendTab : uint8
{
	Home,
	Play,
	Auction,
	Franchise,
	Scouts,
	Store,
	Settings,
	MatchSetup,
	MatchFormat,
	IPLSeason, // the IPL tournament hub (season staged from the auction); appended, values kept
	Count UMETA(Hidden)
};

UENUM(BlueprintType)
enum class EFeatureStatus : uint8
{
	Available,
	InDevelopment,
	ComingSoon,
	Locked
};

USTRUCT(BlueprintType)
struct FFrontendFeature
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Eyebrow;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Title;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Subtitle;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FString> Bullets; // what the feature will do; shown in its detail sheet
	UPROPERTY(EditAnywhere, BlueprintReadWrite) EFeatureStatus Status = EFeatureStatus::Available;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) EFrontendTab Target = EFrontendTab::Home;
};

USTRUCT(BlueprintType)
struct FStoreOffer
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Title;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Detail;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Category;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bFeatured = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) EFeatureStatus Status = EFeatureStatus::ComingSoon;
};

USTRUCT(BlueprintType)
struct FScoutProspect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Name;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Role;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Region;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Rating = 0;    // 0..100
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Potential = 0; // 0..100
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bRevealed = true; // false: report not yet filed, numbers hidden
};

USTRUCT(BlueprintType)
struct FScoutRegion
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Name;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Focus;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) EFeatureStatus Status = EFeatureStatus::ComingSoon;
};

USTRUCT(BlueprintType)
struct FFranchisePlayerRow
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Name;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Role;   // BAT / BOWL / AR
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Detail; // hand, pace or spin type
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Rating = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bInXI = false;
};
