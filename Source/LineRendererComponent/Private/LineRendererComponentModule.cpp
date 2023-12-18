// Copyright 2023 Petr Leontev. All Rights Reserved.

#include "LineRendererComponentModule.h"

void FLineRendererComponentModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
}

void FLineRendererComponentModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}
	
IMPLEMENT_MODULE(FLineRendererComponentModule, LineRendererComponent)