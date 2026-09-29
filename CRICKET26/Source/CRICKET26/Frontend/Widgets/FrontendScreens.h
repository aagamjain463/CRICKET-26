// One builder per frontend page. Pages are composed only from FrontendUI components and talk to the
// shell through UFrontendRoot (navigation, sheets, starting the match).

#pragma once

#include "CoreMinimal.h"
#include "FrontendTypes.h"

class UFrontendRoot;
class UWidget;

namespace FrontendScreens
{
	UWidget* Build(UFrontendRoot* Root, EFrontendTab Tab);
}
