#pragma once

// AE-coupled rendering core for SurfaceLab.
//
// Everything needed to rasterize the surface into a PF_LayerDef: vertex
// projection, camera construction, surface evaluation, and the frame
// entry points (FrameSetup / Render). The custom-UI gizmo shares the
// projection and evaluation helpers declared here; the pixel-templated
// rasterizers stay private to SurfaceLabRender.cpp.

#include "SurfaceLabInternal.h"

struct NullPointOverrideState {
    std::array<
        std::array<bool, kMaximumLatticePoints>,
        kMaximumSurfaces> controlled{};
    std::size_t count{};

    bool IsControlled(
        std::uint32_t surface,
        std::size_t point) const {
        return surface < controlled.size() &&
               point < controlled[surface].size() &&
               controlled[surface][point];
    }
};

// Evaluated per-frame transform state for one v1 surface.
struct SurfaceEvaluationState {
    LatticeData lattice{};
    SurfaceCoordinateTransform coordinate_transform{};
    Affine3D root_pre_scene_transform{};
    bool root_transform_enabled{};
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
    SurfaceRollParams roll{};
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

// Builds the same After Effects view snapshot for rendering and custom UI.
// AEGP_GetEffectCameraMatrix supplies both an active camera layer and AE's
// implicit default camera, so SurfaceLab never owns a separate camera.
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

// Resolves marker-identified 3D Nulls at the current comp time and replaces
// only their corresponding lattice points. Null layer names and layer order
// are deliberately ignored. The returned map is also used by the gizmo to
// make externally controlled points read-only.
NullPointOverrideState ResolveNullPointOverrides(
    PF_InData* in_data,
    SceneData& scene,
    const CameraState& camera,
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
