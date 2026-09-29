// Headless stand-in: the clip key the voice files are named by (same rule as CricketCommentary::ClipKey).
#pragma once
#include "CoreMinimal.h"

namespace CricketCommentary
{
	inline FString ClipKey(const FString& Text)
	{
		FString Key;
		bool bGap = false;
		for (const char Ch : Text.S)
		{
			if (Ch == '\'') continue;
			if (isalnum(uint8(Ch)) && uint8(Ch) < 128)
			{
				if (bGap && !Key.IsEmpty()) Key += '_';
				Key += char(tolower(uint8(Ch)));
				bGap = false;
			}
			else bGap = true;
		}
		return Key;
	}
}
