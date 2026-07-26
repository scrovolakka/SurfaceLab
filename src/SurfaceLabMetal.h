#pragma once

#include "AEConfig.h"
#include "AE_Effect.h"

// Initializes a Metal device-local pipeline and runs a one-thread dispatch
// probe. GPU frame rendering remains opt-in at Smart PreRender time, so the
// existing CPU SmartFX path stays authoritative until the raster port lands.
PF_Err SetupMetalDevice(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_GPUDeviceSetupExtra* extra);

PF_Err SetdownMetalDevice(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_GPUDeviceSetdownExtra* extra);

bool IsMetalDeviceReady(const PF_InData* in_data);

bool MetalDiagnosticCopyEnabled(
    const PF_PreRenderExtra* extra);

PF_Err RenderMetalDiagnosticCopy(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_SmartRenderExtra* extra);
