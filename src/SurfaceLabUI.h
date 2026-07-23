#pragma once

// AE-coupled parameter UI and gizmo for SurfaceLab.
//
// The Capture* bridges read the current parameter values into a
// SurfaceData; they are shared with ResolveSceneForFrame (the scene glue
// in SurfaceLab.cpp). ParamsSetup / UserChangedParamV15 /
// UpdateParameterUi / HandleSurfaceGizmoEvent are the UI entry points
// dispatched from EffectMain. The Load* bridges, animation-bank helpers,
// and all gizmo projection/drag/draw code stay private to
// SurfaceLabUI.cpp.

#include "SurfaceLabInternal.h"

void CaptureLegacySurface(PF_ParamDef* params[], SurfaceData& surface);
void CaptureScale(PF_ParamDef* params[], SurfaceData& surface);
void CaptureRotationOrigin(PF_ParamDef* params[], SurfaceData& surface);
void CaptureMaterial(PF_ParamDef* params[], SurfaceData& surface);
void CaptureThickness(PF_ParamDef* params[], SurfaceData& surface);
void CaptureDeform(PF_ParamDef* params[], SurfaceData& surface);
void CaptureCornerCurl(PF_ParamDef* params[], SurfaceData& surface);
void CaptureEdgeTwist(PF_ParamDef* params[], SurfaceData& surface);
void ApplySizeAndPositionUi(PF_ParamDef* params[], SurfaceData& surface);

PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data);

PF_Err UserChangedParamV15(
    PF_InData* in_data,
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
