// Shared by the auction's test files (one header, so a unity build never sees these twice).

#pragma once

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "AuctionEngine.h"

namespace AuctionTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** Ticks until Stop says so; false if it never does. */
	inline bool RunUntil(FAuction& A, TFunctionRef<bool()> Stop, float Step = 0.1f, int32 MaxSteps = 4000000)
	{
		for (int32 I = 0; I < MaxSteps; ++I)
		{
			if (Stop()) return true;
			A.Tick(Step);
		}
		return false;
	}
}
