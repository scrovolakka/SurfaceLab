#pragma once

#include "SurfaceLabInternal.h"

PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data);

PF_Err UserChangedParam(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_ParamDef* params[],
    const PF_UserChangedParamExtra* extra);

PF_Err UpdateParameterUi(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_ParamDef* params[]);

PF_Err HandleSurfaceGizmoEvent(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_ParamDef* params[],
    PF_EventExtra* event_extra);
