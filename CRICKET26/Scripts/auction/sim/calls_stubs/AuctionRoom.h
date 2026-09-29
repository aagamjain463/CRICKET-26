// Headless stand-in for the room's layout (AuctionCalls reads where each table sits). Mirrors
// AuctionRoomLayout::TableFront in Source/CRICKET26/Auction/AuctionRoom.cpp; keep the two in step.
#pragma once
#include "CoreMinimal.h"

struct FVector { float X = 0.f, Y = 0.f, Z = 0.f; };

namespace AuctionRoomLayout
{
	inline FVector TableFront(int32 Team)
	{
		static const FVector Fronts[] = {
			{ 8.3f, -4.6f, 0.f }, { 8.3f, 4.6f, 0.f }, { 8.3f, 8.2f, 0.f },
			{ 8.3f, -8.2f, 0.f }, { 13.5f, -10.f, 0.45f }, { 13.5f, 6.4f, 0.45f },
			{ 8.3f, -11.8f, 0.f }, { 13.5f, 10.f, 0.45f },
			{ 13.5f, -6.4f, 0.45f }, { 8.3f, 11.8f, 0.f }
		};
		return Fronts[Team];
	}
}
