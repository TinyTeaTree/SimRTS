#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "SimRTSDebugHUD.generated.h"

UCLASS()
class SIMRTS_API ASimRTSDebugHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
};
