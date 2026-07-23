#pragma once

// AE-coupled rendering core for SurfaceLab.
//
// Everything needed to rasterize the surface into a PF_LayerDef: vertex
// projection, camera construction, surface evaluation, and the frame
// entry points (FrameSetup / Render). The custom-UI gizmo shares the
// projection and evaluation helpers declared here; the pixel-templated
// rasterizers stay private to SurfaceLabRender.cpp.

#include "SurfaceLabInternal.h"

#include <array>

// Evaluated per-frame transform state for one surface: scaled control
// points, pivot, rotation origin, and deformation extents.
struct SurfaceEvaluationState {
    std::array<Point3, 16> control_points{};
    SurfaceCoordinateTransform coordinate_transform{};
    double pivot_x{};
    double pivot_y{};
    double pivot_z{};
    double rotation_origin_x{};
    double rotation_origin_y{};
    double rotation_origin_z{};
    double scale_x{1.0};
    double scale_y{1.0};
    double scale_z{1.0};
    double deform_extent_x{1.0};
    double deform_extent_y{1.0};
    double rotation_x{};
    double rotation_y{};
    double rotation_z{};
    double half_thickness{};
};

SurfaceEvaluationState BuildSurfaceEvaluationState(
    const SurfaceData& surface,
    const CameraState& camera,
    double render_scale_x,
    double render_scale_y,
    double render_scale_z);

Point3 EvaluateTransformedPoint(
    const SurfaceData& surface,
    const SurfaceEvaluationState& state,
    double u,
    double v);

Vertex ProjectVertex(
    const Point3& point,
    Point3 normal,
    double u,
    double v,
    const CameraState& camera);

CameraState BuildCameraState(
    PF_ParamDef* params[],
    double center_x,
    double center_y,
    double output_offset_x,
    double output_offset_y,
    double scale_x,
    double scale_y,
    double scale_z);

// Builds the same camera snapshot for rendering and custom UI. When the
// selected source is the active After Effects camera, this resolves its
// current transform and zoom; otherwise it returns the internal camera.
CameraState BuildResolvedCameraState(
    PF_InData* in_data,
    PF_ParamDef* params[],
    double center_x,
    double center_y,
    double output_offset_x,
    double output_offset_y,
    double scale_x,
    double scale_y,
    double scale_z);

PF_Err FrameSetup(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_ParamDef* params[]);

PF_Err Render(PF_InData* in_data, PF_ParamDef* params[], PF_LayerDef* output);

PF_Err SmartPreRender(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_PreRenderExtra* extra);

PF_Err SmartRender(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_SmartRenderExtra* extra);
