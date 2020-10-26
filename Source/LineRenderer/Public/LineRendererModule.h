// Copyright Peter Leontev

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FLineRendererModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
