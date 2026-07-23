#include "SurfaceLab.h"
#include "AEGP_SuiteHandler.h"
#include "AEFX_SuiteHelper.h"
#include <adobesdk/DrawbotSuite.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <type_traits>
#include <vector>

#include "SurfaceLabRender.h"
#include "SurfaceLabUI.h"

void CaptureLegacySurface(PF_ParamDef* params[], SurfaceData& surface) {
    for (int index = 0; index < 16; ++index) {
        const PF_PointDef& point = params[kParamPoint00 + index]->u.td;
        surface.control_points[index].x = static_cast<float>(FIX_2_FLOAT(point.x_value));
        surface.control_points[index].y = static_cast<float>(FIX_2_FLOAT(point.y_value));
        surface.control_points[index].z = static_cast<float>(
            params[kParamDepth00 + index]->u.fs_d.value);
    }
    surface.rotation_x = static_cast<float>(
        FIX_2_FLOAT(params[kParamRotationX]->u.ad.value));
    surface.rotation_y = static_cast<float>(
        FIX_2_FLOAT(params[kParamRotationY]->u.ad.value));
    surface.rotation_z = static_cast<float>(
        FIX_2_FLOAT(params[kParamRotationZ]->u.ad.value));
    UpdateDerivedTransform(surface);
}

void CaptureScale(PF_ParamDef* params[], SurfaceData& surface) {
    surface.scale_x = static_cast<float>(params[kParamSurfaceScaleX]->u.fs_d.value);
    surface.scale_y = static_cast<float>(params[kParamSurfaceScaleY]->u.fs_d.value);
    surface.scale_z = static_cast<float>(params[kParamSurfaceScaleZ]->u.fs_d.value);
}

void CaptureRotationOrigin(PF_ParamDef* params[], SurfaceData& surface) {
    surface.rotation_origin_mode =
        static_cast<std::uint32_t>(std::clamp<A_long>(
            params[kParamSurfaceRotationOriginMode]->u.pd.value,
            static_cast<A_long>(kRotationOriginCenter),
            static_cast<A_long>(kRotationOriginCustom)));
    surface.rotation_origin_x = static_cast<float>(std::clamp(
        params[kParamSurfaceRotationOriginX]->u.fs_d.value,
        -1000.0,
        1000.0));
    surface.rotation_origin_y = static_cast<float>(std::clamp(
        params[kParamSurfaceRotationOriginY]->u.fs_d.value,
        -1000.0,
        1000.0));
}

namespace {
std::uint32_t LegacyTessellation(PF_ParamDef* params[]) {
    return static_cast<std::uint32_t>(std::clamp<A_long>(
        params[kParamTessellation]->u.sd.value,
        static_cast<A_long>(kMinimumDivisions),
        static_cast<A_long>(kMaximumDivisions)));
}

}  // namespace

void CaptureMaterial(PF_ParamDef* params[], SurfaceData& surface) {
    surface.source_slot = static_cast<std::uint32_t>(std::clamp<A_long>(
        params[kParamSurfaceSourceSlot]->u.pd.value - 1,
        0,
        static_cast<A_long>(kMaximumSurfaces - 1)));
    surface.back_source_slot = static_cast<std::uint32_t>(std::clamp<A_long>(
        params[kParamSurfaceBackSourceSlot]->u.pd.value - 1,
        0,
        static_cast<A_long>(kMaximumSurfaces)));
    surface.image_size_mode = static_cast<std::uint32_t>(std::clamp<A_long>(
        params[kParamSurfaceImageSize]->u.pd.value,
        static_cast<A_long>(kImageSizeStretch),
        static_cast<A_long>(kImageSizeFit)));
    surface.image_border_mode = static_cast<std::uint32_t>(std::clamp<A_long>(
        params[kParamSurfaceImageBorder]->u.pd.value,
        static_cast<A_long>(kImageBorderClamp),
        static_cast<A_long>(kImageBorderTransparent)));
    surface.opacity = static_cast<float>(std::clamp(
        params[kParamSurfaceOpacity]->u.fs_d.value,
        0.0,
        100.0));
    surface.diffuse = static_cast<float>(std::clamp(
        params[kParamSurfaceDiffuse]->u.fs_d.value,
        0.0,
        200.0));
    surface.specular = static_cast<float>(std::clamp(
        params[kParamSurfaceSpecular]->u.fs_d.value,
        0.0,
        200.0));
    surface.shininess = static_cast<float>(std::clamp(
        params[kParamSurfaceShininess]->u.fs_d.value,
        1.0,
        256.0));
}

void CaptureThickness(PF_ParamDef* params[], SurfaceData& surface) {
    surface.thickness = static_cast<float>(std::clamp(
        params[kParamSurfaceThickness]->u.fs_d.value,
        0.0,
        4000.0));
}

void CaptureDeform(PF_ParamDef* params[], SurfaceData& surface) {
    surface.bend_x = static_cast<float>(std::clamp(
        params[kParamSurfaceBendX]->u.fs_d.value,
        -720.0,
        720.0));
    surface.bend_y = static_cast<float>(std::clamp(
        params[kParamSurfaceBendY]->u.fs_d.value,
        -720.0,
        720.0));
    surface.roll_edge = static_cast<std::uint32_t>(std::clamp<A_long>(
        params[kParamSurfaceRollEdge]->u.pd.value,
        static_cast<A_long>(kRollEdgeRight),
        static_cast<A_long>(kRollEdgeTop)));
    surface.roll_angle = static_cast<float>(std::clamp(
        params[kParamSurfaceRollAngle]->u.fs_d.value,
        -1080.0,
        1080.0));
    surface.roll_length = static_cast<float>(std::clamp(
        params[kParamSurfaceRollLength]->u.fs_d.value,
        0.0,
        100.0));
}

namespace {
void LoadDeformParams(PF_ParamDef* params[], const SurfaceData& surface) {
    const float values[4] = {
        surface.bend_x,
        surface.bend_y,
        surface.roll_angle,
        surface.roll_length};
    const PF_ParamIndex indices[4] = {
        kParamSurfaceBendX,
        kParamSurfaceBendY,
        kParamSurfaceRollAngle,
        kParamSurfaceRollLength};
    for (int index = 0; index < 4; ++index) {
        PF_ParamDef* param = params[indices[index]];
        param->u.fs_d.value = values[index];
        param->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
    }
    PF_ParamDef* edge_param = params[kParamSurfaceRollEdge];
    edge_param->u.pd.value = static_cast<A_long>(surface.roll_edge);
    edge_param->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
}

}  // namespace

void CaptureCornerCurl(PF_ParamDef* params[], SurfaceData& surface) {
    surface.selected_corner = static_cast<std::uint32_t>(std::clamp<A_long>(
        params[kParamSurfaceCornerSelect]->u.pd.value,
        static_cast<A_long>(kCornerTopLeft),
        static_cast<A_long>(kCornerBottomLeft)));
    for (std::size_t index = 0; index < 4; ++index) {
        CornerCurlData& corner = surface.corner_curls[index];
        corner.amount = static_cast<float>(std::clamp(
            params[kCornerAmountParams[index]]->u.fs_d.value,
            -100.0,
            100.0));
        corner.radius = static_cast<float>(std::clamp(
            params[kCornerRadiusParams[index]]->u.fs_d.value,
            0.1,
            100.0));
        corner.direction = static_cast<float>(std::clamp(
            params[kCornerDirectionParams[index]]->u.fs_d.value,
            0.0,
            90.0));
        corner.length = static_cast<float>(std::clamp(
            params[kCornerLengthParams[index]]->u.fs_d.value,
            0.0,
            150.0));
    }
}

namespace {
void LoadCornerCurlValueParams(
    PF_ParamDef* params[],
    const SurfaceData& surface) {
    for (std::size_t index = 0; index < 4; ++index) {
        const CornerCurlData& corner = surface.corner_curls[index];
        PF_ParamDef* amount = params[kCornerAmountParams[index]];
        amount->u.fs_d.value = corner.amount;
        amount->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
        PF_ParamDef* radius = params[kCornerRadiusParams[index]];
        radius->u.fs_d.value = corner.radius;
        radius->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
        PF_ParamDef* direction = params[kCornerDirectionParams[index]];
        direction->u.fs_d.value = corner.direction;
        direction->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
        PF_ParamDef* length = params[kCornerLengthParams[index]];
        length->u.fs_d.value = corner.length;
        length->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
    }
}

void LoadCornerCurlParams(PF_ParamDef* params[], const SurfaceData& surface) {
    const std::uint32_t selected_corner = std::clamp<std::uint32_t>(
        surface.selected_corner,
        kCornerTopLeft,
        kCornerBottomLeft);
    PF_ParamDef* corner_select = params[kParamSurfaceCornerSelect];
    corner_select->u.pd.value = static_cast<A_long>(selected_corner);
    corner_select->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
    LoadCornerCurlValueParams(params, surface);
}

}  // namespace

void CaptureEdgeTwist(PF_ParamDef* params[], SurfaceData& surface) {
    surface.selected_twist_edge = static_cast<std::uint32_t>(std::clamp<A_long>(
        params[kParamSurfaceTwistEdge]->u.pd.value,
        static_cast<A_long>(kTwistEdgeLeft),
        static_cast<A_long>(kTwistEdgeBottom)));
    for (std::size_t index = 0; index < 4; ++index) {
        EdgeTwistData& edge = surface.edge_twists[index];
        edge.angle = static_cast<float>(std::clamp(
            params[kTwistAngleParams[index]]->u.fs_d.value,
            -180.0,
            180.0));
        edge.falloff = static_cast<float>(std::clamp(
            params[kTwistFalloffParams[index]]->u.fs_d.value,
            1.0,
            100.0));
    }
}

namespace {
void LoadEdgeTwistValueParams(
    PF_ParamDef* params[],
    const SurfaceData& surface) {
    for (std::size_t index = 0; index < 4; ++index) {
        const EdgeTwistData& edge = surface.edge_twists[index];
        PF_ParamDef* angle = params[kTwistAngleParams[index]];
        angle->u.fs_d.value = edge.angle;
        angle->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
        PF_ParamDef* falloff = params[kTwistFalloffParams[index]];
        falloff->u.fs_d.value = edge.falloff;
        falloff->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
    }
}

void LoadEdgeTwistParams(PF_ParamDef* params[], const SurfaceData& surface) {
    const std::uint32_t selected_edge = std::clamp<std::uint32_t>(
        surface.selected_twist_edge,
        kTwistEdgeLeft,
        kTwistEdgeBottom);
    PF_ParamDef* edge_select = params[kParamSurfaceTwistEdge];
    edge_select->u.pd.value = static_cast<A_long>(selected_edge);
    edge_select->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
    LoadEdgeTwistValueParams(params, surface);
}

}  // namespace

void ApplySizeAndPositionUi(PF_ParamDef* params[], SurfaceData& surface) {
    const float desired_size_x = static_cast<float>(std::max(
        0.001,
        params[kParamSurfaceSizeX]->u.fs_d.value));
    const float desired_size_y = static_cast<float>(std::max(
        0.001,
        params[kParamSurfaceSizeY]->u.fs_d.value));
    const float ratio_x = desired_size_x / std::max(0.001F, surface.size_x);
    const float ratio_y = desired_size_y / std::max(0.001F, surface.size_y);
    for (StoredPoint3& point : surface.control_points) {
        point.x = surface.position_x + (point.x - surface.position_x) * ratio_x;
        point.y = surface.position_y + (point.y - surface.position_y) * ratio_y;
    }
    UpdateDerivedTransform(surface);

    const PF_Point3DDef& desired = params[kParamSurfacePosition]->u.point3d_d;
    const float offset_x = static_cast<float>(desired.x_value) - surface.position_x;
    const float offset_y = static_cast<float>(desired.y_value) - surface.position_y;
    const float offset_z = static_cast<float>(desired.z_value) - surface.position_z;
    for (StoredPoint3& point : surface.control_points) {
        point.x += offset_x;
        point.y += offset_y;
        point.z += offset_z;
    }
    UpdateDerivedTransform(surface);
}

namespace {
void LoadMaterialParams(PF_ParamDef* params[], const SurfaceData& surface) {
    PF_ParamDef* source_slot_param = params[kParamSurfaceSourceSlot];
    source_slot_param->u.pd.value = static_cast<A_long>(surface.source_slot + 1);
    source_slot_param->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;

    PF_ParamDef* back_source_slot_param = params[kParamSurfaceBackSourceSlot];
    back_source_slot_param->u.pd.value =
        static_cast<A_long>(surface.back_source_slot + 1);
    back_source_slot_param->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;

    PF_ParamDef* image_size_param = params[kParamSurfaceImageSize];
    image_size_param->u.pd.value = static_cast<A_long>(surface.image_size_mode);
    image_size_param->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;

    PF_ParamDef* image_border_param = params[kParamSurfaceImageBorder];
    image_border_param->u.pd.value = static_cast<A_long>(surface.image_border_mode);
    image_border_param->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;

    PF_ParamDef* opacity_param = params[kParamSurfaceOpacity];
    opacity_param->u.fs_d.value = surface.opacity;
    opacity_param->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;

    PF_ParamDef* diffuse_param = params[kParamSurfaceDiffuse];
    diffuse_param->u.fs_d.value = surface.diffuse;
    diffuse_param->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;

    PF_ParamDef* specular_param = params[kParamSurfaceSpecular];
    specular_param->u.fs_d.value = surface.specular;
    specular_param->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;

    PF_ParamDef* shininess_param = params[kParamSurfaceShininess];
    shininess_param->u.fs_d.value = surface.shininess;
    shininess_param->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
}

void LoadSizeAndPositionParams(
    PF_ParamDef* params[],
    const SurfaceData& surface) {
    PF_ParamDef* size_x_param = params[kParamSurfaceSizeX];
    size_x_param->u.fs_d.value = surface.size_x;
    size_x_param->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
    PF_ParamDef* size_y_param = params[kParamSurfaceSizeY];
    size_y_param->u.fs_d.value = surface.size_y;
    size_y_param->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;

    PF_ParamDef* position_param = params[kParamSurfacePosition];
    position_param->u.point3d_d.x_value = surface.position_x;
    position_param->u.point3d_d.y_value = surface.position_y;
    position_param->u.point3d_d.z_value = surface.position_z;
    position_param->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
}

void LoadTransformParams(PF_ParamDef* params[], const SurfaceData& surface) {
    LoadSizeAndPositionParams(params, surface);

    PF_ParamDef* origin_mode = params[kParamSurfaceRotationOriginMode];
    origin_mode->u.pd.value = static_cast<A_long>(std::clamp<std::uint32_t>(
        surface.rotation_origin_mode,
        kRotationOriginCenter,
        kRotationOriginCustom));
    origin_mode->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
    PF_ParamDef* origin_x = params[kParamSurfaceRotationOriginX];
    origin_x->u.fs_d.value = surface.rotation_origin_x;
    origin_x->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
    PF_ParamDef* origin_y = params[kParamSurfaceRotationOriginY];
    origin_y->u.fs_d.value = surface.rotation_origin_y;
    origin_y->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;

    const float scales[3] = {surface.scale_x, surface.scale_y, surface.scale_z};
    for (int index = 0; index < 3; ++index) {
        PF_ParamDef* scale_param = params[kParamSurfaceScaleX + index];
        scale_param->u.fs_d.value = scales[index];
        scale_param->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
    }

    const std::uint32_t legacy_tessellation = LegacyTessellation(params);
    const std::uint32_t divisions[2] = {
        ResolveDivisions(surface.divisions_x, legacy_tessellation),
        ResolveDivisions(surface.divisions_y, legacy_tessellation)};
    for (int index = 0; index < 2; ++index) {
        PF_ParamDef* divisions_param = params[kParamSurfaceDivisionsX + index];
        divisions_param->u.sd.value = static_cast<A_long>(divisions[index]);
        divisions_param->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
    }

    LoadMaterialParams(params, surface);

    PF_ParamDef* thickness_param = params[kParamSurfaceThickness];
    thickness_param->u.fs_d.value = surface.thickness;
    thickness_param->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;

    LoadDeformParams(params, surface);
    LoadCornerCurlParams(params, surface);
    LoadEdgeTwistParams(params, surface);
}

void LoadLegacyGeometryParams(
    PF_ParamDef* params[],
    const SurfaceData& surface) {
    for (int index = 0; index < 16; ++index) {
        PF_ParamDef* point_param = params[kParamPoint00 + index];
        point_param->u.td.x_value = FLOAT2FIX(surface.control_points[index].x);
        point_param->u.td.y_value = FLOAT2FIX(surface.control_points[index].y);
        point_param->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;

        PF_ParamDef* depth_param = params[kParamDepth00 + index];
        depth_param->u.fs_d.value = surface.control_points[index].z;
        depth_param->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
    }
}

void LoadSurfaceIntoLegacyParams(PF_ParamDef* params[], const SurfaceData& surface) {
    LoadLegacyGeometryParams(params, surface);

    const float rotations[3] = {
        surface.rotation_x,
        surface.rotation_y,
        surface.rotation_z};
    for (int index = 0; index < 3; ++index) {
        PF_ParamDef* rotation_param = params[kParamRotationX + index];
        rotation_param->u.ad.value = FLOAT2FIX(rotations[index]);
        rotation_param->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
    }

    LoadTransformParams(params, surface);
}

void UpdateSurfaceUiParams(PF_ParamDef* params[], const SceneData& scene) {
    PF_ParamDef* count_param = params[kParamSurfaceCount];
    count_param->u.sd.value = static_cast<A_long>(scene.surface_count);
    count_param->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;

    PF_ParamDef* select_param = params[kParamSurfaceSelect];
    select_param->u.pd.value = static_cast<A_long>(scene.selected_surface + 1);
    select_param->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;

    const SurfaceData& selected = scene.surfaces[scene.selected_surface];
    PF_ParamDef* id_param = params[kParamControllerSurfaceId];
    id_param->u.sd.value = static_cast<A_long>(std::min<std::uint32_t>(
        selected.id,
        static_cast<std::uint32_t>(std::numeric_limits<A_long>::max())));
    id_param->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;

    PF_ParamDef* bank_param = params[kParamControllerAnimationBank];
    bank_param->u.sd.value = static_cast<A_long>(selected.animation_bank);
    bank_param->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
}

std::uint32_t FindSurfaceForAnimationBank(
    const SceneData& scene,
    std::uint32_t bank) {
    for (std::uint32_t index = 0; index < scene.surface_count; ++index) {
        if (scene.surfaces[index].animation_bank == bank) {
            return index;
        }
    }
    return scene.selected_surface;
}

std::uint32_t FindAvailableAnimationBank(const SceneData& scene) {
    std::array<bool, kMaximumSurfaces> used{};
    for (std::uint32_t index = 0; index < scene.surface_count; ++index) {
        used[scene.surfaces[index].animation_bank] = true;
    }
    for (std::uint32_t bank = 0; bank < kMaximumSurfaces; ++bank) {
        if (!used[bank]) {
            return bank;
        }
    }
    return kMaximumSurfaces - 1;
}

void LoadSurfaceIntoAnimationBank(
    PF_ParamDef* params[],
    const SurfaceData& surface) {
    auto view = BuildAnimationParameterView(params, surface.animation_bank);
    LoadSurfaceIntoLegacyParams(view.data(), surface);
}

void InitializeAdditionalAnimationBanks(
    PF_ParamDef* params[],
    SceneData& scene) {
    if (scene.reserved[kAnimationBanksInitializedIndex] != 0) {
        return;
    }
    for (std::uint32_t index = 0; index < scene.surface_count; ++index) {
        if (scene.surfaces[index].animation_bank != 0) {
            LoadSurfaceIntoAnimationBank(params, scene.surfaces[index]);
        }
    }
    scene.reserved[kAnimationBanksInitializedIndex] = 1;
}

void CaptureSurfaceAtCurrentFrame(
    PF_ParamDef* params[],
    SurfaceData& surface) {
    auto view = BuildAnimationParameterView(params, surface.animation_bank);
    PF_ParamDef** surface_params = view.data();
    CaptureLegacySurface(surface_params, surface);
    ApplySizeAndPositionUi(surface_params, surface);
    CaptureScale(surface_params, surface);
    CaptureRotationOrigin(surface_params, surface);
    surface.divisions_x = static_cast<std::uint32_t>(std::clamp<A_long>(
        surface_params[kParamSurfaceDivisionsX]->u.sd.value,
        static_cast<A_long>(kMinimumDivisions),
        static_cast<A_long>(kMaximumDivisions)));
    surface.divisions_y = static_cast<std::uint32_t>(std::clamp<A_long>(
        surface_params[kParamSurfaceDivisionsY]->u.sd.value,
        static_cast<A_long>(kMinimumDivisions),
        static_cast<A_long>(kMaximumDivisions)));
    CaptureMaterial(surface_params, surface);
    CaptureThickness(surface_params, surface);
    CaptureDeform(surface_params, surface);
    CaptureCornerCurl(surface_params, surface);
    CaptureEdgeTwist(surface_params, surface);
}


}  // namespace

PF_Err UserChangedParamV15(
    PF_InData* in_data,
    PF_ParamDef* params[],
    const PF_UserChangedParamExtra* extra) {
    if (!extra || !params[kParamSceneData]) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }
    PF_Handle scene_handle = params[kParamSceneData]->u.arb_d.value;
    if (!scene_handle) {
        return PF_Err_NONE;
    }
    auto* scene = static_cast<SceneData*>(PF_LOCK_HANDLE(scene_handle));
    if (!scene) {
        return PF_Err_OUT_OF_MEMORY;
    }
    if (!IsValidScene(*scene)) {
        InitializeScene(*scene, in_data->width, in_data->height);
    }

    bool scene_changed = false;
    const PF_ParamIndex raw_changed_index = extra->param_index;
    const AnimationParamAddress address =
        ResolveAnimationParam(raw_changed_index);
    const PF_ParamIndex changed_index =
        address.valid ? address.canonical : raw_changed_index;

    if (scene->reserved[kAnimationStreamsInitializedIndex] == 0) {
        const bool preserve_changed_value =
            address.valid &&
            (IsCornerCurlValueParam(changed_index) ||
             IsEdgeTwistValueParam(changed_index));
        const double changed_value = preserve_changed_value
                                         ? params[raw_changed_index]->u.fs_d.value
                                         : 0.0;
        SurfaceData& selected = scene->surfaces[scene->selected_surface];
        auto selected_view =
            BuildAnimationParameterView(params, selected.animation_bank);
        LoadCornerCurlValueParams(selected_view.data(), selected);
        LoadEdgeTwistValueParams(selected_view.data(), selected);
        if (preserve_changed_value) {
            params[raw_changed_index]->u.fs_d.value = changed_value;
        }
        scene->reserved[kAnimationStreamsInitializedIndex] = 1;
        scene_changed = true;
    }

    if (scene->reserved[kAnimationBanksInitializedIndex] == 0) {
        InitializeAdditionalAnimationBanks(params, *scene);
        scene_changed = true;
    }

    if (raw_changed_index == kParamSurfaceSelect) {
        if (scene->active != 0) {
            CaptureSurfaceAtCurrentFrame(
                params,
                scene->surfaces[scene->selected_surface]);
            const A_long requested =
                params[kParamSurfaceSelect]->u.pd.value - 1;
            scene->selected_surface = static_cast<std::uint32_t>(
                std::clamp<A_long>(
                    requested,
                    0,
                    static_cast<A_long>(scene->surface_count - 1)));
            scene_changed = true;
        }
        UpdateSurfaceUiParams(params, *scene);
    } else if (
        raw_changed_index == kParamAddSurface ||
        raw_changed_index == kParamDuplicateSurface ||
        raw_changed_index == kParamDeleteSurface) {
        if (scene->active == 0) {
            scene->active = 1;
            scene->surface_count = 1;
            scene->selected_surface = 0;
            CaptureSurfaceAtCurrentFrame(params, scene->surfaces[0]);
            scene_changed = true;
        } else {
            CaptureSurfaceAtCurrentFrame(
                params,
                scene->surfaces[scene->selected_surface]);
        }

        if (raw_changed_index == kParamAddSurface &&
            scene->surface_count < kMaximumSurfaces) {
            std::array<bool, kMaximumSurfaces> used_source_slots{};
            for (std::uint32_t index = 0; index < scene->surface_count; ++index) {
                used_source_slots[scene->surfaces[index].source_slot] = true;
            }
            std::uint32_t available_source_slot = 0;
            while (available_source_slot + 1 < kMaximumSurfaces &&
                   used_source_slots[available_source_slot]) {
                ++available_source_slot;
            }
            const std::uint32_t available_bank =
                FindAvailableAnimationBank(*scene);
            const std::uint32_t new_index = scene->surface_count++;
            InitializeFlatSurface(
                scene->surfaces[new_index],
                scene->next_surface_id++,
                in_data->width,
                in_data->height,
                true);
            scene->surfaces[new_index].source_slot = available_source_slot;
            scene->surfaces[new_index].animation_bank = available_bank;
            scene->selected_surface = new_index;
            LoadSurfaceIntoAnimationBank(params, scene->surfaces[new_index]);
            scene_changed = true;
        } else if (
            raw_changed_index == kParamDuplicateSurface &&
            scene->surface_count < kMaximumSurfaces) {
            const std::uint32_t available_bank =
                FindAvailableAnimationBank(*scene);
            const SurfaceData source =
                scene->surfaces[scene->selected_surface];
            const std::uint32_t new_index = scene->surface_count++;
            scene->surfaces[new_index] = source;
            scene->surfaces[new_index].id = scene->next_surface_id++;
            scene->surfaces[new_index].animation_bank = available_bank;
            scene->selected_surface = new_index;
            LoadSurfaceIntoAnimationBank(params, scene->surfaces[new_index]);
            scene_changed = true;
        } else if (
            raw_changed_index == kParamDeleteSurface &&
            scene->surface_count > 1) {
            for (std::uint32_t index = scene->selected_surface;
                 index + 1 < scene->surface_count;
                 ++index) {
                scene->surfaces[index] = scene->surfaces[index + 1];
            }
            scene->surfaces[scene->surface_count - 1] = {};
            --scene->surface_count;
            scene->selected_surface = std::min(
                scene->selected_surface,
                scene->surface_count - 1);
            scene_changed = true;
        }
        UpdateSurfaceUiParams(params, *scene);
    } else if (address.valid) {
        if (scene->active == 0) {
            scene->active = 1;
            scene->surface_count = 1;
            scene->selected_surface = 0;
        }
        const std::uint32_t surface_index =
            FindSurfaceForAnimationBank(*scene, address.bank);
        SurfaceData& surface = scene->surfaces[surface_index];
        auto view = BuildAnimationParameterView(params, address.bank);
        PF_ParamDef** surface_params = view.data();

        if ((changed_index >= kParamPoint00 &&
             changed_index <= kParamPoint33) ||
            (changed_index >= kParamDepth00 &&
             changed_index <= kParamDepth33) ||
            (changed_index >= kParamRotationX &&
             changed_index <= kParamRotationZ)) {
            CaptureLegacySurface(surface_params, surface);
            LoadSizeAndPositionParams(surface_params, surface);
        } else {
            CaptureLegacySurface(surface_params, surface);
            if (changed_index == kParamSurfaceSizeX ||
                changed_index == kParamSurfaceSizeY) {
                const bool change_x = changed_index == kParamSurfaceSizeX;
                const float current_size =
                    change_x ? surface.size_x : surface.size_y;
                const float desired_size = static_cast<float>(std::max(
                    0.001,
                    surface_params[changed_index]->u.fs_d.value));
                const float ratio =
                    desired_size / std::max(0.001F, current_size);
                for (StoredPoint3& point : surface.control_points) {
                    if (change_x) {
                        point.x = surface.position_x +
                                  (point.x - surface.position_x) * ratio;
                    } else {
                        point.y = surface.position_y +
                                  (point.y - surface.position_y) * ratio;
                    }
                }
                UpdateDerivedTransform(surface);
            } else if (changed_index == kParamSurfacePosition) {
                const PF_Point3DDef& desired =
                    surface_params[kParamSurfacePosition]->u.point3d_d;
                const float offset_x =
                    static_cast<float>(desired.x_value) - surface.position_x;
                const float offset_y =
                    static_cast<float>(desired.y_value) - surface.position_y;
                const float offset_z =
                    static_cast<float>(desired.z_value) - surface.position_z;
                for (StoredPoint3& point : surface.control_points) {
                    point.x += offset_x;
                    point.y += offset_y;
                    point.z += offset_z;
                }
                UpdateDerivedTransform(surface);
            } else if (
                changed_index == kParamSurfaceRotationOriginMode ||
                changed_index == kParamSurfaceRotationOriginX ||
                changed_index == kParamSurfaceRotationOriginY) {
                CaptureRotationOrigin(surface_params, surface);
            } else if (
                changed_index >= kParamSurfaceScaleX &&
                changed_index <= kParamSurfaceScaleZ) {
                CaptureScale(surface_params, surface);
            } else if (
                changed_index == kParamSurfaceDivisionsX ||
                changed_index == kParamSurfaceDivisionsY) {
                const std::uint32_t divisions =
                    static_cast<std::uint32_t>(std::clamp<A_long>(
                        surface_params[changed_index]->u.sd.value,
                        static_cast<A_long>(kMinimumDivisions),
                        static_cast<A_long>(kMaximumDivisions)));
                if (changed_index == kParamSurfaceDivisionsX) {
                    surface.divisions_x = divisions;
                } else {
                    surface.divisions_y = divisions;
                }
            } else if (changed_index == kParamSurfaceThickness) {
                CaptureThickness(surface_params, surface);
            } else if (
                changed_index == kParamSurfaceBendX ||
                changed_index == kParamSurfaceBendY ||
                changed_index == kParamSurfaceRollEdge ||
                changed_index == kParamSurfaceRollAngle ||
                changed_index == kParamSurfaceRollLength) {
                CaptureDeform(surface_params, surface);
            } else if (IsCornerCurlValueParam(changed_index)) {
                CaptureCornerCurl(surface_params, surface);
            } else if (IsEdgeTwistValueParam(changed_index)) {
                CaptureEdgeTwist(surface_params, surface);
            } else {
                CaptureMaterial(surface_params, surface);
            }

            if (changed_index == kParamSurfaceSizeX ||
                changed_index == kParamSurfaceSizeY ||
                changed_index == kParamSurfacePosition ||
                (changed_index >= kParamSurfaceScaleX &&
                 changed_index <= kParamSurfaceScaleZ)) {
                surface.transform_mode = 1;
            }
            if (changed_index == kParamSurfaceSizeX ||
                changed_index == kParamSurfaceSizeY ||
                changed_index == kParamSurfacePosition) {
                LoadLegacyGeometryParams(surface_params, surface);
            }
        }
        UpdateSurfaceUiParams(params, *scene);
        scene_changed = true;
    }

    PF_UNLOCK_HANDLE(scene_handle);
    if (scene_changed) {
        params[kParamSceneData]->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
    }
    return PF_Err_NONE;
}

namespace {
enum GizmoDragTarget : A_intptr_t {
    kGizmoDragNone = 0,
    kGizmoDragControlPoint = 1,
    kGizmoDragSurface = 2,
    kGizmoDragRotationOrigin = 3,
    kGizmoDragCurlTip = 4,
    kGizmoDragCurlExtent = 5,
    kGizmoDragRollAngle = 6,
    kGizmoDragRollLength = 7,
    kGizmoDragTwistAngle = 8,
    kGizmoDragTwistFalloff = 9,
    kGizmoDragControlDepth = 10,
    kGizmoDragSurfaceScale = 11,
    kGizmoDragSurfaceRotation = 12
};

constexpr double kGizmoControlHitRadius = 9.0;
constexpr double kGizmoOriginHitRadius = 11.0;
constexpr double kGizmoDeformHitRadius = 11.0;
constexpr double kGizmoCurlTipOffset = 16.0;
constexpr double kGizmoDepthHandleOffset = 18.0;
constexpr double kGizmoDepthHitRadius = 7.5;
constexpr double kGizmoTransformHitRadius = 10.0;
constexpr double kGizmoScaleAxisLength = 54.0;
constexpr double kGizmoRotationHandleRadius = 76.0;
constexpr int kGizmoCurveSamples = 18;
constexpr int kGizmoHitSubdivisions = 8;

// Drag-solver probes and guards. The probe step is the parameter-space
// perturbation used for the finite-difference Jacobians; ForwardEpsilon flips
// its sign within kGizmoLimitMargin of a parameter's maximum so the probe
// stays inside the legal range. The damping terms keep the damped-least-squares
// solves usable when the surface is nearly edge-on, and a solved step is capped
// at kGizmoMaxSolvedDelta parameter units per event.
constexpr double kGizmoProbeStep = 1.0;
constexpr double kGizmoLimitMargin = 0.5;
constexpr double kGizmoSolverDamping = 1.0e-4;
constexpr double kGizmoScalarSolverDamping = 1.0e-5;
constexpr double kGizmoSolverSingularityGuard = 1.0e-10;
constexpr double kGizmoMaxSolvedDelta = 2000.0;

// Minimum projected screen-direction lengths (frame pixels) below which a
// handle falls back to its default direction instead of normalizing noise.
constexpr double kGizmoMinDepthDirection = 0.5;
constexpr double kGizmoMinAxisDirection = 0.75;
constexpr double kGizmoMinRotationTangent = 0.05;
constexpr double kGizmoMinCurlDirection = 1.0e-5;

// Handle placement in normalized surface coordinates: how far along its edge
// the roll/twist handle pair sits, and how far inward the twist-falloff handle
// moves at 100% falloff (never closer to the edge than the minimum).
constexpr double kGizmoRollHandleEdgeFraction = 0.38;
constexpr double kGizmoTwistHandleEdgeFraction = 0.62;
constexpr double kGizmoTwistFalloffInwardScale = 0.5;
constexpr double kGizmoTwistFalloffMinimum = 0.01;

struct GizmoHandleHit {
    GizmoDragTarget target{kGizmoDragNone};
    std::uint32_t index{};
    Point2 point{};
};

struct GizmoVisibility {
    bool surface{};
    bool controls{};
    bool deform{};
    bool position{};
    bool rotation{};
    bool scale{};
};

GizmoVisibility ResolveGizmoVisibility(PF_ParamDef* params[]) {
    const A_long interaction = std::clamp<A_long>(
        params[kParamGizmoInteractionMode]->u.pd.value,
        kGizmoInteractionAll,
        kGizmoInteractionDeform);
    const A_long tool = std::clamp<A_long>(
        params[kParamGizmoTool]->u.pd.value,
        kGizmoToolAll,
        kGizmoToolScale);
    const bool all_interactions = interaction == kGizmoInteractionAll;
    GizmoVisibility visibility;
    visibility.surface =
        all_interactions || interaction == kGizmoInteractionSurface;
    visibility.controls =
        all_interactions || interaction == kGizmoInteractionControlPoints;
    visibility.deform =
        all_interactions || interaction == kGizmoInteractionDeform;
    visibility.position = visibility.surface &&
                          (tool == kGizmoToolAll ||
                           tool == kGizmoToolPosition);
    visibility.rotation = visibility.surface &&
                          (tool == kGizmoToolAll ||
                           tool == kGizmoToolRotation);
    visibility.scale = visibility.surface &&
                       (tool == kGizmoToolAll || tool == kGizmoToolScale);
    return visibility;
}

bool LayerPointToFrame(
    PF_InData* in_data,
    PF_EventExtra* event_extra,
    const Point2& layer_point,
    Point2& frame_point) {
    if (!event_extra || !event_extra->contextH ||
        (*event_extra->contextH)->w_type != PF_Window_COMP) {
        return false;
    }
    PF_FixedPoint point{
        FLOAT2FIX(layer_point.x),
        FLOAT2FIX(layer_point.y)};
    if (event_extra->cbs.layer_to_comp(
            event_extra->cbs.refcon,
            event_extra->contextH,
            in_data->current_time,
            in_data->time_scale,
            &point) != PF_Err_NONE) {
        return false;
    }
    if (event_extra->cbs.source_to_frame(
            event_extra->cbs.refcon,
            event_extra->contextH,
            &point) != PF_Err_NONE) {
        return false;
    }
    frame_point = {FIX_2_FLOAT(point.x), FIX_2_FLOAT(point.y)};
    return std::isfinite(frame_point.x) && std::isfinite(frame_point.y);
}

bool ProjectWorldPointToFrame(
    PF_InData* in_data,
    PF_EventExtra* event_extra,
    const CameraState& camera,
    const Point3& world_point,
    Point2& frame_point) {
    const Vertex projected = ProjectVertex(
        world_point,
        {0.0, 0.0, -1.0},
        0.0,
        0.0,
        camera);
    if (!projected.visible || !std::isfinite(projected.x) ||
        !std::isfinite(projected.y)) {
        return false;
    }
    return LayerPointToFrame(
        in_data,
        event_extra,
        {projected.x, projected.y},
        frame_point);
}

bool ProjectSurfacePointToFrame(
    PF_InData* in_data,
    PF_EventExtra* event_extra,
    const SurfaceData& surface,
    const CameraState& camera,
    double u,
    double v,
    Point2& frame_point) {
    const SurfaceEvaluationState evaluation =
        BuildSurfaceEvaluationState(surface, camera, 1.0, 1.0, 1.0);
    return ProjectWorldPointToFrame(
        in_data,
        event_extra,
        camera,
        EvaluateTransformedPoint(surface, evaluation, u, v),
        frame_point);
}

bool ProjectRotationOriginToFrame(
    PF_InData* in_data,
    PF_EventExtra* event_extra,
    const SurfaceData& surface,
    const CameraState& camera,
    Point2& frame_point) {
    const SurfaceEvaluationState evaluation =
        BuildSurfaceEvaluationState(surface, camera, 1.0, 1.0, 1.0);
    return ProjectWorldPointToFrame(
        in_data,
        event_extra,
        camera,
        {evaluation.rotation_origin_x,
         evaluation.rotation_origin_y,
         evaluation.rotation_origin_z},
        frame_point);
}

bool ProjectControlDepthHandle(
    PF_InData* in_data,
    PF_EventExtra* event_extra,
    const SurfaceData& surface,
    const CameraState& camera,
    std::uint32_t control_index,
    Point2& frame_point,
    Point2* anchor = nullptr,
    Point2* axis = nullptr) {
    if (control_index >= 16) {
        return false;
    }
    const double u = static_cast<double>(control_index % 4) / 3.0;
    const double v = static_cast<double>(control_index / 4) / 3.0;
    Point2 control_point;
    if (!ProjectSurfacePointToFrame(
            in_data,
            event_extra,
            surface,
            camera,
            u,
            v,
            control_point)) {
        return false;
    }
    const double reference_extent = std::max({
        std::abs(static_cast<double>(surface.size_x)),
        std::abs(static_cast<double>(surface.size_y)),
        100.0});
    const double depth_step = std::clamp(reference_extent * 0.08, 8.0, 200.0);
    SurfaceData depth_surface = surface;
    depth_surface.control_points[control_index].z +=
        static_cast<float>(depth_step);
    UpdateDerivedTransform(depth_surface);
    Point2 depth_point;
    double direction_x = 0.0;
    double direction_y = -1.0;
    if (ProjectSurfacePointToFrame(
            in_data,
            event_extra,
            depth_surface,
            camera,
            u,
            v,
            depth_point)) {
        const double candidate_x = depth_point.x - control_point.x;
        const double candidate_y = depth_point.y - control_point.y;
        const double length = std::hypot(candidate_x, candidate_y);
        if (length > kGizmoMinDepthDirection) {
            direction_x = candidate_x / length;
            direction_y = candidate_y / length;
        }
    }
    frame_point = {
        control_point.x + direction_x * kGizmoDepthHandleOffset,
        control_point.y + direction_y * kGizmoDepthHandleOffset};
    if (anchor) {
        *anchor = control_point;
    }
    if (axis) {
        *axis = {direction_x, direction_y};
    }
    return true;
}

Point3 RotateSurfaceTransformPoint(
    const SurfaceEvaluationState& evaluation,
    Point3 point) {
    return RotatePoint(
        point,
        evaluation.rotation_origin_x,
        evaluation.rotation_origin_y,
        evaluation.rotation_origin_z,
        evaluation.rotation_x,
        evaluation.rotation_y,
        evaluation.rotation_z);
}

Point3 TransformAxisVector(std::uint32_t axis, double length) {
    switch (axis) {
        case 1:
            return {0.0, length, 0.0};
        case 2:
            return {0.0, 0.0, length};
        default:
            return {length, 0.0, 0.0};
    }
}

Point2 FallbackTransformDirection(std::uint32_t axis) {
    switch (axis) {
        case 1:
            return {0.0, -1.0};
        case 2:
            return {-0.70710678118, -0.70710678118};
        default:
            return {1.0, 0.0};
    }
}

bool NormalizeScreenDirection(
    const Point2& anchor,
    const Point2& endpoint,
    std::uint32_t axis,
    Point2& direction) {
    const double dx = endpoint.x - anchor.x;
    const double dy = endpoint.y - anchor.y;
    const double length = std::hypot(dx, dy);
    if (length > kGizmoMinAxisDirection) {
        direction = {dx / length, dy / length};
    } else {
        direction = FallbackTransformDirection(axis);
    }
    return std::isfinite(direction.x) && std::isfinite(direction.y);
}

bool ProjectScaleHandle(
    PF_InData* in_data,
    PF_EventExtra* event_extra,
    const SurfaceData& surface,
    const CameraState& camera,
    std::uint32_t axis,
    Point2& frame_point,
    Point2* anchor = nullptr,
    Point2* direction = nullptr) {
    if (axis > 2) {
        return false;
    }
    const SurfaceEvaluationState evaluation =
        BuildSurfaceEvaluationState(surface, camera, 1.0, 1.0, 1.0);
    const Point3 pivot{
        evaluation.pivot_x,
        evaluation.pivot_y,
        evaluation.pivot_z};
    const Point3 rotated_pivot =
        RotateSurfaceTransformPoint(evaluation, pivot);
    const double world_axis_length = std::max({
        std::abs(static_cast<double>(surface.size_x)),
        std::abs(static_cast<double>(surface.size_y)),
        100.0}) *
                                     0.25;
    const Point3 vector = TransformAxisVector(axis, world_axis_length);
    const Point3 axis_point = RotateSurfaceTransformPoint(
        evaluation,
        {pivot.x + vector.x, pivot.y + vector.y, pivot.z + vector.z});
    Point2 pivot_frame;
    Point2 axis_frame;
    if (!ProjectWorldPointToFrame(
            in_data,
            event_extra,
            camera,
            rotated_pivot,
            pivot_frame) ||
        !ProjectWorldPointToFrame(
            in_data,
            event_extra,
            camera,
            axis_point,
            axis_frame)) {
        return false;
    }
    Point2 screen_direction;
    if (!NormalizeScreenDirection(
            pivot_frame, axis_frame, axis, screen_direction)) {
        return false;
    }
    frame_point = {
        pivot_frame.x + screen_direction.x * kGizmoScaleAxisLength,
        pivot_frame.y + screen_direction.y * kGizmoScaleAxisLength};
    if (anchor) {
        *anchor = pivot_frame;
    }
    if (direction) {
        *direction = screen_direction;
    }
    return true;
}

Point3 RotationHandleVector(std::uint32_t axis, double radius) {
    switch (axis) {
        case 1:
            return {radius * 0.94, 0.0, radius * 0.34};
        case 2:
            return {-radius * 0.70710678118,
                    radius * 0.70710678118,
                    0.0};
        default:
            return {0.0, -radius * 0.94, radius * 0.34};
    }
}

bool ProjectRotationHandle(
    PF_InData* in_data,
    PF_EventExtra* event_extra,
    const SurfaceData& surface,
    const CameraState& camera,
    std::uint32_t axis,
    Point2& frame_point,
    Point2* anchor = nullptr) {
    if (axis > 2) {
        return false;
    }
    const SurfaceEvaluationState evaluation =
        BuildSurfaceEvaluationState(surface, camera, 1.0, 1.0, 1.0);
    const Point3 origin{
        evaluation.rotation_origin_x,
        evaluation.rotation_origin_y,
        evaluation.rotation_origin_z};
    const double world_radius = std::max({
        std::abs(static_cast<double>(surface.size_x)),
        std::abs(static_cast<double>(surface.size_y)),
        100.0}) *
                                0.42;
    const Point3 vector = RotationHandleVector(axis, world_radius);
    const Point3 ring_point = RotateSurfaceTransformPoint(
        evaluation,
        {origin.x + vector.x, origin.y + vector.y, origin.z + vector.z});
    Point2 origin_frame;
    Point2 ring_frame;
    if (!ProjectWorldPointToFrame(
            in_data,
            event_extra,
            camera,
            origin,
            origin_frame) ||
        !ProjectWorldPointToFrame(
            in_data,
            event_extra,
            camera,
            ring_point,
            ring_frame)) {
        return false;
    }
    Point2 screen_direction;
    if (!NormalizeScreenDirection(
            origin_frame, ring_frame, axis, screen_direction)) {
        return false;
    }
    frame_point = {
        origin_frame.x + screen_direction.x * kGizmoRotationHandleRadius,
        origin_frame.y + screen_direction.y * kGizmoRotationHandleRadius};
    if (anchor) {
        *anchor = origin_frame;
    }
    return true;
}

void CornerCoordinates(
    std::uint32_t corner,
    double& u,
    double& v) {
    u = corner == kCornerTopRight || corner == kCornerBottomRight
            ? 1.0
            : 0.0;
    v = corner == kCornerBottomRight || corner == kCornerBottomLeft
            ? 1.0
            : 0.0;
}

void CurlExtentCoordinates(
    const SurfaceData& surface,
    std::uint32_t corner,
    double& u,
    double& v) {
    constexpr double kDegreesToRadians =
        3.14159265358979323846 / 180.0;
    const CornerCurlData& curl = surface.corner_curls[corner - 1];
    const double extent_x = std::max(
        std::abs(static_cast<double>(surface.size_x)),
        1.0);
    const double extent_y = std::max(
        std::abs(static_cast<double>(surface.size_y)),
        1.0);
    const double length = std::min(extent_x, extent_y) *
                          std::clamp(
                              static_cast<double>(curl.length) / 100.0,
                              0.0,
                              1.5);
    const double direction = std::clamp(
                                 static_cast<double>(curl.direction),
                                 0.0,
                                 90.0) *
                             kDegreesToRadians;
    const double horizontal =
        std::clamp(length * std::cos(direction) / extent_x, 0.0, 1.0);
    const double vertical =
        std::clamp(length * std::sin(direction) / extent_y, 0.0, 1.0);
    u = corner == kCornerTopRight || corner == kCornerBottomRight
            ? 1.0 - horizontal
            : horizontal;
    v = corner == kCornerBottomRight || corner == kCornerBottomLeft
            ? 1.0 - vertical
            : vertical;
}

void RollHandleCoordinates(
    const SurfaceData& surface,
    bool extent_handle,
    double& u,
    double& v) {
    constexpr double along_edge = kGizmoRollHandleEdgeFraction;
    const double inward = extent_handle
                              ? std::clamp(
                                    static_cast<double>(surface.roll_length) /
                                        100.0,
                                    0.0,
                                    1.0)
                              : 0.0;
    switch (surface.roll_edge) {
        case kRollEdgeLeft:
            u = inward;
            v = along_edge;
            break;
        case kRollEdgeBottom:
            u = along_edge;
            v = 1.0 - inward;
            break;
        case kRollEdgeTop:
            u = along_edge;
            v = inward;
            break;
        default:
            u = 1.0 - inward;
            v = along_edge;
            break;
    }
}

void TwistHandleCoordinates(
    const SurfaceData& surface,
    std::uint32_t edge,
    bool falloff_handle,
    double& u,
    double& v) {
    constexpr double along_edge = kGizmoTwistHandleEdgeFraction;
    const double inward = falloff_handle
                              ? kGizmoTwistFalloffInwardScale *
                                    std::clamp(
                                        static_cast<double>(
                                            surface.edge_twists[edge - 1]
                                                .falloff) /
                                            100.0,
                                        kGizmoTwistFalloffMinimum,
                                        1.0)
                              : 0.0;
    switch (edge) {
        case kTwistEdgeRight:
            u = 1.0 - inward;
            v = along_edge;
            break;
        case kTwistEdgeTop:
            u = along_edge;
            v = inward;
            break;
        case kTwistEdgeBottom:
            u = along_edge;
            v = 1.0 - inward;
            break;
        default:
            u = inward;
            v = along_edge;
            break;
    }
}

bool ProjectCurlTipHandle(
    PF_InData* in_data,
    PF_EventExtra* event_extra,
    const SurfaceData& surface,
    const CameraState& camera,
    std::uint32_t corner,
    Point2& frame_point,
    Point2* anchor = nullptr) {
    double u = 0.0;
    double v = 0.0;
    CornerCoordinates(corner, u, v);
    Point2 corner_point;
    Point2 center_point;
    if (!ProjectSurfacePointToFrame(
            in_data,
            event_extra,
            surface,
            camera,
            u,
            v,
            corner_point) ||
        !ProjectSurfacePointToFrame(
            in_data,
            event_extra,
            surface,
            camera,
            0.5,
            0.5,
            center_point)) {
        return false;
    }
    double dx = corner_point.x - center_point.x;
    double dy = corner_point.y - center_point.y;
    double length = std::hypot(dx, dy);
    if (length < kGizmoMinCurlDirection) {
        dx = u < 0.5 ? -1.0 : 1.0;
        dy = v < 0.5 ? -1.0 : 1.0;
        length = std::sqrt(2.0);
    }
    frame_point = {
        corner_point.x + dx * kGizmoCurlTipOffset / length,
        corner_point.y + dy * kGizmoCurlTipOffset / length};
    if (anchor) {
        *anchor = corner_point;
    }
    return true;
}

bool ProjectCurlExtentHandle(
    PF_InData* in_data,
    PF_EventExtra* event_extra,
    const SurfaceData& surface,
    const CameraState& camera,
    std::uint32_t corner,
    Point2& frame_point) {
    double u = 0.0;
    double v = 0.0;
    CurlExtentCoordinates(surface, corner, u, v);
    return ProjectSurfacePointToFrame(
        in_data, event_extra, surface, camera, u, v, frame_point);
}

bool ProjectRollHandle(
    PF_InData* in_data,
    PF_EventExtra* event_extra,
    const SurfaceData& surface,
    const CameraState& camera,
    bool extent_handle,
    Point2& frame_point) {
    double u = 0.0;
    double v = 0.0;
    RollHandleCoordinates(surface, extent_handle, u, v);
    return ProjectSurfacePointToFrame(
        in_data, event_extra, surface, camera, u, v, frame_point);
}

bool ProjectTwistHandle(
    PF_InData* in_data,
    PF_EventExtra* event_extra,
    const SurfaceData& surface,
    const CameraState& camera,
    std::uint32_t edge,
    bool falloff_handle,
    Point2& frame_point) {
    double u = 0.0;
    double v = 0.0;
    TwistHandleCoordinates(surface, edge, falloff_handle, u, v);
    return ProjectSurfacePointToFrame(
        in_data, event_extra, surface, camera, u, v, frame_point);
}

CameraState BuildGizmoCamera(
    PF_InData* in_data,
    PF_ParamDef* params[],
    A_long input_width,
    A_long input_height) {
    return BuildResolvedCameraState(
        in_data,
        params,
        static_cast<double>(input_width) * 0.5,
        static_cast<double>(input_height) * 0.5,
        0.0,
        0.0,
        1.0,
        1.0,
        1.0);
}

double SquaredDistance(const Point2& first, const Point2& second) {
    const double dx = first.x - second.x;
    const double dy = first.y - second.y;
    return dx * dx + dy * dy;
}

double Cross2D(const Point2& a, const Point2& b, const Point2& c) {
    return (b.x - a.x) * (c.y - a.y) -
           (b.y - a.y) * (c.x - a.x);
}

bool PointInTriangle(
    const Point2& point,
    const Point2& a,
    const Point2& b,
    const Point2& c) {
    const double first = Cross2D(a, b, point);
    const double second = Cross2D(b, c, point);
    const double third = Cross2D(c, a, point);
    const bool has_negative = first < 0.0 || second < 0.0 || third < 0.0;
    const bool has_positive = first > 0.0 || second > 0.0 || third > 0.0;
    return !(has_negative && has_positive);
}

bool HitProjectedSurface(
    PF_InData* in_data,
    PF_EventExtra* event_extra,
    const SurfaceData& surface,
    const CameraState& camera,
    const Point2& mouse) {
    constexpr int stride = kGizmoHitSubdivisions + 1;
    std::array<Point2, stride * stride> points{};
    std::array<bool, stride * stride> visible{};
    for (int row = 0; row <= kGizmoHitSubdivisions; ++row) {
        for (int column = 0; column <= kGizmoHitSubdivisions; ++column) {
            const int index = row * stride + column;
            visible[static_cast<std::size_t>(index)] =
                ProjectSurfacePointToFrame(
                    in_data,
                    event_extra,
                    surface,
                    camera,
                    static_cast<double>(column) / kGizmoHitSubdivisions,
                    static_cast<double>(row) / kGizmoHitSubdivisions,
                    points[static_cast<std::size_t>(index)]);
        }
    }
    for (int row = 0; row < kGizmoHitSubdivisions; ++row) {
        for (int column = 0; column < kGizmoHitSubdivisions; ++column) {
            const int top_left = row * stride + column;
            const int top_right = top_left + 1;
            const int bottom_left = top_left + stride;
            const int bottom_right = bottom_left + 1;
            if (visible[static_cast<std::size_t>(top_left)] &&
                visible[static_cast<std::size_t>(top_right)] &&
                visible[static_cast<std::size_t>(bottom_left)] &&
                visible[static_cast<std::size_t>(bottom_right)] &&
                (PointInTriangle(
                     mouse,
                     points[static_cast<std::size_t>(top_left)],
                     points[static_cast<std::size_t>(top_right)],
                     points[static_cast<std::size_t>(bottom_right)]) ||
                 PointInTriangle(
                     mouse,
                     points[static_cast<std::size_t>(top_left)],
                     points[static_cast<std::size_t>(bottom_right)],
                     points[static_cast<std::size_t>(bottom_left)]))) {
                return true;
            }
        }
    }
    return false;
}

bool SolveProjectedDelta(
    const Point2& base,
    const Point2& perturbed_x,
    const Point2& perturbed_y,
    const Point2& target,
    double epsilon_x,
    double epsilon_y,
    double& delta_x,
    double& delta_y) {
    const double j00 = (perturbed_x.x - base.x) / epsilon_x;
    const double j10 = (perturbed_x.y - base.y) / epsilon_x;
    const double j01 = (perturbed_y.x - base.x) / epsilon_y;
    const double j11 = (perturbed_y.y - base.y) / epsilon_y;
    const double screen_x = target.x - base.x;
    const double screen_y = target.y - base.y;

    // Damped least squares remains usable when the surface is nearly edge-on.
    const double a = j00 * j00 + j10 * j10 + kGizmoSolverDamping;
    const double b = j00 * j01 + j10 * j11;
    const double c = j01 * j01 + j11 * j11 + kGizmoSolverDamping;
    const double rhs_x = j00 * screen_x + j10 * screen_y;
    const double rhs_y = j01 * screen_x + j11 * screen_y;
    const double determinant = a * c - b * b;
    if (std::abs(determinant) < kGizmoSolverSingularityGuard) {
        return false;
    }
    delta_x = (rhs_x * c - rhs_y * b) / determinant;
    delta_y = (a * rhs_y - b * rhs_x) / determinant;
    delta_x = std::clamp(delta_x, -kGizmoMaxSolvedDelta, kGizmoMaxSolvedDelta);
    delta_y = std::clamp(delta_y, -kGizmoMaxSolvedDelta, kGizmoMaxSolvedDelta);
    return std::isfinite(delta_x) && std::isfinite(delta_y);
}

bool SolveProjectedScalarDelta(
    const Point2& base,
    const Point2& perturbed,
    const Point2& target,
    double epsilon,
    double& delta) {
    const double derivative_x = (perturbed.x - base.x) / epsilon;
    const double derivative_y = (perturbed.y - base.y) / epsilon;
    const double denominator =
        derivative_x * derivative_x + derivative_y * derivative_y +
        kGizmoScalarSolverDamping;
    if (denominator <= kGizmoSolverSingularityGuard) {
        return false;
    }
    delta = (derivative_x * (target.x - base.x) +
             derivative_y * (target.y - base.y)) /
            denominator;
    delta = std::clamp(delta, -kGizmoMaxSolvedDelta, kGizmoMaxSolvedDelta);
    return std::isfinite(delta);
}

double ForwardEpsilon(double value, double maximum) {
    return value >= maximum - kGizmoLimitMargin ? -kGizmoProbeStep
                                                : kGizmoProbeStep;
}

void SetFloatParameter(
    PF_ParamDef* parameter,
    double value) {
    parameter->u.fs_d.value = value;
    parameter->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
}

void SetPopupParameter(
    PF_ParamDef* parameter,
    A_long value) {
    parameter->u.pd.value = value;
    parameter->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
}

void MarkSurfaceDerivedGeometryParams(
    PF_ParamDef* surface_params[],
    const SurfaceData& surface) {
    PF_ParamDef* size_x = surface_params[kParamSurfaceSizeX];
    PF_ParamDef* size_y = surface_params[kParamSurfaceSizeY];
    PF_ParamDef* position = surface_params[kParamSurfacePosition];
    size_x->u.fs_d.value = surface.size_x;
    size_y->u.fs_d.value = surface.size_y;
    position->u.point3d_d.x_value = surface.position_x;
    position->u.point3d_d.y_value = surface.position_y;
    position->u.point3d_d.z_value = surface.position_z;
    size_x->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
    size_y->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
    position->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
}

bool DragControlPoint(
    PF_InData* in_data,
    PF_ParamDef* params[],
    PF_EventExtra* event_extra,
    SurfaceData surface,
    const CameraState& camera,
    std::uint32_t control_index,
    const Point2& mouse) {
    if (control_index >= 16) {
        return false;
    }
    const int row = static_cast<int>(control_index / 4);
    const int column = static_cast<int>(control_index % 4);
    const double u = static_cast<double>(column) / 3.0;
    const double v = static_cast<double>(row) / 3.0;
    Point2 base;
    Point2 perturbed_x;
    Point2 perturbed_y;
    if (!ProjectSurfacePointToFrame(
            in_data, event_extra, surface, camera, u, v, base)) {
        return false;
    }
    constexpr double epsilon = kGizmoProbeStep;
    SurfaceData x_surface = surface;
    x_surface.control_points[control_index].x += epsilon;
    UpdateDerivedTransform(x_surface);
    SurfaceData y_surface = surface;
    y_surface.control_points[control_index].y += epsilon;
    UpdateDerivedTransform(y_surface);
    if (!ProjectSurfacePointToFrame(
            in_data, event_extra, x_surface, camera, u, v, perturbed_x) ||
        !ProjectSurfacePointToFrame(
            in_data, event_extra, y_surface, camera, u, v, perturbed_y)) {
        return false;
    }
    double delta_x = 0.0;
    double delta_y = 0.0;
    if (!SolveProjectedDelta(
            base,
            perturbed_x,
            perturbed_y,
            mouse,
            epsilon,
            epsilon,
            delta_x,
            delta_y)) {
        return false;
    }
    surface.control_points[control_index].x += static_cast<float>(delta_x);
    surface.control_points[control_index].y += static_cast<float>(delta_y);
    UpdateDerivedTransform(surface);
    auto view = BuildAnimationParameterView(params, surface.animation_bank);
    PF_ParamDef** surface_params = view.data();
    PF_ParamDef* point = surface_params[kParamPoint00 + control_index];
    point->u.td.x_value = FLOAT2FIX(surface.control_points[control_index].x);
    point->u.td.y_value = FLOAT2FIX(surface.control_points[control_index].y);
    point->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
    MarkSurfaceDerivedGeometryParams(surface_params, surface);
    return true;
}

bool DragControlDepth(
    PF_InData* in_data,
    PF_ParamDef* params[],
    PF_EventExtra* event_extra,
    const SurfaceData& surface,
    const CameraState& camera,
    std::uint32_t control_index,
    double screen_delta_x,
    double screen_delta_y) {
    if (control_index >= 16 || !std::isfinite(screen_delta_x) ||
        !std::isfinite(screen_delta_y)) {
        return false;
    }
    Point2 handle;
    Point2 axis;
    if (!ProjectControlDepthHandle(
            in_data,
            event_extra,
            surface,
            camera,
            control_index,
            handle,
            nullptr,
            &axis)) {
        return false;
    }
    const double reference_extent = std::max({
        std::abs(static_cast<double>(surface.size_x)),
        std::abs(static_cast<double>(surface.size_y)),
        100.0});
    const double depth_per_pixel =
        std::clamp(reference_extent / 500.0, 0.25, 10.0);
    const double projected_delta =
        screen_delta_x * axis.x + screen_delta_y * axis.y;
    const double next_depth = std::clamp(
        static_cast<double>(surface.control_points[control_index].z) +
            projected_delta * depth_per_pixel,
        -4000.0,
        4000.0);
    auto view = BuildAnimationParameterView(params, surface.animation_bank);
    SetFloatParameter(
        view[kParamDepth00 + control_index],
        next_depth);
    return true;
}

bool DragSurfaceScale(
    PF_InData* in_data,
    PF_ParamDef* params[],
    PF_EventExtra* event_extra,
    const SurfaceData& surface,
    const CameraState& camera,
    std::uint32_t axis,
    double screen_delta_x,
    double screen_delta_y) {
    if (axis > 2) {
        return false;
    }
    Point2 handle;
    Point2 direction;
    if (!ProjectScaleHandle(
            in_data,
            event_extra,
            surface,
            camera,
            axis,
            handle,
            nullptr,
            &direction)) {
        return false;
    }
    const double projected_delta =
        screen_delta_x * direction.x + screen_delta_y * direction.y;
    const double current_scale[3] = {
        surface.scale_x,
        surface.scale_y,
        surface.scale_z};
    const double next_scale = std::clamp(
        current_scale[axis] + projected_delta * 1.5,
        -1000.0,
        1000.0);
    auto view = BuildAnimationParameterView(params, surface.animation_bank);
    SetFloatParameter(
        view[kParamSurfaceScaleX + axis],
        next_scale);
    return true;
}

bool DragSurfaceRotation(
    PF_InData* in_data,
    PF_ParamDef* params[],
    PF_EventExtra* event_extra,
    const SurfaceData& surface,
    const CameraState& camera,
    std::uint32_t axis,
    double screen_delta_x,
    double screen_delta_y) {
    if (axis > 2) {
        return false;
    }
    Point2 base;
    Point2 anchor;
    if (!ProjectRotationHandle(
            in_data,
            event_extra,
            surface,
            camera,
            axis,
            base,
            &anchor)) {
        return false;
    }
    SurfaceData perturbed_surface = surface;
    float* rotations[3] = {
        &perturbed_surface.rotation_x,
        &perturbed_surface.rotation_y,
        &perturbed_surface.rotation_z};
    *rotations[axis] += 1.0F;
    Point2 perturbed;
    if (!ProjectRotationHandle(
            in_data,
            event_extra,
            perturbed_surface,
            camera,
            axis,
            perturbed)) {
        return false;
    }
    double tangent_x = perturbed.x - base.x;
    double tangent_y = perturbed.y - base.y;
    double tangent_length = std::hypot(tangent_x, tangent_y);
    if (tangent_length < kGizmoMinRotationTangent) {
        const double radial_x = base.x - anchor.x;
        const double radial_y = base.y - anchor.y;
        tangent_length = std::hypot(radial_x, radial_y);
        if (tangent_length < kGizmoMinRotationTangent) {
            return false;
        }
        tangent_x = -radial_y / tangent_length;
        tangent_y = radial_x / tangent_length;
    } else {
        tangent_x /= tangent_length;
        tangent_y /= tangent_length;
    }
    const double angle_delta =
        (screen_delta_x * tangent_x + screen_delta_y * tangent_y) * 0.85;
    const double current_rotation[3] = {
        surface.rotation_x,
        surface.rotation_y,
        surface.rotation_z};
    const double next_rotation = std::clamp(
        current_rotation[axis] + angle_delta,
        -3600.0,
        3600.0);
    auto view = BuildAnimationParameterView(params, surface.animation_bank);
    PF_ParamDef* rotation = view[kParamRotationX + axis];
    rotation->u.ad.value = FLOAT2FIX(next_rotation);
    rotation->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
    return true;
}

bool DragSurfacePosition(
    PF_InData* in_data,
    PF_ParamDef* params[],
    PF_EventExtra* event_extra,
    SurfaceData surface,
    const CameraState& camera,
    const Point2& mouse) {
    Point2 base;
    Point2 perturbed_x;
    Point2 perturbed_y;
    if (!ProjectSurfacePointToFrame(
            in_data, event_extra, surface, camera, 0.5, 0.5, base)) {
        return false;
    }
    constexpr float epsilon = static_cast<float>(kGizmoProbeStep);
    SurfaceData x_surface = surface;
    SurfaceData y_surface = surface;
    for (StoredPoint3& point : x_surface.control_points) {
        point.x += epsilon;
    }
    for (StoredPoint3& point : y_surface.control_points) {
        point.y += epsilon;
    }
    UpdateDerivedTransform(x_surface);
    UpdateDerivedTransform(y_surface);
    if (!ProjectSurfacePointToFrame(
            in_data, event_extra, x_surface, camera, 0.5, 0.5, perturbed_x) ||
        !ProjectSurfacePointToFrame(
            in_data, event_extra, y_surface, camera, 0.5, 0.5, perturbed_y)) {
        return false;
    }
    double delta_x = 0.0;
    double delta_y = 0.0;
    if (!SolveProjectedDelta(
            base,
            perturbed_x,
            perturbed_y,
            mouse,
            epsilon,
            epsilon,
            delta_x,
            delta_y)) {
        return false;
    }
    auto view = BuildAnimationParameterView(params, surface.animation_bank);
    PF_ParamDef* position = view[kParamSurfacePosition];
    position->u.point3d_d.x_value = surface.position_x + delta_x;
    position->u.point3d_d.y_value = surface.position_y + delta_y;
    position->u.point3d_d.z_value = surface.position_z;
    position->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
    return true;
}

bool DragRotationOrigin(
    PF_InData* in_data,
    PF_ParamDef* params[],
    PF_EventExtra* event_extra,
    SurfaceData surface,
    const CameraState& camera,
    const Point2& mouse) {
    // Switching to Custom preserves the current preset pivot before dragging.
    if (surface.rotation_origin_mode != kRotationOriginCustom) {
        switch (surface.rotation_origin_mode) {
            case kRotationOriginLeftEdge:
                surface.rotation_origin_x = 0.0F;
                surface.rotation_origin_y = 50.0F;
                break;
            case kRotationOriginRightEdge:
                surface.rotation_origin_x = 100.0F;
                surface.rotation_origin_y = 50.0F;
                break;
            case kRotationOriginTopEdge:
                surface.rotation_origin_x = 50.0F;
                surface.rotation_origin_y = 0.0F;
                break;
            case kRotationOriginBottomEdge:
                surface.rotation_origin_x = 50.0F;
                surface.rotation_origin_y = 100.0F;
                break;
            default:
                surface.rotation_origin_x = 50.0F;
                surface.rotation_origin_y = 50.0F;
                break;
        }
        surface.rotation_origin_mode = kRotationOriginCustom;
    }
    Point2 base;
    Point2 perturbed_x;
    Point2 perturbed_y;
    if (!ProjectRotationOriginToFrame(
            in_data, event_extra, surface, camera, base)) {
        return false;
    }
    constexpr float epsilon = static_cast<float>(kGizmoProbeStep);
    SurfaceData x_surface = surface;
    SurfaceData y_surface = surface;
    x_surface.rotation_origin_x += epsilon;
    y_surface.rotation_origin_y += epsilon;
    if (!ProjectRotationOriginToFrame(
            in_data, event_extra, x_surface, camera, perturbed_x) ||
        !ProjectRotationOriginToFrame(
            in_data, event_extra, y_surface, camera, perturbed_y)) {
        return false;
    }
    double delta_x = 0.0;
    double delta_y = 0.0;
    if (!SolveProjectedDelta(
            base,
            perturbed_x,
            perturbed_y,
            mouse,
            epsilon,
            epsilon,
            delta_x,
            delta_y)) {
        return false;
    }
    auto view = BuildAnimationParameterView(params, surface.animation_bank);
    PF_ParamDef* mode = view[kParamSurfaceRotationOriginMode];
    PF_ParamDef* origin_x = view[kParamSurfaceRotationOriginX];
    PF_ParamDef* origin_y = view[kParamSurfaceRotationOriginY];
    mode->u.pd.value = static_cast<A_long>(kRotationOriginCustom);
    origin_x->u.fs_d.value = std::clamp(
        static_cast<double>(surface.rotation_origin_x) + delta_x,
        -1000.0,
        1000.0);
    origin_y->u.fs_d.value = std::clamp(
        static_cast<double>(surface.rotation_origin_y) + delta_y,
        -1000.0,
        1000.0);
    mode->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
    origin_x->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
    origin_y->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
    return true;
}

bool DragCurlTip(
    PF_InData* in_data,
    PF_ParamDef* params[],
    PF_EventExtra* event_extra,
    SurfaceData surface,
    const CameraState& camera,
    std::uint32_t corner,
    const Point2& mouse) {
    if (corner < kCornerTopLeft || corner > kCornerBottomLeft) {
        return false;
    }
    CornerCurlData& curl = surface.corner_curls[corner - 1];
    Point2 base;
    Point2 perturbed_amount;
    Point2 perturbed_radius;
    if (!ProjectCurlTipHandle(
            in_data, event_extra, surface, camera, corner, base)) {
        return false;
    }
    const double amount_epsilon = ForwardEpsilon(curl.amount, 100.0);
    const double radius_epsilon = ForwardEpsilon(curl.radius, 100.0);
    SurfaceData amount_surface = surface;
    amount_surface.corner_curls[corner - 1].amount +=
        static_cast<float>(amount_epsilon);
    SurfaceData radius_surface = surface;
    radius_surface.corner_curls[corner - 1].radius +=
        static_cast<float>(radius_epsilon);
    if (!ProjectCurlTipHandle(
            in_data,
            event_extra,
            amount_surface,
            camera,
            corner,
            perturbed_amount) ||
        !ProjectCurlTipHandle(
            in_data,
            event_extra,
            radius_surface,
            camera,
            corner,
            perturbed_radius)) {
        return false;
    }
    double amount_delta = 0.0;
    double radius_delta = 0.0;
    if (!SolveProjectedDelta(
            base,
            perturbed_amount,
            perturbed_radius,
            mouse,
            amount_epsilon,
            radius_epsilon,
            amount_delta,
            radius_delta)) {
        return false;
    }
    auto view = BuildAnimationParameterView(params, surface.animation_bank);
    SetPopupParameter(
        view[kParamSurfaceCornerSelect],
        static_cast<A_long>(corner));
    SetFloatParameter(
        view[kCornerAmountParams[corner - 1]],
        std::clamp(
            static_cast<double>(curl.amount) + amount_delta,
            -100.0,
            100.0));
    SetFloatParameter(
        view[kCornerRadiusParams[corner - 1]],
        std::clamp(
            static_cast<double>(curl.radius) + radius_delta,
            0.1,
            100.0));
    return true;
}

bool DragCurlExtent(
    PF_InData* in_data,
    PF_ParamDef* params[],
    PF_EventExtra* event_extra,
    SurfaceData surface,
    const CameraState& camera,
    std::uint32_t corner,
    const Point2& mouse) {
    if (corner < kCornerTopLeft || corner > kCornerBottomLeft) {
        return false;
    }
    CornerCurlData& curl = surface.corner_curls[corner - 1];
    Point2 base;
    Point2 perturbed_length;
    Point2 perturbed_direction;
    if (!ProjectCurlExtentHandle(
            in_data, event_extra, surface, camera, corner, base)) {
        return false;
    }
    const double length_epsilon = ForwardEpsilon(curl.length, 150.0);
    const double direction_epsilon = ForwardEpsilon(curl.direction, 90.0);
    SurfaceData length_surface = surface;
    length_surface.corner_curls[corner - 1].length +=
        static_cast<float>(length_epsilon);
    SurfaceData direction_surface = surface;
    direction_surface.corner_curls[corner - 1].direction +=
        static_cast<float>(direction_epsilon);
    if (!ProjectCurlExtentHandle(
            in_data,
            event_extra,
            length_surface,
            camera,
            corner,
            perturbed_length) ||
        !ProjectCurlExtentHandle(
            in_data,
            event_extra,
            direction_surface,
            camera,
            corner,
            perturbed_direction)) {
        return false;
    }
    double length_delta = 0.0;
    double direction_delta = 0.0;
    if (!SolveProjectedDelta(
            base,
            perturbed_length,
            perturbed_direction,
            mouse,
            length_epsilon,
            direction_epsilon,
            length_delta,
            direction_delta)) {
        return false;
    }
    auto view = BuildAnimationParameterView(params, surface.animation_bank);
    SetPopupParameter(
        view[kParamSurfaceCornerSelect],
        static_cast<A_long>(corner));
    SetFloatParameter(
        view[kCornerLengthParams[corner - 1]],
        std::clamp(
            static_cast<double>(curl.length) + length_delta,
            0.0,
            150.0));
    SetFloatParameter(
        view[kCornerDirectionParams[corner - 1]],
        std::clamp(
            static_cast<double>(curl.direction) + direction_delta,
            0.0,
            90.0));
    return true;
}

bool DragRollHandle(
    PF_InData* in_data,
    PF_ParamDef* params[],
    PF_EventExtra* event_extra,
    SurfaceData surface,
    const CameraState& camera,
    bool length_handle,
    const Point2& mouse) {
    Point2 base;
    Point2 perturbed;
    if (!ProjectRollHandle(
            in_data,
            event_extra,
            surface,
            camera,
            length_handle,
            base)) {
        return false;
    }
    const double value = length_handle ? surface.roll_length
                                       : surface.roll_angle;
    const double maximum = length_handle ? 100.0 : 1080.0;
    const double epsilon = ForwardEpsilon(value, maximum);
    SurfaceData changed_surface = surface;
    if (length_handle) {
        changed_surface.roll_length += static_cast<float>(epsilon);
    } else {
        changed_surface.roll_angle += static_cast<float>(epsilon);
    }
    if (!ProjectRollHandle(
            in_data,
            event_extra,
            changed_surface,
            camera,
            length_handle,
            perturbed)) {
        return false;
    }
    double delta = 0.0;
    if (!SolveProjectedScalarDelta(
            base, perturbed, mouse, epsilon, delta)) {
        return false;
    }
    auto view = BuildAnimationParameterView(params, surface.animation_bank);
    SetPopupParameter(
        view[kParamSurfaceRollEdge],
        static_cast<A_long>(surface.roll_edge));
    if (length_handle) {
        SetFloatParameter(
            view[kParamSurfaceRollLength],
            std::clamp(value + delta, 0.0, 100.0));
    } else {
        SetFloatParameter(
            view[kParamSurfaceRollAngle],
            std::clamp(value + delta, -1080.0, 1080.0));
    }
    return true;
}

bool DragTwistHandle(
    PF_InData* in_data,
    PF_ParamDef* params[],
    PF_EventExtra* event_extra,
    SurfaceData surface,
    const CameraState& camera,
    std::uint32_t edge,
    bool falloff_handle,
    const Point2& mouse) {
    if (edge < kTwistEdgeLeft || edge > kTwistEdgeBottom) {
        return false;
    }
    const EdgeTwistData& twist = surface.edge_twists[edge - 1];
    Point2 base;
    Point2 perturbed;
    if (!ProjectTwistHandle(
            in_data,
            event_extra,
            surface,
            camera,
            edge,
            falloff_handle,
            base)) {
        return false;
    }
    const double value = falloff_handle ? twist.falloff : twist.angle;
    const double maximum = falloff_handle ? 100.0 : 180.0;
    const double epsilon = ForwardEpsilon(value, maximum);
    SurfaceData changed_surface = surface;
    if (falloff_handle) {
        changed_surface.edge_twists[edge - 1].falloff +=
            static_cast<float>(epsilon);
    } else {
        changed_surface.edge_twists[edge - 1].angle +=
            static_cast<float>(epsilon);
    }
    if (!ProjectTwistHandle(
            in_data,
            event_extra,
            changed_surface,
            camera,
            edge,
            falloff_handle,
            perturbed)) {
        return false;
    }
    double delta = 0.0;
    if (!SolveProjectedScalarDelta(
            base, perturbed, mouse, epsilon, delta)) {
        return false;
    }
    auto view = BuildAnimationParameterView(params, surface.animation_bank);
    SetPopupParameter(
        view[kParamSurfaceTwistEdge],
        static_cast<A_long>(edge));
    if (falloff_handle) {
        SetFloatParameter(
            view[kTwistFalloffParams[edge - 1]],
            std::clamp(value + delta, 1.0, 100.0));
    } else {
        SetFloatParameter(
            view[kTwistAngleParams[edge - 1]],
            std::clamp(value + delta, -180.0, 180.0));
    }
    return true;
}

PF_Err DrawSurfaceGizmo(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_EventExtra* event_extra,
    const SurfaceData& surface,
    const CameraState& camera,
    const GizmoVisibility& visibility) {
    PF_Err error = PF_Err_NONE;
    DRAWBOT_Suites drawbot_suites{};
    DRAWBOT_DrawRef drawing_ref = nullptr;
    DRAWBOT_SurfaceRef drawing_surface = nullptr;
    DRAWBOT_SupplierRef supplier = nullptr;
    error = AEFX_AcquireDrawbotSuites(in_data, out_data, &drawbot_suites);
    if (error != PF_Err_NONE) {
        return error;
    }
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    error = suites.EffectCustomUISuite1()->PF_GetDrawingReference(
        event_extra->contextH,
        &drawing_ref);
    if (error == PF_Err_NONE && drawing_ref) {
        error = suites.DrawbotSuiteCurrent()->GetSurface(
            drawing_ref,
            &drawing_surface);
    }
    if (error == PF_Err_NONE && drawing_ref) {
        error = drawbot_suites.drawbot_suiteP->GetSupplier(
            drawing_ref,
            &supplier);
    }
    if (error != PF_Err_NONE || !drawing_surface || !supplier) {
        AEFX_ReleaseDrawbotSuites(in_data, out_data);
        return error;
    }

    const DRAWBOT_ColorRGBA cage_color{0.15F, 0.78F, 1.0F, 0.88F};
    const DRAWBOT_ColorRGBA boundary_color{0.08F, 0.88F, 1.0F, 1.0F};
    const DRAWBOT_ColorRGBA point_color{0.92F, 0.96F, 1.0F, 1.0F};
    const DRAWBOT_ColorRGBA depth_color{0.05F, 0.48F, 0.72F, 1.0F};
    const DRAWBOT_ColorRGBA origin_color{1.0F, 0.58F, 0.12F, 1.0F};
    const DRAWBOT_ColorRGBA curl_color{1.0F, 0.30F, 0.58F, 1.0F};
    const DRAWBOT_ColorRGBA curl_extent_color{1.0F, 0.62F, 0.76F, 1.0F};
    const DRAWBOT_ColorRGBA roll_color{1.0F, 0.76F, 0.18F, 1.0F};
    const DRAWBOT_ColorRGBA roll_extent_color{1.0F, 0.90F, 0.48F, 1.0F};
    const DRAWBOT_ColorRGBA twist_color{0.70F, 0.42F, 1.0F, 1.0F};
    const DRAWBOT_ColorRGBA twist_extent_color{0.84F, 0.68F, 1.0F, 1.0F};
    const std::array<DRAWBOT_ColorRGBA, 3> transform_axis_colors{{
        {1.0F, 0.24F, 0.20F, 1.0F},
        {0.26F, 0.92F, 0.35F, 1.0F},
        {0.24F, 0.52F, 1.0F, 1.0F}}};

    if (visibility.controls || visibility.deform) {
        DRAWBOT_PathP cage_path(drawbot_suites.supplier_suiteP, supplier);
        for (int row = 0; row < 4; ++row) {
            bool started = false;
            for (int sample = 0; sample <= kGizmoCurveSamples; ++sample) {
                Point2 point;
                if (!ProjectSurfacePointToFrame(
                        in_data,
                        event_extra,
                        surface,
                        camera,
                        static_cast<double>(sample) / kGizmoCurveSamples,
                        static_cast<double>(row) / 3.0,
                        point)) {
                    started = false;
                    continue;
                }
                if (!started) {
                    drawbot_suites.path_suiteP->MoveTo(
                        cage_path,
                        static_cast<float>(point.x),
                        static_cast<float>(point.y));
                    started = true;
                } else {
                    drawbot_suites.path_suiteP->LineTo(
                        cage_path,
                        static_cast<float>(point.x),
                        static_cast<float>(point.y));
                }
            }
        }
        for (int column = 0; column < 4; ++column) {
            bool started = false;
            for (int sample = 0; sample <= kGizmoCurveSamples; ++sample) {
                Point2 point;
                if (!ProjectSurfacePointToFrame(
                        in_data,
                        event_extra,
                        surface,
                        camera,
                        static_cast<double>(column) / 3.0,
                        static_cast<double>(sample) / kGizmoCurveSamples,
                        point)) {
                    started = false;
                    continue;
                }
                if (!started) {
                    drawbot_suites.path_suiteP->MoveTo(
                        cage_path,
                        static_cast<float>(point.x),
                        static_cast<float>(point.y));
                    started = true;
                } else {
                    drawbot_suites.path_suiteP->LineTo(
                        cage_path,
                        static_cast<float>(point.x),
                        static_cast<float>(point.y));
                }
            }
        }
        DRAWBOT_PenP cage_pen(
            drawbot_suites.supplier_suiteP,
            supplier,
            &cage_color,
            1.0F);
        drawbot_suites.surface_suiteP->StrokePath(
            drawing_surface,
            cage_pen,
            cage_path);
    }

    {
        DRAWBOT_PathP boundary_path(drawbot_suites.supplier_suiteP, supplier);
        const std::array<std::array<double, 4>, 4> edges{{
            {{0.0, 0.0, 1.0, 0.0}},
            {{1.0, 0.0, 1.0, 1.0}},
            {{1.0, 1.0, 0.0, 1.0}},
            {{0.0, 1.0, 0.0, 0.0}}}};
        for (const auto& edge : edges) {
            bool started = false;
            for (int sample = 0; sample <= kGizmoCurveSamples; ++sample) {
                const double t =
                    static_cast<double>(sample) / kGizmoCurveSamples;
                Point2 point;
                if (!ProjectSurfacePointToFrame(
                        in_data,
                        event_extra,
                        surface,
                        camera,
                        edge[0] + (edge[2] - edge[0]) * t,
                        edge[1] + (edge[3] - edge[1]) * t,
                        point)) {
                    started = false;
                    continue;
                }
                if (!started) {
                    drawbot_suites.path_suiteP->MoveTo(
                        boundary_path,
                        static_cast<float>(point.x),
                        static_cast<float>(point.y));
                    started = true;
                } else {
                    drawbot_suites.path_suiteP->LineTo(
                        boundary_path,
                        static_cast<float>(point.x),
                        static_cast<float>(point.y));
                }
            }
        }
        DRAWBOT_PenP boundary_pen(
            drawbot_suites.supplier_suiteP,
            supplier,
            &boundary_color,
            2.0F);
        drawbot_suites.surface_suiteP->StrokePath(
            drawing_surface,
            boundary_pen,
            boundary_path);
    }

    if (visibility.controls) {
        DRAWBOT_PathP depth_axes(drawbot_suites.supplier_suiteP, supplier);
        std::array<Point2, 16> handles{};
        std::array<bool, 16> visible{};
        for (std::uint32_t index = 0; index < 16; ++index) {
            Point2 anchor;
            visible[index] = ProjectControlDepthHandle(
                in_data,
                event_extra,
                surface,
                camera,
                index,
                handles[index],
                &anchor);
            if (!visible[index]) {
                continue;
            }
            drawbot_suites.path_suiteP->MoveTo(
                depth_axes,
                static_cast<float>(anchor.x),
                static_cast<float>(anchor.y));
            drawbot_suites.path_suiteP->LineTo(
                depth_axes,
                static_cast<float>(handles[index].x),
                static_cast<float>(handles[index].y));
        }
        DRAWBOT_PenP depth_pen(
            drawbot_suites.supplier_suiteP,
            supplier,
            &depth_color,
            1.0F);
        drawbot_suites.surface_suiteP->StrokePath(
            drawing_surface,
            depth_pen,
            depth_axes);
        for (std::size_t index = 0; index < handles.size(); ++index) {
            if (!visible[index]) {
                continue;
            }
            DRAWBOT_RectF32 rect{
                static_cast<float>(handles[index].x) - 3.0F,
                static_cast<float>(handles[index].y) - 3.0F,
                6.0F,
                6.0F};
            suites.SurfaceSuiteCurrent()->PaintRect(
                drawing_surface,
                &depth_color,
                &rect);
        }
    }

    if (visibility.controls) {
        for (int row = 0; row < 4; ++row) {
            for (int column = 0; column < 4; ++column) {
            Point2 point;
            if (!ProjectSurfacePointToFrame(
                    in_data,
                    event_extra,
                    surface,
                    camera,
                    static_cast<double>(column) / 3.0,
                    static_cast<double>(row) / 3.0,
                    point)) {
                continue;
            }
            const bool corner =
                (row == 0 || row == 3) && (column == 0 || column == 3);
            const float radius = corner ? 4.5F : 3.5F;
            DRAWBOT_RectF32 rect{
                static_cast<float>(point.x) - radius,
                static_cast<float>(point.y) - radius,
                radius * 2.0F,
                radius * 2.0F};
            suites.SurfaceSuiteCurrent()->PaintRect(
                drawing_surface,
                &point_color,
                &rect);
            DRAWBOT_RectF32 depth_rect{
                static_cast<float>(point.x) - 1.25F,
                static_cast<float>(point.y) - 1.25F,
                2.5F,
                2.5F};
            suites.SurfaceSuiteCurrent()->PaintRect(
                drawing_surface,
                &depth_color,
                &depth_rect);
            }
        }
    }

    Point2 center;
    if (visibility.position && ProjectSurfacePointToFrame(
            in_data, event_extra, surface, camera, 0.5, 0.5, center)) {
        DRAWBOT_PathP center_path(drawbot_suites.supplier_suiteP, supplier);
        drawbot_suites.path_suiteP->MoveTo(
            center_path,
            static_cast<float>(center.x - 7.0),
            static_cast<float>(center.y));
        drawbot_suites.path_suiteP->LineTo(
            center_path,
            static_cast<float>(center.x + 7.0),
            static_cast<float>(center.y));
        drawbot_suites.path_suiteP->MoveTo(
            center_path,
            static_cast<float>(center.x),
            static_cast<float>(center.y - 7.0));
        drawbot_suites.path_suiteP->LineTo(
            center_path,
            static_cast<float>(center.x),
            static_cast<float>(center.y + 7.0));
        DRAWBOT_PenP center_pen(
            drawbot_suites.supplier_suiteP,
            supplier,
            &boundary_color,
            1.5F);
        drawbot_suites.surface_suiteP->StrokePath(
            drawing_surface,
            center_pen,
            center_path);
    }

    Point2 origin;
    if (visibility.rotation && ProjectRotationOriginToFrame(
            in_data, event_extra, surface, camera, origin)) {
        DRAWBOT_PathP origin_path(drawbot_suites.supplier_suiteP, supplier);
        drawbot_suites.path_suiteP->MoveTo(
            origin_path,
            static_cast<float>(origin.x),
            static_cast<float>(origin.y - 7.0));
        drawbot_suites.path_suiteP->LineTo(
            origin_path,
            static_cast<float>(origin.x + 7.0),
            static_cast<float>(origin.y));
        drawbot_suites.path_suiteP->LineTo(
            origin_path,
            static_cast<float>(origin.x),
            static_cast<float>(origin.y + 7.0));
        drawbot_suites.path_suiteP->LineTo(
            origin_path,
            static_cast<float>(origin.x - 7.0),
            static_cast<float>(origin.y));
        drawbot_suites.path_suiteP->LineTo(
            origin_path,
            static_cast<float>(origin.x),
            static_cast<float>(origin.y - 7.0));
        DRAWBOT_PenP origin_pen(
            drawbot_suites.supplier_suiteP,
            supplier,
            &origin_color,
            2.0F);
        drawbot_suites.surface_suiteP->StrokePath(
            drawing_surface,
            origin_pen,
            origin_path);
    }

    // Transform gizmo: filled squares scale along local XYZ; outer diamonds
    // rotate around XYZ at the current Rotation Origin.
    for (std::uint32_t axis = 0; axis < 3; ++axis) {
        Point2 scale_handle;
        Point2 scale_anchor;
        if (visibility.scale && ProjectScaleHandle(
                in_data,
                event_extra,
                surface,
                camera,
                axis,
                scale_handle,
                &scale_anchor)) {
            DRAWBOT_PathP axis_path(drawbot_suites.supplier_suiteP, supplier);
            drawbot_suites.path_suiteP->MoveTo(
                axis_path,
                static_cast<float>(scale_anchor.x),
                static_cast<float>(scale_anchor.y));
            drawbot_suites.path_suiteP->LineTo(
                axis_path,
                static_cast<float>(scale_handle.x),
                static_cast<float>(scale_handle.y));
            DRAWBOT_PenP axis_pen(
                drawbot_suites.supplier_suiteP,
                supplier,
                &transform_axis_colors[axis],
                2.0F);
            drawbot_suites.surface_suiteP->StrokePath(
                drawing_surface,
                axis_pen,
                axis_path);
            DRAWBOT_RectF32 rect{
                static_cast<float>(scale_handle.x) - 4.0F,
                static_cast<float>(scale_handle.y) - 4.0F,
                8.0F,
                8.0F};
            suites.SurfaceSuiteCurrent()->PaintRect(
                drawing_surface,
                &transform_axis_colors[axis],
                &rect);
        }

        Point2 rotation_handle;
        Point2 rotation_anchor;
        if (visibility.rotation && ProjectRotationHandle(
                in_data,
                event_extra,
                surface,
                camera,
                axis,
                rotation_handle,
                &rotation_anchor)) {
            DRAWBOT_PathP rotation_path(
                drawbot_suites.supplier_suiteP,
                supplier);
            drawbot_suites.path_suiteP->MoveTo(
                rotation_path,
                static_cast<float>(rotation_anchor.x),
                static_cast<float>(rotation_anchor.y));
            drawbot_suites.path_suiteP->LineTo(
                rotation_path,
                static_cast<float>(rotation_handle.x),
                static_cast<float>(rotation_handle.y));
            drawbot_suites.path_suiteP->MoveTo(
                rotation_path,
                static_cast<float>(rotation_handle.x),
                static_cast<float>(rotation_handle.y - 5.5));
            drawbot_suites.path_suiteP->LineTo(
                rotation_path,
                static_cast<float>(rotation_handle.x + 5.5),
                static_cast<float>(rotation_handle.y));
            drawbot_suites.path_suiteP->LineTo(
                rotation_path,
                static_cast<float>(rotation_handle.x),
                static_cast<float>(rotation_handle.y + 5.5));
            drawbot_suites.path_suiteP->LineTo(
                rotation_path,
                static_cast<float>(rotation_handle.x - 5.5),
                static_cast<float>(rotation_handle.y));
            drawbot_suites.path_suiteP->LineTo(
                rotation_path,
                static_cast<float>(rotation_handle.x),
                static_cast<float>(rotation_handle.y - 5.5));
            DRAWBOT_PenP rotation_pen(
                drawbot_suites.supplier_suiteP,
                supplier,
                &transform_axis_colors[axis],
                1.5F);
            drawbot_suites.surface_suiteP->StrokePath(
                drawing_surface,
                rotation_pen,
                rotation_path);
        }
    }

    // Curl handles: the larger outer square edits tip height/radius, while the
    // smaller inner square edits the fold reach/direction.
    if (visibility.deform) {
        DRAWBOT_PathP leaders(drawbot_suites.supplier_suiteP, supplier);
        std::array<Point2, 4> tips{};
        std::array<Point2, 4> extents{};
        std::array<bool, 4> tip_visible{};
        std::array<bool, 4> extent_visible{};
        for (std::uint32_t corner = kCornerTopLeft;
             corner <= kCornerBottomLeft;
             ++corner) {
            Point2 anchor;
            tip_visible[corner - 1] = ProjectCurlTipHandle(
                in_data,
                event_extra,
                surface,
                camera,
                corner,
                tips[corner - 1],
                &anchor);
            extent_visible[corner - 1] = ProjectCurlExtentHandle(
                in_data,
                event_extra,
                surface,
                camera,
                corner,
                extents[corner - 1]);
            if (tip_visible[corner - 1]) {
                drawbot_suites.path_suiteP->MoveTo(
                    leaders,
                    static_cast<float>(anchor.x),
                    static_cast<float>(anchor.y));
                drawbot_suites.path_suiteP->LineTo(
                    leaders,
                    static_cast<float>(tips[corner - 1].x),
                    static_cast<float>(tips[corner - 1].y));
            }
            if (tip_visible[corner - 1] && extent_visible[corner - 1]) {
                drawbot_suites.path_suiteP->MoveTo(
                    leaders,
                    static_cast<float>(tips[corner - 1].x),
                    static_cast<float>(tips[corner - 1].y));
                drawbot_suites.path_suiteP->LineTo(
                    leaders,
                    static_cast<float>(extents[corner - 1].x),
                    static_cast<float>(extents[corner - 1].y));
            }
        }
        DRAWBOT_PenP pen(
            drawbot_suites.supplier_suiteP,
            supplier,
            &curl_extent_color,
            1.25F);
        drawbot_suites.surface_suiteP->StrokePath(
            drawing_surface, pen, leaders);
        for (std::size_t index = 0; index < tips.size(); ++index) {
            if (tip_visible[index]) {
                DRAWBOT_RectF32 rect{
                    static_cast<float>(tips[index].x) - 5.0F,
                    static_cast<float>(tips[index].y) - 5.0F,
                    10.0F,
                    10.0F};
                suites.SurfaceSuiteCurrent()->PaintRect(
                    drawing_surface, &curl_color, &rect);
            }
            if (extent_visible[index]) {
                DRAWBOT_RectF32 rect{
                    static_cast<float>(extents[index].x) - 3.5F,
                    static_cast<float>(extents[index].y) - 3.5F,
                    7.0F,
                    7.0F};
                suites.SurfaceSuiteCurrent()->PaintRect(
                    drawing_surface, &curl_extent_color, &rect);
            }
        }
    }

    // Roll handles live on the selected roll edge. The edge square controls
    // angle and the inner square controls how far the roll reaches.
    if (visibility.deform) {
        Point2 angle_handle;
        Point2 length_handle;
        const bool angle_visible = ProjectRollHandle(
            in_data,
            event_extra,
            surface,
            camera,
            false,
            angle_handle);
        const bool length_visible = ProjectRollHandle(
            in_data,
            event_extra,
            surface,
            camera,
            true,
            length_handle);
        if (angle_visible && length_visible) {
            DRAWBOT_PathP leader(drawbot_suites.supplier_suiteP, supplier);
            drawbot_suites.path_suiteP->MoveTo(
                leader,
                static_cast<float>(angle_handle.x),
                static_cast<float>(angle_handle.y));
            drawbot_suites.path_suiteP->LineTo(
                leader,
                static_cast<float>(length_handle.x),
                static_cast<float>(length_handle.y));
            DRAWBOT_PenP pen(
                drawbot_suites.supplier_suiteP,
                supplier,
                &roll_extent_color,
                1.5F);
            drawbot_suites.surface_suiteP->StrokePath(
                drawing_surface, pen, leader);
        }
        if (angle_visible) {
            DRAWBOT_RectF32 rect{
                static_cast<float>(angle_handle.x) - 5.0F,
                static_cast<float>(angle_handle.y) - 5.0F,
                10.0F,
                10.0F};
            suites.SurfaceSuiteCurrent()->PaintRect(
                drawing_surface, &roll_color, &rect);
        }
        if (length_visible) {
            DRAWBOT_RectF32 rect{
                static_cast<float>(length_handle.x) - 3.5F,
                static_cast<float>(length_handle.y) - 3.5F,
                7.0F,
                7.0F};
            suites.SurfaceSuiteCurrent()->PaintRect(
                drawing_surface, &roll_extent_color, &rect);
        }
    }

    // Every edge has an independent Twist pair: edge square = angle,
    // inner square = falloff.
    if (visibility.deform) {
        for (std::uint32_t edge = kTwistEdgeLeft;
             edge <= kTwistEdgeBottom;
             ++edge) {
        Point2 angle_handle;
        Point2 falloff_handle;
        const bool angle_visible = ProjectTwistHandle(
            in_data,
            event_extra,
            surface,
            camera,
            edge,
            false,
            angle_handle);
        const bool falloff_visible = ProjectTwistHandle(
            in_data,
            event_extra,
            surface,
            camera,
            edge,
            true,
            falloff_handle);
        if (angle_visible && falloff_visible) {
            DRAWBOT_PathP leader(drawbot_suites.supplier_suiteP, supplier);
            drawbot_suites.path_suiteP->MoveTo(
                leader,
                static_cast<float>(angle_handle.x),
                static_cast<float>(angle_handle.y));
            drawbot_suites.path_suiteP->LineTo(
                leader,
                static_cast<float>(falloff_handle.x),
                static_cast<float>(falloff_handle.y));
            DRAWBOT_PenP pen(
                drawbot_suites.supplier_suiteP,
                supplier,
                &twist_extent_color,
                1.25F);
            drawbot_suites.surface_suiteP->StrokePath(
                drawing_surface, pen, leader);
        }
        if (angle_visible) {
            DRAWBOT_RectF32 rect{
                static_cast<float>(angle_handle.x) - 4.5F,
                static_cast<float>(angle_handle.y) - 4.5F,
                9.0F,
                9.0F};
            suites.SurfaceSuiteCurrent()->PaintRect(
                drawing_surface, &twist_color, &rect);
        }
        if (falloff_visible) {
            DRAWBOT_RectF32 rect{
                static_cast<float>(falloff_handle.x) - 3.0F,
                static_cast<float>(falloff_handle.y) - 3.0F,
                6.0F,
                6.0F};
            suites.SurfaceSuiteCurrent()->PaintRect(
                drawing_surface, &twist_extent_color, &rect);
        }
        }
    }

    AEFX_ReleaseDrawbotSuites(in_data, out_data);
    return error;
}

}  // namespace

namespace {

constexpr A_short kSurfaceListUiWidth = 280;
constexpr A_short kSurfaceListRowHeight = 22;
constexpr A_short kSurfaceListUiHeight =
    static_cast<A_short>(kMaximumSurfaces * kSurfaceListRowHeight);
constexpr float kSurfaceListVisibilityWidth = 28.0F;

DRAWBOT_ColorRGBA AppUiColor(
    AEGP_SuiteHandler& suites,
    PF_App_ColorType type,
    DRAWBOT_ColorRGBA fallback) {
    PF_App_Color color{};
    if (suites.AppSuite6()->PF_AppGetColor(type, &color) == PF_Err_NONE) {
        constexpr float kChannelScale = 1.0F / 65535.0F;
        return {
            static_cast<float>(color.red) * kChannelScale,
            static_cast<float>(color.green) * kChannelScale,
            static_cast<float>(color.blue) * kChannelScale,
            1.0F};
    }
    return fallback;
}

std::array<DRAWBOT_UTF16Char, 64> SurfaceListText(
    std::uint32_t row,
    std::uint32_t surface_id) {
    char ascii[64];
    std::snprintf(
        ascii,
        sizeof(ascii),
        "%u   Surface %u",
        row + 1,
        surface_id);
    std::array<DRAWBOT_UTF16Char, 64> text{};
    for (std::size_t index = 0;
         index + 1 < text.size() && ascii[index] != '\0';
         ++index) {
        text[index] = static_cast<DRAWBOT_UTF16Char>(
            static_cast<unsigned char>(ascii[index]));
    }
    return text;
}

void DrawSurfaceVisibilityIcon(
    const DRAWBOT_Suites& drawbot_suites,
    DRAWBOT_SurfaceRef surface,
    DRAWBOT_SupplierRef supplier,
    float center_x,
    float center_y,
    bool visible,
    const DRAWBOT_ColorRGBA& color) {
    DRAWBOT_PathP outline(drawbot_suites.supplier_suiteP, supplier);
    drawbot_suites.path_suiteP->MoveTo(
        outline,
        center_x - 7.0F,
        center_y);
    const DRAWBOT_PointF32 top_control_1{
        center_x - 3.5F,
        center_y - 5.0F};
    const DRAWBOT_PointF32 top_control_2{
        center_x + 3.5F,
        center_y - 5.0F};
    const DRAWBOT_PointF32 right{center_x + 7.0F, center_y};
    drawbot_suites.path_suiteP->BezierTo(
        outline,
        &top_control_1,
        &top_control_2,
        &right);
    const DRAWBOT_PointF32 bottom_control_1{
        center_x + 3.5F,
        center_y + 5.0F};
    const DRAWBOT_PointF32 bottom_control_2{
        center_x - 3.5F,
        center_y + 5.0F};
    const DRAWBOT_PointF32 left{center_x - 7.0F, center_y};
    drawbot_suites.path_suiteP->BezierTo(
        outline,
        &bottom_control_1,
        &bottom_control_2,
        &left);
    DRAWBOT_PenP pen(
        drawbot_suites.supplier_suiteP,
        supplier,
        &color,
        1.25F);
    drawbot_suites.surface_suiteP->StrokePath(surface, pen, outline);

    DRAWBOT_PathP pupil(drawbot_suites.supplier_suiteP, supplier);
    const DRAWBOT_PointF32 center{center_x, center_y};
    drawbot_suites.path_suiteP->AddArc(
        pupil,
        &center,
        visible ? 2.25F : 1.5F,
        0.0F,
        360.0F);
    DRAWBOT_BrushP brush(
        drawbot_suites.supplier_suiteP,
        supplier,
        &color);
    drawbot_suites.surface_suiteP->FillPath(
        surface,
        brush,
        pupil,
        kDRAWBOT_FillType_Default);

    if (!visible) {
        DRAWBOT_PathP slash(drawbot_suites.supplier_suiteP, supplier);
        drawbot_suites.path_suiteP->MoveTo(
            slash,
            center_x - 6.0F,
            center_y + 5.0F);
        drawbot_suites.path_suiteP->LineTo(
            slash,
            center_x + 6.0F,
            center_y - 5.0F);
        drawbot_suites.surface_suiteP->StrokePath(surface, pen, slash);
    }
}

PF_Err DrawSurfaceList(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_ParamDef* params[],
    PF_EventExtra* event_extra) {
    SceneData scene{};
    PF_Handle scene_handle = params[kParamSceneData]->u.arb_d.value;
    if (!scene_handle) {
        return PF_Err_NONE;
    }
    const auto* scene_data =
        static_cast<const SceneData*>(PF_LOCK_HANDLE(scene_handle));
    if (!scene_data) {
        return PF_Err_OUT_OF_MEMORY;
    }
    if (IsValidScene(*scene_data)) {
        scene = *scene_data;
    } else {
        InitializeScene(scene, in_data->width, in_data->height);
    }
    PF_UNLOCK_HANDLE(scene_handle);

    PF_Err error = PF_Err_NONE;
    DRAWBOT_Suites drawbot_suites{};
    error = AEFX_AcquireDrawbotSuites(
        in_data,
        out_data,
        &drawbot_suites);
    if (error != PF_Err_NONE) {
        return error;
    }

    AEGP_SuiteHandler suites(in_data->pica_basicP);
    DRAWBOT_DrawRef drawing_ref = nullptr;
    DRAWBOT_SurfaceRef drawing_surface = nullptr;
    DRAWBOT_SupplierRef supplier = nullptr;
    error = suites.EffectCustomUISuite1()->PF_GetDrawingReference(
        event_extra->contextH,
        &drawing_ref);
    if (error == PF_Err_NONE && drawing_ref) {
        error = drawbot_suites.drawbot_suiteP->GetSurface(
            drawing_ref,
            &drawing_surface);
    }
    if (error == PF_Err_NONE && drawing_ref) {
        error = drawbot_suites.drawbot_suiteP->GetSupplier(
            drawing_ref,
            &supplier);
    }
    if (error != PF_Err_NONE || !drawing_surface || !supplier) {
        AEFX_ReleaseDrawbotSuites(in_data, out_data);
        return error;
    }

    try {
        const float left =
            static_cast<float>(event_extra->effect_win.current_frame.left);
        const float top =
            static_cast<float>(event_extra->effect_win.current_frame.top);
        const float width = static_cast<float>(
            event_extra->effect_win.current_frame.right -
            event_extra->effect_win.current_frame.left);
        const float height = static_cast<float>(
            event_extra->effect_win.current_frame.bottom -
            event_extra->effect_win.current_frame.top);
        const DRAWBOT_ColorRGBA background = AppUiColor(
            suites,
            PF_App_Color_FILL,
            {0.12F, 0.12F, 0.12F, 1.0F});
        const DRAWBOT_ColorRGBA text_color = AppUiColor(
            suites,
            PF_App_Color_TEXT,
            {0.86F, 0.86F, 0.86F, 1.0F});
        const DRAWBOT_ColorRGBA disabled_color = AppUiColor(
            suites,
            PF_App_Color_TEXT_DISABLED,
            {0.42F, 0.42F, 0.42F, 1.0F});
        const DRAWBOT_ColorRGBA frame_color = AppUiColor(
            suites,
            PF_App_Color_FRAME,
            {0.05F, 0.05F, 0.05F, 1.0F});
        const DRAWBOT_ColorRGBA selected_color{
            0.18F,
            0.42F,
            0.68F,
            1.0F};
        const DRAWBOT_ColorRGBA alternate_color{
            std::min(1.0F, background.red + 0.025F),
            std::min(1.0F, background.green + 0.025F),
            std::min(1.0F, background.blue + 0.025F),
            1.0F};

        const DRAWBOT_RectF32 background_rect{left, top, width, height};
        suites.SurfaceSuiteCurrent()->PaintRect(
            drawing_surface,
            &background,
            &background_rect);

        float default_font_size = 11.0F;
        drawbot_suites.supplier_suiteP->GetDefaultFontSize(
            supplier,
            &default_font_size);
        DRAWBOT_FontP font(
            drawbot_suites.supplier_suiteP,
            supplier,
            std::max(10.0F, default_font_size));
        DRAWBOT_BrushP text_brush(
            drawbot_suites.supplier_suiteP,
            supplier,
            &text_color);

        for (std::uint32_t row = 0; row < kMaximumSurfaces; ++row) {
            const float row_top =
                top + static_cast<float>(row * kSurfaceListRowHeight);
            DRAWBOT_RectF32 row_rect{
                left,
                row_top,
                width,
                static_cast<float>(kSurfaceListRowHeight)};
            if (row < scene.surface_count &&
                row == scene.selected_surface) {
                suites.SurfaceSuiteCurrent()->PaintRect(
                    drawing_surface,
                    &selected_color,
                    &row_rect);
            } else if ((row & 1U) != 0U) {
                suites.SurfaceSuiteCurrent()->PaintRect(
                    drawing_surface,
                    &alternate_color,
                    &row_rect);
            }

            DRAWBOT_PathP separator(
                drawbot_suites.supplier_suiteP,
                supplier);
            drawbot_suites.path_suiteP->MoveTo(
                separator,
                left,
                row_top + static_cast<float>(kSurfaceListRowHeight));
            drawbot_suites.path_suiteP->LineTo(
                separator,
                left + width,
                row_top + static_cast<float>(kSurfaceListRowHeight));
            DRAWBOT_PenP separator_pen(
                drawbot_suites.supplier_suiteP,
                supplier,
                &frame_color,
                1.0F);
            drawbot_suites.surface_suiteP->StrokePath(
                drawing_surface,
                separator_pen,
                separator);

            if (row >= scene.surface_count) {
                continue;
            }
            const SurfaceData& surface = scene.surfaces[row];
            const bool visible = surface.enabled != 0;
            DrawSurfaceVisibilityIcon(
                drawbot_suites,
                drawing_surface,
                supplier,
                left + 14.0F,
                row_top + static_cast<float>(kSurfaceListRowHeight) * 0.5F,
                visible,
                visible ? text_color : disabled_color);

            const auto label = SurfaceListText(row, surface.id);
            const DRAWBOT_PointF32 origin{
                left + kSurfaceListVisibilityWidth + 4.0F,
                row_top + static_cast<float>(kSurfaceListRowHeight) * 0.72F};
            if (visible) {
                drawbot_suites.surface_suiteP->DrawString(
                    drawing_surface,
                    text_brush,
                    font,
                    label.data(),
                    &origin,
                    kDRAWBOT_TextAlignment_Default,
                    kDRAWBOT_TextTruncation_End,
                    std::max(
                        0.0F,
                        width - kSurfaceListVisibilityWidth - 8.0F));
            } else {
                DRAWBOT_BrushP disabled_brush(
                    drawbot_suites.supplier_suiteP,
                    supplier,
                    &disabled_color);
                drawbot_suites.surface_suiteP->DrawString(
                    drawing_surface,
                    disabled_brush,
                    font,
                    label.data(),
                    &origin,
                    kDRAWBOT_TextAlignment_Default,
                    kDRAWBOT_TextTruncation_End,
                    std::max(
                        0.0F,
                        width - kSurfaceListVisibilityWidth - 8.0F));
            }
        }
        drawbot_suites.surface_suiteP->Flush(drawing_surface);
    } catch (const DRAWBOT_Exception& draw_error) {
        error = static_cast<PF_Err>(draw_error.mErr);
    }

    const PF_Err release_error =
        AEFX_ReleaseDrawbotSuites(in_data, out_data);
    if (error == PF_Err_NONE) {
        error = release_error;
    }
    if (error == PF_Err_NONE) {
        event_extra->evt_out_flags = PF_EO_HANDLED_EVENT;
    }
    return error;
}

PF_Err HandleSurfaceListEvent(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_ParamDef* params[],
    PF_EventExtra* event_extra) {
    if (event_extra->effect_win.area != PF_EA_CONTROL ||
        event_extra->effect_win.index != kParamSurfaceCount) {
        return PF_Err_NONE;
    }
    if (event_extra->e_type == PF_Event_DRAW) {
        return DrawSurfaceList(
            in_data,
            out_data,
            params,
            event_extra);
    }
    if (event_extra->e_type == PF_Event_ADJUST_CURSOR) {
        event_extra->u.adjust_cursor.set_cursor = PF_Cursor_FINGER_POINTER;
        event_extra->evt_out_flags = PF_EO_HANDLED_EVENT;
        return PF_Err_NONE;
    }
    if (event_extra->e_type != PF_Event_DO_CLICK) {
        return PF_Err_NONE;
    }

    const PF_UnionableRect& frame =
        event_extra->effect_win.current_frame;
    const PF_Point mouse = event_extra->u.do_click.screen_point;
    const float width = static_cast<float>(frame.right - frame.left);
    const float height = static_cast<float>(frame.bottom - frame.top);
    const float mouse_x =
        mouse.h >= frame.left && mouse.h <= frame.right
            ? static_cast<float>(mouse.h - frame.left)
            : static_cast<float>(mouse.h);
    const float mouse_y =
        mouse.v >= frame.top && mouse.v <= frame.bottom
            ? static_cast<float>(mouse.v - frame.top)
            : static_cast<float>(mouse.v);
    if (mouse_x < 0.0F || mouse_x > width ||
        mouse_y < 0.0F || mouse_y >= height) {
        return PF_Err_NONE;
    }
    const std::uint32_t row = static_cast<std::uint32_t>(
        mouse_y / static_cast<float>(kSurfaceListRowHeight));

    PF_Handle scene_handle = params[kParamSceneData]->u.arb_d.value;
    if (!scene_handle) {
        return PF_Err_NONE;
    }
    auto* scene = static_cast<SceneData*>(PF_LOCK_HANDLE(scene_handle));
    if (!scene) {
        return PF_Err_OUT_OF_MEMORY;
    }
    if (!IsValidScene(*scene)) {
        InitializeScene(*scene, in_data->width, in_data->height);
    }
    bool changed = false;
    if (row < scene->surface_count) {
        if (mouse_x < kSurfaceListVisibilityWidth) {
            SurfaceData& surface = scene->surfaces[row];
            surface.enabled = surface.enabled == 0 ? 1U : 0U;
            changed = true;
        } else if (row != scene->selected_surface) {
            CaptureSurfaceAtCurrentFrame(
                params,
                scene->surfaces[scene->selected_surface]);
            scene->selected_surface = row;
            UpdateSurfaceUiParams(params, *scene);
            changed = true;
        }
    }
    PF_UNLOCK_HANDLE(scene_handle);

    event_extra->evt_out_flags = PF_EO_HANDLED_EVENT;
    if (changed) {
        params[kParamSceneData]->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
        out_data->out_flags |= PF_OutFlag_REFRESH_UI;
        event_extra->evt_out_flags = static_cast<PF_EventOutFlags>(
            PF_EO_HANDLED_EVENT | PF_EO_ALWAYS_UPDATE | PF_EO_UPDATE_NOW);
    }
    return PF_Err_NONE;
}

}  // namespace

PF_Err HandleSurfaceGizmoEvent(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_ParamDef* params[],
    PF_EventExtra* event_extra) {
    if (!event_extra ||
        (event_extra->e_type != PF_Event_DRAW &&
         event_extra->e_type != PF_Event_DO_CLICK &&
         event_extra->e_type != PF_Event_DRAG &&
         event_extra->e_type != PF_Event_ADJUST_CURSOR)) {
        return PF_Err_NONE;
    }
    if (!event_extra->contextH || !params ||
        !params[kParamInput] || !params[kParamSceneData]) {
        return PF_Err_NONE;
    }
    if ((*event_extra->contextH)->w_type == PF_Window_EFFECT) {
        return HandleSurfaceListEvent(
            in_data,
            out_data,
            params,
            event_extra);
    }
    if ((*event_extra->contextH)->w_type != PF_Window_COMP) {
        return PF_Err_NONE;
    }
    if (event_extra->e_type == PF_Event_ADJUST_CURSOR) {
        return PF_Err_NONE;
    }
    const A_long input_width =
        params[kParamInput]->u.ld.width > 0
            ? params[kParamInput]->u.ld.width
            : in_data->width;
    const A_long input_height =
        params[kParamInput]->u.ld.height > 0
            ? params[kParamInput]->u.ld.height
            : in_data->height;
    SceneData scene =
        ResolveSceneForFrame(in_data, params, input_width, input_height);
    if (scene.surface_count == 0 ||
        scene.selected_surface >= scene.surface_count) {
        return PF_Err_NONE;
    }
    SurfaceData surface = scene.surfaces[scene.selected_surface];
    const CameraState camera =
        BuildGizmoCamera(in_data, params, input_width, input_height);
    const GizmoVisibility visibility = ResolveGizmoVisibility(params);

    if (event_extra->e_type == PF_Event_DRAW) {
        const PF_Err error = DrawSurfaceGizmo(
            in_data,
            out_data,
            event_extra,
            surface,
            camera,
            visibility);
        if (error == PF_Err_NONE) {
            event_extra->evt_out_flags = PF_EO_HANDLED_EVENT;
        }
        return error;
    }

    const Point2 mouse{
        static_cast<double>(event_extra->u.do_click.screen_point.h),
        static_cast<double>(event_extra->u.do_click.screen_point.v)};
    if (event_extra->e_type == PF_Event_DO_CLICK) {
        std::vector<GizmoHandleHit> transform_handles;
        transform_handles.reserve(6);
        for (std::uint32_t axis = 0; axis < 3; ++axis) {
            Point2 transform_point;
            if (visibility.scale && ProjectScaleHandle(
                    in_data,
                    event_extra,
                    surface,
                    camera,
                    axis,
                    transform_point)) {
                transform_handles.push_back(
                    {kGizmoDragSurfaceScale, axis, transform_point});
            }
            if (visibility.rotation && ProjectRotationHandle(
                    in_data,
                    event_extra,
                    surface,
                    camera,
                    axis,
                    transform_point)) {
                transform_handles.push_back(
                    {kGizmoDragSurfaceRotation, axis, transform_point});
            }
        }
        const GizmoHandleHit* closest_transform = nullptr;
        double closest_transform_distance =
            kGizmoTransformHitRadius * kGizmoTransformHitRadius;
        for (const GizmoHandleHit& handle : transform_handles) {
            const double distance = SquaredDistance(handle.point, mouse);
            if (distance <= closest_transform_distance) {
                closest_transform_distance = distance;
                closest_transform = &handle;
            }
        }

        std::vector<GizmoHandleHit> deform_handles;
        deform_handles.reserve(18);
        for (std::uint32_t corner = kCornerTopLeft;
             corner <= kCornerBottomLeft;
             ++corner) {
            Point2 point;
            if (visibility.deform && ProjectCurlTipHandle(
                    in_data,
                    event_extra,
                    surface,
                    camera,
                    corner,
                    point)) {
                deform_handles.push_back(
                    {kGizmoDragCurlTip, corner, point});
            }
            if (visibility.deform && ProjectCurlExtentHandle(
                    in_data,
                    event_extra,
                    surface,
                    camera,
                    corner,
                    point)) {
                deform_handles.push_back(
                    {kGizmoDragCurlExtent, corner, point});
            }
        }
        Point2 point;
        if (visibility.deform && ProjectRollHandle(
                in_data,
                event_extra,
                surface,
                camera,
                false,
                point)) {
            deform_handles.push_back(
                {kGizmoDragRollAngle, surface.roll_edge, point});
        }
        if (visibility.deform && ProjectRollHandle(
                in_data,
                event_extra,
                surface,
                camera,
                true,
                point)) {
            deform_handles.push_back(
                {kGizmoDragRollLength, surface.roll_edge, point});
        }
        for (std::uint32_t edge = kTwistEdgeLeft;
             edge <= kTwistEdgeBottom;
             ++edge) {
            if (visibility.deform && ProjectTwistHandle(
                    in_data,
                    event_extra,
                    surface,
                    camera,
                    edge,
                    false,
                    point)) {
                deform_handles.push_back(
                    {kGizmoDragTwistAngle, edge, point});
            }
            if (visibility.deform && ProjectTwistHandle(
                    in_data,
                    event_extra,
                    surface,
                    camera,
                    edge,
                    true,
                    point)) {
                deform_handles.push_back(
                    {kGizmoDragTwistFalloff, edge, point});
            }
        }
        const GizmoHandleHit* closest_deform = nullptr;
        double closest_deform_distance =
            kGizmoDeformHitRadius * kGizmoDeformHitRadius;
        for (const GizmoHandleHit& handle : deform_handles) {
            const double distance = SquaredDistance(handle.point, mouse);
            if (distance <= closest_deform_distance) {
                closest_deform_distance = distance;
                closest_deform = &handle;
            }
        }

        double closest_distance =
            kGizmoControlHitRadius * kGizmoControlHitRadius;
        double closest_depth_distance =
            kGizmoDepthHitRadius * kGizmoDepthHitRadius;
        int closest_control = -1;
        int closest_depth_control = -1;
        if (visibility.controls) {
            for (int row = 0; row < 4; ++row) {
                for (int column = 0; column < 4; ++column) {
                const int control_index = row * 4 + column;
                Point2 point;
                if (!ProjectSurfacePointToFrame(
                        in_data,
                        event_extra,
                        surface,
                        camera,
                        static_cast<double>(column) / 3.0,
                        static_cast<double>(row) / 3.0,
                        point)) {
                    continue;
                }
                const double distance = SquaredDistance(point, mouse);
                if (distance <= closest_distance) {
                    closest_distance = distance;
                    closest_control = control_index;
                }
                Point2 depth_handle;
                if (ProjectControlDepthHandle(
                        in_data,
                        event_extra,
                        surface,
                        camera,
                        static_cast<std::uint32_t>(control_index),
                        depth_handle)) {
                    const double depth_distance =
                        SquaredDistance(depth_handle, mouse);
                    if (depth_distance <= closest_depth_distance) {
                        closest_depth_distance = depth_distance;
                        closest_depth_control = control_index;
                    }
                }
            }
        }
        }
        Point2 origin;
        const bool hit_origin =
            visibility.rotation && ProjectRotationOriginToFrame(
                in_data, event_extra, surface, camera, origin) &&
            SquaredDistance(origin, mouse) <=
                kGizmoOriginHitRadius * kGizmoOriginHitRadius;
        const bool depth_modifier =
            (event_extra->u.do_click.modifiers & PF_Mod_OPT_ALT_KEY) != 0;
        if (depth_modifier && closest_control >= 0) {
            event_extra->u.do_click.continue_refcon[0] =
                kGizmoDragControlDepth;
            event_extra->u.do_click.continue_refcon[1] = closest_control;
            event_extra->u.do_click.continue_refcon[2] =
                static_cast<A_intptr_t>(event_extra->u.do_click.screen_point.h);
            event_extra->u.do_click.continue_refcon[3] =
                static_cast<A_intptr_t>(event_extra->u.do_click.screen_point.v);
        } else if (closest_depth_control >= 0) {
            event_extra->u.do_click.continue_refcon[0] =
                kGizmoDragControlDepth;
            event_extra->u.do_click.continue_refcon[1] = closest_depth_control;
            event_extra->u.do_click.continue_refcon[2] =
                static_cast<A_intptr_t>(event_extra->u.do_click.screen_point.h);
            event_extra->u.do_click.continue_refcon[3] =
                static_cast<A_intptr_t>(event_extra->u.do_click.screen_point.v);
        } else if (closest_transform) {
            event_extra->u.do_click.continue_refcon[0] =
                closest_transform->target;
            event_extra->u.do_click.continue_refcon[1] =
                static_cast<A_intptr_t>(closest_transform->index);
            event_extra->u.do_click.continue_refcon[2] =
                static_cast<A_intptr_t>(event_extra->u.do_click.screen_point.h);
            event_extra->u.do_click.continue_refcon[3] =
                static_cast<A_intptr_t>(event_extra->u.do_click.screen_point.v);
        } else if (closest_deform) {
            event_extra->u.do_click.continue_refcon[0] =
                closest_deform->target;
            event_extra->u.do_click.continue_refcon[1] =
                static_cast<A_intptr_t>(closest_deform->index);
        } else if (hit_origin) {
            event_extra->u.do_click.continue_refcon[0] =
                kGizmoDragRotationOrigin;
        } else if (closest_control >= 0) {
            event_extra->u.do_click.continue_refcon[0] =
                kGizmoDragControlPoint;
            event_extra->u.do_click.continue_refcon[1] = closest_control;
        } else if (visibility.position && HitProjectedSurface(
                       in_data,
                       event_extra,
                       surface,
                       camera,
                       mouse)) {
            event_extra->u.do_click.continue_refcon[0] = kGizmoDragSurface;
        } else {
            return PF_Err_NONE;
        }
        event_extra->u.do_click.send_drag = TRUE;
        event_extra->evt_out_flags = PF_EO_HANDLED_EVENT;
        return PF_Err_NONE;
    }

    if (event_extra->e_type == PF_Event_DRAG) {
        bool changed = false;
        switch (event_extra->u.do_click.continue_refcon[0]) {
            case kGizmoDragSurfaceScale:
            case kGizmoDragSurfaceRotation: {
                const double previous_x = static_cast<double>(
                    event_extra->u.do_click.continue_refcon[2]);
                const double previous_y = static_cast<double>(
                    event_extra->u.do_click.continue_refcon[3]);
                const std::uint32_t axis = static_cast<std::uint32_t>(
                    event_extra->u.do_click.continue_refcon[1]);
                if (event_extra->u.do_click.continue_refcon[0] ==
                    kGizmoDragSurfaceScale) {
                    changed = DragSurfaceScale(
                        in_data,
                        params,
                        event_extra,
                        surface,
                        camera,
                        axis,
                        mouse.x - previous_x,
                        mouse.y - previous_y);
                } else {
                    changed = DragSurfaceRotation(
                        in_data,
                        params,
                        event_extra,
                        surface,
                        camera,
                        axis,
                        mouse.x - previous_x,
                        mouse.y - previous_y);
                }
                event_extra->u.do_click.continue_refcon[2] =
                    static_cast<A_intptr_t>(event_extra->u.do_click.screen_point.h);
                event_extra->u.do_click.continue_refcon[3] =
                    static_cast<A_intptr_t>(event_extra->u.do_click.screen_point.v);
                break;
            }
            case kGizmoDragControlDepth: {
                const double previous_x = static_cast<double>(
                    event_extra->u.do_click.continue_refcon[2]);
                const double previous_y = static_cast<double>(
                    event_extra->u.do_click.continue_refcon[3]);
                changed = DragControlDepth(
                    in_data,
                    params,
                    event_extra,
                    surface,
                    camera,
                    static_cast<std::uint32_t>(
                        event_extra->u.do_click.continue_refcon[1]),
                    mouse.x - previous_x,
                    mouse.y - previous_y);
                event_extra->u.do_click.continue_refcon[2] =
                    static_cast<A_intptr_t>(event_extra->u.do_click.screen_point.h);
                event_extra->u.do_click.continue_refcon[3] =
                    static_cast<A_intptr_t>(event_extra->u.do_click.screen_point.v);
                break;
            }
            case kGizmoDragControlPoint:
                changed = DragControlPoint(
                    in_data,
                    params,
                    event_extra,
                    surface,
                    camera,
                    static_cast<std::uint32_t>(
                        event_extra->u.do_click.continue_refcon[1]),
                    mouse);
                break;
            case kGizmoDragSurface:
                changed = DragSurfacePosition(
                    in_data,
                    params,
                    event_extra,
                    surface,
                    camera,
                    mouse);
                break;
            case kGizmoDragRotationOrigin:
                changed = DragRotationOrigin(
                    in_data,
                    params,
                    event_extra,
                    surface,
                    camera,
                    mouse);
                break;
            case kGizmoDragCurlTip:
                changed = DragCurlTip(
                    in_data,
                    params,
                    event_extra,
                    surface,
                    camera,
                    static_cast<std::uint32_t>(
                        event_extra->u.do_click.continue_refcon[1]),
                    mouse);
                break;
            case kGizmoDragCurlExtent:
                changed = DragCurlExtent(
                    in_data,
                    params,
                    event_extra,
                    surface,
                    camera,
                    static_cast<std::uint32_t>(
                        event_extra->u.do_click.continue_refcon[1]),
                    mouse);
                break;
            case kGizmoDragRollAngle:
                changed = DragRollHandle(
                    in_data,
                    params,
                    event_extra,
                    surface,
                    camera,
                    false,
                    mouse);
                break;
            case kGizmoDragRollLength:
                changed = DragRollHandle(
                    in_data,
                    params,
                    event_extra,
                    surface,
                    camera,
                    true,
                    mouse);
                break;
            case kGizmoDragTwistAngle:
                changed = DragTwistHandle(
                    in_data,
                    params,
                    event_extra,
                    surface,
                    camera,
                    static_cast<std::uint32_t>(
                        event_extra->u.do_click.continue_refcon[1]),
                    false,
                    mouse);
                break;
            case kGizmoDragTwistFalloff:
                changed = DragTwistHandle(
                    in_data,
                    params,
                    event_extra,
                    surface,
                    camera,
                    static_cast<std::uint32_t>(
                        event_extra->u.do_click.continue_refcon[1]),
                    true,
                    mouse);
                break;
            default:
                break;
        }
        if (changed) {
            event_extra->evt_out_flags = static_cast<PF_EventOutFlags>(
                PF_EO_HANDLED_EVENT | PF_EO_ALWAYS_UPDATE | PF_EO_UPDATE_NOW);
        }
        if (event_extra->u.do_click.last_time) {
            event_extra->u.do_click.send_drag = FALSE;
            event_extra->u.do_click.continue_refcon[0] = kGizmoDragNone;
        } else if (changed) {
            event_extra->u.do_click.send_drag = TRUE;
        }
    }
    return PF_Err_NONE;
}

PF_Err UpdateParameterUi(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_ParamDef* params[]) {
    if (!in_data->global_data || !params) {
        return PF_Err_NONE;
    }
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    auto* global = static_cast<GlobalData*>(
        suites.HandleSuite1()->host_lock_handle(in_data->global_data));
    if (!global) {
        return PF_Err_OUT_OF_MEMORY;
    }
    const AEGP_PluginID plugin_id = global->plugin_id;
    suites.HandleSuite1()->host_unlock_handle(in_data->global_data);

    AEGP_EffectRefH effect = nullptr;
    A_Err error = suites.PFInterfaceSuite1()->AEGP_GetNewEffectForEffect(
        plugin_id,
        in_data->effect_ref,
        &effect);
    if (error != A_Err_NONE || !effect) {
        return static_cast<PF_Err>(error);
    }

    const auto set_hidden = [&](PF_ParamIndex parameter, bool hidden) -> A_Err {
        AEGP_StreamRefH stream = nullptr;
        A_Err stream_error =
            suites.StreamSuite2()->AEGP_GetNewEffectStreamByIndex(
                plugin_id,
                effect,
                parameter,
                &stream);
        if (stream_error == A_Err_NONE && stream) {
            stream_error =
                suites.DynamicStreamSuite2()->AEGP_SetDynamicStreamFlag(
                    stream,
                    AEGP_DynStreamFlag_HIDDEN,
                    FALSE,
                    hidden ? TRUE : FALSE);
        }
        if (stream) {
            const A_Err dispose_error =
                suites.StreamSuite2()->AEGP_DisposeStream(stream);
            if (stream_error == A_Err_NONE) {
                stream_error = dispose_error;
            }
        }
        return stream_error;
    };

    std::uint32_t selected_bank = 0;
    std::uint32_t selected_surface_id = 1;
    PF_Handle scene_handle = params[kParamSceneData]->u.arb_d.value;
    if (scene_handle) {
        const auto* scene = static_cast<const SceneData*>(
            PF_LOCK_HANDLE(scene_handle));
        if (scene && IsValidScene(*scene)) {
            const SurfaceData& selected = scene->surfaces[scene->selected_surface];
            selected_bank = selected.animation_bank;
            selected_surface_id = selected.id;
        }
        if (scene) {
            PF_UNLOCK_HANDLE(scene_handle);
        }
    }

    if (error == A_Err_NONE) {
        PF_ParamDef* id_param = params[kParamControllerSurfaceId];
        id_param->u.sd.value = static_cast<A_long>(std::min<std::uint32_t>(
            selected_surface_id,
            static_cast<std::uint32_t>(std::numeric_limits<A_long>::max())));
        id_param->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
        error = suites.ParamUtilsSuite3()->PF_UpdateParamUI(
            in_data->effect_ref,
            kParamControllerSurfaceId,
            id_param);
    }
    if (error == A_Err_NONE) {
        PF_ParamDef* bank_param = params[kParamControllerAnimationBank];
        bank_param->u.sd.value = static_cast<A_long>(selected_bank);
        bank_param->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
        error = suites.ParamUtilsSuite3()->PF_UpdateParamUI(
            in_data->effect_ref,
            kParamControllerAnimationBank,
            bank_param);
    }

    if (error == A_Err_NONE) {
        error = set_hidden(kParamSurfaceCornerSelect, true);
    }
    if (error == A_Err_NONE) {
        error = set_hidden(kParamSurfaceTwistEdge, true);
    }
    for (std::size_t property = 0;
         property < static_cast<std::size_t>(kSurfaceAnimationPropertyCount) &&
         error == A_Err_NONE;
         ++property) {
        error = set_hidden(
            PrimaryAnimationParam(property),
            selected_bank != 0);
    }
    for (std::uint32_t bank = 1;
         bank < kMaximumSurfaces && error == A_Err_NONE;
         ++bank) {
        error = set_hidden(AnimationBankTopicParam(bank), bank != selected_bank);
    }

    const A_Err dispose_error = suites.EffectSuite2()->AEGP_DisposeEffect(effect);
    if (error == A_Err_NONE) {
        error = dispose_error;
    }
    if (error == A_Err_NONE) {
        out_data->out_flags |= PF_OutFlag_REFRESH_UI;
    }
    return static_cast<PF_Err>(error);
}

namespace {
PF_Err AddSurfaceAnimationBankParams(
    PF_InData* in_data,
    std::uint32_t bank) {
    PF_ParamDef def;
    const A_long disk_base = 10000 + static_cast<A_long>(bank) * 100;
    A_long property = 0;
    char topic_name[32];
    std::snprintf(topic_name, sizeof(topic_name), "Surface %u Animation", bank + 1);
    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC(topic_name, disk_base);

    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            char name[32];
            std::snprintf(name, sizeof(name), "Control %d,%d", row + 1, column + 1);
            AEFX_CLR_STRUCT(def);
            def.flags = PF_ParamFlag_SUPERVISE;
            PF_ADD_POINT(
                name,
                100.0 * static_cast<double>(column) / 3.0,
                100.0 * static_cast<double>(row) / 3.0,
                FALSE,
                disk_base + 1 + property++);
        }
    }
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            char name[32];
            std::snprintf(name, sizeof(name), "Depth %d,%d", row + 1, column + 1);
            AEFX_CLR_STRUCT(def);
            def.flags = PF_ParamFlag_SUPERVISE;
            PF_ADD_FLOAT_SLIDERX(
                name,
                -4000.0,
                4000.0,
                -2000.0,
                2000.0,
                0.0,
                PF_Precision_TENTHS,
                0,
                PF_ParamFlag_SUPERVISE,
                disk_base + 1 + property++);
        }
    }
    const char* rotation_names[3] = {"Rotation X", "Rotation Y", "Rotation Z"};
    for (int index = 0; index < 3; ++index) {
        AEFX_CLR_STRUCT(def);
        def.flags = PF_ParamFlag_SUPERVISE;
        PF_ADD_ANGLE(rotation_names[index], 0.0, disk_base + 1 + property++);
    }

    const char* size_names[2] = {"Size X", "Size Y"};
    const double size_defaults[2] = {
        std::max(1.0, static_cast<double>(in_data->width)),
        std::max(1.0, static_cast<double>(in_data->height))};
    for (int index = 0; index < 2; ++index) {
        AEFX_CLR_STRUCT(def);
        def.flags = PF_ParamFlag_SUPERVISE;
        PF_ADD_FLOAT_SLIDERX(
            size_names[index],
            0.001,
            30000.0,
            1.0,
            10000.0,
            size_defaults[index],
            PF_Precision_TENTHS,
            0,
            PF_ParamFlag_SUPERVISE,
            disk_base + 1 + property++);
    }

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_SUPERVISE;
    def.param_type = PF_Param_POINT_3D;
    PF_STRNNCPY(def.PF_DEF_NAME, "Position", sizeof(def.PF_DEF_NAME));
    def.u.point3d_d.x_value = def.u.point3d_d.x_dephault =
        static_cast<double>(in_data->width) * 0.5;
    def.u.point3d_d.y_value = def.u.point3d_d.y_dephault =
        static_cast<double>(in_data->height) * 0.5;
    def.u.point3d_d.z_value = def.u.point3d_d.z_dephault = 0.0;
    def.uu.id = disk_base + 1 + property++;
    PF_Err error = PF_ADD_PARAM(in_data, -1, &def);
    if (error != PF_Err_NONE) {
        return error;
    }

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_INTERP;
    PF_ADD_POPUP(
        "Origin Mode",
        6,
        static_cast<A_long>(kRotationOriginCenter),
        "Center|Left Edge|Right Edge|Top Edge|Bottom Edge|Custom",
        disk_base + 1 + property++);
    const char* origin_names[2] = {"Custom Origin X", "Custom Origin Y"};
    for (int index = 0; index < 2; ++index) {
        AEFX_CLR_STRUCT(def);
        def.flags = PF_ParamFlag_SUPERVISE;
        PF_ADD_FLOAT_SLIDERX(
            origin_names[index],
            -1000.0,
            1000.0,
            -200.0,
            300.0,
            50.0,
            PF_Precision_TENTHS,
            PF_ValueDisplayFlag_PERCENT,
            PF_ParamFlag_SUPERVISE,
            disk_base + 1 + property++);
    }

    const char* scale_names[3] = {"Scale X", "Scale Y", "Scale Z"};
    for (int index = 0; index < 3; ++index) {
        AEFX_CLR_STRUCT(def);
        def.flags = PF_ParamFlag_SUPERVISE;
        PF_ADD_FLOAT_SLIDERX(
            scale_names[index],
            -1000.0,
            1000.0,
            -400.0,
            400.0,
            100.0,
            PF_Precision_TENTHS,
            PF_ValueDisplayFlag_PERCENT,
            PF_ParamFlag_SUPERVISE,
            disk_base + 1 + property++);
    }

    const char* divisions_names[2] = {"Divisions X", "Divisions Y"};
    for (int index = 0; index < 2; ++index) {
        AEFX_CLR_STRUCT(def);
        def.flags = PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY;
        PF_ADD_SLIDER(
            divisions_names[index],
            static_cast<A_long>(kMinimumDivisions),
            static_cast<A_long>(kMaximumDivisions),
            static_cast<A_long>(kMinimumDivisions),
            static_cast<A_long>(kMaximumDivisions),
            static_cast<A_long>(kDefaultDivisions),
            disk_base + 1 + property++);
    }

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_SUPERVISE;
    PF_ADD_FLOAT_SLIDERX(
        "Thickness", 0.0, 4000.0, 0.0, 1000.0, 0.0,
        PF_Precision_TENTHS, 0, PF_ParamFlag_SUPERVISE,
        disk_base + 1 + property++);
    const char* bend_names[2] = {"Bend X", "Bend Y"};
    for (int index = 0; index < 2; ++index) {
        AEFX_CLR_STRUCT(def);
        PF_ADD_FLOAT_SLIDERX(
            bend_names[index], -720.0, 720.0, -360.0, 360.0, 0.0,
            PF_Precision_TENTHS, 0, PF_ParamFlag_SUPERVISE,
            disk_base + 1 + property++);
    }
    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_INTERP;
    PF_ADD_POPUP(
        "Roll Edge", 4, static_cast<A_long>(kRollEdgeRight),
        "Right|Left|Bottom|Top", disk_base + 1 + property++);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(
        "Roll Angle", -1080.0, 1080.0, -720.0, 720.0, 0.0,
        PF_Precision_TENTHS, 0, PF_ParamFlag_SUPERVISE,
        disk_base + 1 + property++);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(
        "Roll Length", 0.0, 100.0, 0.0, 100.0, 25.0,
        PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT,
        PF_ParamFlag_SUPERVISE, disk_base + 1 + property++);

    const char* corner_names[4] = {"TL", "TR", "BR", "BL"};
    for (int corner = 0; corner < 4; ++corner) {
        char name[32];
        std::snprintf(name, sizeof(name), "%s Curl Amount", corner_names[corner]);
        AEFX_CLR_STRUCT(def);
        PF_ADD_FLOAT_SLIDERX(
            name, -100.0, 100.0, -100.0, 100.0, 0.0,
            PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT,
            PF_ParamFlag_SUPERVISE, disk_base + 1 + property++);
        std::snprintf(name, sizeof(name), "%s Curl Radius", corner_names[corner]);
        AEFX_CLR_STRUCT(def);
        PF_ADD_FLOAT_SLIDERX(
            name, 0.1, 100.0, 1.0, 50.0, 15.0,
            PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT,
            PF_ParamFlag_SUPERVISE, disk_base + 1 + property++);
        std::snprintf(name, sizeof(name), "%s Curl Direction", corner_names[corner]);
        AEFX_CLR_STRUCT(def);
        PF_ADD_FLOAT_SLIDERX(
            name, 0.0, 90.0, 0.0, 90.0, 45.0,
            PF_Precision_TENTHS, 0, PF_ParamFlag_SUPERVISE,
            disk_base + 1 + property++);
        std::snprintf(name, sizeof(name), "%s Curl Length", corner_names[corner]);
        AEFX_CLR_STRUCT(def);
        PF_ADD_FLOAT_SLIDERX(
            name, 0.0, 150.0, 0.0, 100.0, 30.0,
            PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT,
            PF_ParamFlag_SUPERVISE, disk_base + 1 + property++);
    }

    const char* edge_names[4] = {"Left", "Right", "Top", "Bottom"};
    for (int edge = 0; edge < 4; ++edge) {
        char name[32];
        std::snprintf(name, sizeof(name), "%s Twist Angle", edge_names[edge]);
        AEFX_CLR_STRUCT(def);
        PF_ADD_FLOAT_SLIDERX(
            name, -180.0, 180.0, -180.0, 180.0, 0.0,
            PF_Precision_TENTHS, 0, PF_ParamFlag_SUPERVISE,
            disk_base + 1 + property++);
        std::snprintf(name, sizeof(name), "%s Twist Falloff", edge_names[edge]);
        AEFX_CLR_STRUCT(def);
        PF_ADD_FLOAT_SLIDERX(
            name, 1.0, 100.0, 1.0, 100.0, 100.0,
            PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT,
            PF_ParamFlag_SUPERVISE, disk_base + 1 + property++);
    }

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY;
    PF_ADD_POPUP(
        "Front Source", 8, 1,
        "Slot 1|Slot 2|Slot 3|Slot 4|Slot 5|Slot 6|Slot 7|Slot 8",
        disk_base + 1 + property++);
    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY;
    PF_ADD_POPUP(
        "Back Source", 9, 1,
        "Same as Front|Slot 1|Slot 2|Slot 3|Slot 4|Slot 5|Slot 6|Slot 7|Slot 8",
        disk_base + 1 + property++);
    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_INTERP;
    PF_ADD_POPUP(
        "Image Size", 3, static_cast<A_long>(kImageSizeStretch),
        "Stretch|Fill|Fit", disk_base + 1 + property++);
    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_INTERP;
    PF_ADD_POPUP(
        "Image Border", 4, static_cast<A_long>(kImageBorderClamp),
        "Clamp|Repeat|Mirror|Transparent", disk_base + 1 + property++);

    const char* material_names[4] = {
        "Opacity", "Diffuse", "Specular", "Shininess"};
    const double material_minimums[4] = {0.0, 0.0, 0.0, 1.0};
    const double material_maximums[4] = {100.0, 200.0, 200.0, 256.0};
    const double material_defaults[4] = {100.0, 100.0, 50.0, 32.0};
    for (int index = 0; index < 4; ++index) {
        AEFX_CLR_STRUCT(def);
        PF_ADD_FLOAT_SLIDERX(
            material_names[index],
            material_minimums[index],
            material_maximums[index],
            material_minimums[index],
            index == 3 ? 128.0 : material_maximums[index],
            material_defaults[index],
            PF_Precision_TENTHS,
            index == 3 ? 0 : PF_ValueDisplayFlag_PERCENT,
            PF_ParamFlag_SUPERVISE,
            disk_base + 1 + property++);
    }

    if (property != kSurfaceAnimationPropertyCount) {
        return PF_Err_INTERNAL_STRUCT_DAMAGED;
    }
    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(disk_base + 99);
    return PF_Err_NONE;
}

}  // namespace

PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data) {
    PF_ParamDef def;
    AEFX_CLR_STRUCT(def);

    PF_ADD_SLIDER(
        "Tessellation",
        2,
        32,
        2,
        32,
        12,
        kDiskTessellation);

    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOX("Wireframe", "Show tessellation", FALSE, 0, kDiskWireframe);

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_CANNOT_TIME_VARY | PF_ParamFlag_CANNOT_INTERP;
    PF_ADD_POPUP(
        "Interaction Mode",
        4,
        kGizmoInteractionAll,
        "All|Surface|Control Points|Deform",
        kDiskGizmoInteractionMode);

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_CANNOT_TIME_VARY | PF_ParamFlag_CANNOT_INTERP;
    PF_ADD_POPUP(
        "Gizmo Tool",
        4,
        kGizmoToolAll,
        "All|Position|Rotation|Scale",
        kDiskGizmoTool);

    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            const int index = row * 4 + column;
            char name[32];
            std::snprintf(name, sizeof(name), "Control %d,%d", row + 1, column + 1);
            AEFX_CLR_STRUCT(def);
            def.flags = PF_ParamFlag_SUPERVISE;
            PF_ADD_POINT(
                name,
                100.0 * static_cast<double>(column) / 3.0,
                100.0 * static_cast<double>(row) / 3.0,
                FALSE,
                kDiskPoint00 + index);
        }
    }

    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOX(
        "Perspective",
        "Use internal perspective camera",
        TRUE,
        0,
        kDiskPerspective);

    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER(
        "Camera Distance",
        100,
        10000,
        100,
        5000,
        2000,
        kDiskCameraDistance);

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_SUPERVISE;
    PF_ADD_ANGLE("Rotation X", 0.0, kDiskRotationX);
    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_SUPERVISE;
    PF_ADD_ANGLE("Rotation Y", 0.0, kDiskRotationY);
    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_SUPERVISE;
    PF_ADD_ANGLE("Rotation Z", 0.0, kDiskRotationZ);

    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            const int index = row * 4 + column;
            char name[32];
            std::snprintf(name, sizeof(name), "Depth %d,%d", row + 1, column + 1);
            AEFX_CLR_STRUCT(def);
            def.flags = PF_ParamFlag_SUPERVISE;
            PF_ADD_FLOAT_SLIDERX(
                name,
                -4000.0,
                4000.0,
                -2000.0,
                2000.0,
                0.0,
                PF_Precision_TENTHS,
                0,
                PF_ParamFlag_SUPERVISE,
                kDiskDepth00 + index);
        }
    }

    PF_ArbitraryH default_scene = nullptr;
    PF_Err error = CreateSceneHandle(
        in_data,
        &default_scene,
        in_data->width,
        in_data->height);
    if (error != PF_Err_NONE) {
        return error;
    }

    AEFX_CLR_STRUCT(def);
    PF_ADD_ARBITRARY2(
        "Scene Data",
        0,
        0,
        PF_ParamFlag_NONE,
        PF_PUI_NO_ECW_UI,
        default_scene,
        kDiskSceneData,
        SceneRefcon());

    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Surfaces", kDiskSurfacesStart);

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_CANNOT_TIME_VARY;
    def.ui_flags = PF_PUI_CONTROL | PF_PUI_DONT_ERASE_CONTROL;
    def.ui_width = kSurfaceListUiWidth;
    def.ui_height = kSurfaceListUiHeight;
    PF_ADD_SLIDER(
        "Surface List",
        1,
        static_cast<A_long>(kMaximumSurfaces),
        1,
        static_cast<A_long>(kMaximumSurfaces),
        1,
        kDiskSurfaceCount);

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_CANNOT_TIME_VARY | PF_ParamFlag_SUPERVISE;
    def.ui_flags = PF_PUI_NO_ECW_UI;
    PF_ADD_POPUP(
        "Selected Surface",
        static_cast<A_long>(kMaximumSurfaces),
        1,
        "Surface 1|Surface 2|Surface 3|Surface 4|Surface 5|Surface 6|Surface 7|Surface 8",
        kDiskSurfaceSelect);

    PF_ADD_BUTTON(
        "Create Surface",
        "Add...",
        PF_PUI_NONE,
        PF_ParamFlag_SUPERVISE,
        kDiskAddSurface);
    PF_ADD_BUTTON(
        "Duplicate Selected",
        "Duplicate",
        PF_PUI_NONE,
        PF_ParamFlag_SUPERVISE,
        kDiskDuplicateSurface);
    PF_ADD_BUTTON(
        "Delete Selected",
        "Delete",
        PF_PUI_NONE,
        PF_ParamFlag_SUPERVISE,
        kDiskDeleteSurface);

    // Script-facing stable binding metadata. It remains a normal effect
    // stream for expressions/JSX, but does not add controls to the ECW.
    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_CANNOT_TIME_VARY;
    def.ui_flags = PF_PUI_NO_ECW_UI | PF_PUI_DISABLED;
    PF_ADD_SLIDER(
        "Controller Surface ID",
        1,
        std::numeric_limits<A_long>::max(),
        1,
        std::numeric_limits<A_long>::max(),
        1,
        kDiskControllerSurfaceId);

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_CANNOT_TIME_VARY;
    def.ui_flags = PF_PUI_NO_ECW_UI | PF_PUI_DISABLED;
    PF_ADD_SLIDER(
        "Controller Animation Bank",
        0,
        static_cast<A_long>(kMaximumSurfaces - 1),
        0,
        static_cast<A_long>(kMaximumSurfaces - 1),
        0,
        kDiskControllerAnimationBank);

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_SUPERVISE;
    PF_ADD_FLOAT_SLIDERX(
        "Size X",
        0.001,
        30000.0,
        1.0,
        10000.0,
        std::max(1.0, static_cast<double>(in_data->width)),
        PF_Precision_TENTHS,
        0,
        PF_ParamFlag_SUPERVISE,
        kDiskSurfaceSizeX);

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_SUPERVISE;
    PF_ADD_FLOAT_SLIDERX(
        "Size Y",
        0.001,
        30000.0,
        1.0,
        10000.0,
        std::max(1.0, static_cast<double>(in_data->height)),
        PF_Precision_TENTHS,
        0,
        PF_ParamFlag_SUPERVISE,
        kDiskSurfaceSizeY);

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_SUPERVISE;
    def.param_type = PF_Param_POINT_3D;
    PF_STRNNCPY(def.PF_DEF_NAME, "Position", sizeof(def.PF_DEF_NAME));
    def.u.point3d_d.x_value = def.u.point3d_d.x_dephault = 50.0;
    def.u.point3d_d.y_value = def.u.point3d_d.y_dephault = 50.0;
    def.u.point3d_d.z_value = def.u.point3d_d.z_dephault = 0.0;
    def.uu.id = kDiskSurfacePosition;
    error = PF_ADD_PARAM(in_data, -1, &def);
    if (error != PF_Err_NONE) {
        return error;
    }

    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Rotation Origin", kDiskSurfaceRotationOriginStart);

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_INTERP;
    PF_ADD_POPUP(
        "Origin Mode",
        6,
        static_cast<A_long>(kRotationOriginCenter),
        "Center|Left Edge|Right Edge|Top Edge|Bottom Edge|Custom",
        kDiskSurfaceRotationOriginMode);

    const char* origin_names[2] = {"Custom Origin X", "Custom Origin Y"};
    const A_long origin_disk_ids[2] = {
        kDiskSurfaceRotationOriginX,
        kDiskSurfaceRotationOriginY};
    for (int index = 0; index < 2; ++index) {
        AEFX_CLR_STRUCT(def);
        def.flags = PF_ParamFlag_SUPERVISE;
        PF_ADD_FLOAT_SLIDERX(
            origin_names[index],
            -1000.0,
            1000.0,
            -200.0,
            300.0,
            50.0,
            PF_Precision_TENTHS,
            PF_ValueDisplayFlag_PERCENT,
            PF_ParamFlag_SUPERVISE,
            origin_disk_ids[index]);
    }

    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(kDiskSurfaceRotationOriginEnd);

    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Scale", kDiskSurfaceScaleStart);

    const char* scale_names[3] = {"Scale X", "Scale Y", "Scale Z"};
    const A_long scale_disk_ids[3] = {
        kDiskSurfaceScaleX,
        kDiskSurfaceScaleY,
        kDiskSurfaceScaleZ};
    for (int index = 0; index < 3; ++index) {
        AEFX_CLR_STRUCT(def);
        def.flags = PF_ParamFlag_SUPERVISE;
        PF_ADD_FLOAT_SLIDERX(
            scale_names[index],
            -1000.0,
            1000.0,
            -400.0,
            400.0,
            100.0,
            PF_Precision_TENTHS,
            PF_ValueDisplayFlag_PERCENT,
            PF_ParamFlag_SUPERVISE,
            scale_disk_ids[index]);
    }

    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(kDiskSurfaceScaleEnd);

    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Divisions", kDiskSurfaceDivisionsStart);

    const char* divisions_names[2] = {"Divisions X", "Divisions Y"};
    const A_long divisions_disk_ids[2] = {
        kDiskSurfaceDivisionsX,
        kDiskSurfaceDivisionsY};
    for (int index = 0; index < 2; ++index) {
        AEFX_CLR_STRUCT(def);
        def.flags = PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY;
        PF_ADD_SLIDER(
            divisions_names[index],
            static_cast<A_long>(kMinimumDivisions),
            static_cast<A_long>(kMaximumDivisions),
            static_cast<A_long>(kMinimumDivisions),
            static_cast<A_long>(kMaximumDivisions),
            static_cast<A_long>(kDefaultDivisions),
            divisions_disk_ids[index]);
    }

    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(kDiskSurfaceDivisionsEnd);

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_SUPERVISE;
    PF_ADD_FLOAT_SLIDERX(
        "Thickness",
        0.0,
        4000.0,
        0.0,
        1000.0,
        0.0,
        PF_Precision_TENTHS,
        0,
        PF_ParamFlag_SUPERVISE,
        kDiskSurfaceThickness);

    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Deform", kDiskSurfaceDeformStart);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(
        "Bend X",
        -720.0,
        720.0,
        -360.0,
        360.0,
        0.0,
        PF_Precision_TENTHS,
        0,
        PF_ParamFlag_SUPERVISE,
        kDiskSurfaceBendX);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(
        "Bend Y",
        -720.0,
        720.0,
        -360.0,
        360.0,
        0.0,
        PF_Precision_TENTHS,
        0,
        PF_ParamFlag_SUPERVISE,
        kDiskSurfaceBendY);

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_INTERP;
    PF_ADD_POPUP(
        "Roll Edge",
        4,
        static_cast<A_long>(kRollEdgeRight),
        "Right|Left|Bottom|Top",
        kDiskSurfaceRollEdge);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(
        "Roll Angle",
        -1080.0,
        1080.0,
        -720.0,
        720.0,
        0.0,
        PF_Precision_TENTHS,
        0,
        PF_ParamFlag_SUPERVISE,
        kDiskSurfaceRollAngle);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(
        "Roll Length",
        0.0,
        100.0,
        0.0,
        100.0,
        25.0,
        PF_Precision_TENTHS,
        PF_ValueDisplayFlag_PERCENT,
        PF_ParamFlag_SUPERVISE,
        kDiskSurfaceRollLength);

    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Corner Curl", kDiskSurfaceCornerCurlStart);

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY;
    PF_ADD_POPUP(
        "Corner",
        4,
        static_cast<A_long>(kCornerTopLeft),
        "Top Left|Top Right|Bottom Right|Bottom Left",
        kDiskSurfaceCornerSelect);

    const A_long corner_amount_disk_ids[4] = {
        kDiskSurfaceCornerAmount,
        kDiskSurfaceCorner2Amount,
        kDiskSurfaceCorner3Amount,
        kDiskSurfaceCorner4Amount};
    const A_long corner_radius_disk_ids[4] = {
        kDiskSurfaceCornerRadius,
        kDiskSurfaceCorner2Radius,
        kDiskSurfaceCorner3Radius,
        kDiskSurfaceCorner4Radius};
    const A_long corner_direction_disk_ids[4] = {
        kDiskSurfaceCornerDirection,
        kDiskSurfaceCorner2Direction,
        kDiskSurfaceCorner3Direction,
        kDiskSurfaceCorner4Direction};
    const A_long corner_length_disk_ids[4] = {
        kDiskSurfaceCornerLength,
        kDiskSurfaceCorner2Length,
        kDiskSurfaceCorner3Length,
        kDiskSurfaceCorner4Length};
    const char* corner_names[4] = {"TL", "TR", "BR", "BL"};
    for (int corner = 0; corner < 4; ++corner) {
        char amount_name[32];
        char radius_name[32];
        char direction_name[32];
        char length_name[32];
        std::snprintf(
            amount_name,
            sizeof(amount_name),
            "%s Curl Amount",
            corner_names[corner]);
        std::snprintf(
            radius_name,
            sizeof(radius_name),
            "%s Curl Radius",
            corner_names[corner]);
        std::snprintf(
            direction_name,
            sizeof(direction_name),
            "%s Curl Direction",
            corner_names[corner]);
        std::snprintf(
            length_name,
            sizeof(length_name),
            "%s Curl Length",
            corner_names[corner]);
        PF_ADD_FLOAT_SLIDERX(
            amount_name,
            -100.0,
            100.0,
            -100.0,
            100.0,
            0.0,
            PF_Precision_TENTHS,
            PF_ValueDisplayFlag_PERCENT,
            PF_ParamFlag_SUPERVISE,
            corner_amount_disk_ids[corner]);
        PF_ADD_FLOAT_SLIDERX(
            radius_name,
            0.1,
            100.0,
            1.0,
            50.0,
            15.0,
            PF_Precision_TENTHS,
            PF_ValueDisplayFlag_PERCENT,
            PF_ParamFlag_SUPERVISE,
            corner_radius_disk_ids[corner]);
        PF_ADD_FLOAT_SLIDERX(
            direction_name,
            0.0,
            90.0,
            0.0,
            90.0,
            45.0,
            PF_Precision_TENTHS,
            0,
            PF_ParamFlag_SUPERVISE,
            corner_direction_disk_ids[corner]);
        PF_ADD_FLOAT_SLIDERX(
            length_name,
            0.0,
            150.0,
            0.0,
            100.0,
            30.0,
            PF_Precision_TENTHS,
            PF_ValueDisplayFlag_PERCENT,
            PF_ParamFlag_SUPERVISE,
            corner_length_disk_ids[corner]);
    }

    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(kDiskSurfaceCornerCurlEnd);

    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Edge Twist", kDiskSurfaceEdgeTwistStart);

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY;
    PF_ADD_POPUP(
        "Twist Edge",
        4,
        static_cast<A_long>(kTwistEdgeLeft),
        "Left|Right|Top|Bottom",
        kDiskSurfaceTwistEdge);

    const A_long twist_angle_disk_ids[4] = {
        kDiskSurfaceTwistAngle,
        kDiskSurfaceTwist2Angle,
        kDiskSurfaceTwist3Angle,
        kDiskSurfaceTwist4Angle};
    const A_long twist_falloff_disk_ids[4] = {
        kDiskSurfaceTwistFalloff,
        kDiskSurfaceTwist2Falloff,
        kDiskSurfaceTwist3Falloff,
        kDiskSurfaceTwist4Falloff};
    const char* edge_names[4] = {"Left", "Right", "Top", "Bottom"};
    for (int edge = 0; edge < 4; ++edge) {
        char angle_name[32];
        char falloff_name[32];
        std::snprintf(
            angle_name,
            sizeof(angle_name),
            "%s Twist Angle",
            edge_names[edge]);
        std::snprintf(
            falloff_name,
            sizeof(falloff_name),
            "%s Twist Falloff",
            edge_names[edge]);
        PF_ADD_FLOAT_SLIDERX(
            angle_name,
            -180.0,
            180.0,
            -180.0,
            180.0,
            0.0,
            PF_Precision_TENTHS,
            0,
            PF_ParamFlag_SUPERVISE,
            twist_angle_disk_ids[edge]);
        PF_ADD_FLOAT_SLIDERX(
            falloff_name,
            1.0,
            100.0,
            1.0,
            100.0,
            100.0,
            PF_Precision_TENTHS,
            PF_ValueDisplayFlag_PERCENT,
            PF_ParamFlag_SUPERVISE,
            twist_falloff_disk_ids[edge]);
    }

    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(kDiskSurfaceEdgeTwistEnd);

    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(kDiskSurfaceDeformEnd);

    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Material", kDiskSurfaceMaterialStart);

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY;
    PF_ADD_POPUP(
        "Front Source",
        static_cast<A_long>(kMaximumSurfaces),
        1,
        "Slot 1|Slot 2|Slot 3|Slot 4|Slot 5|Slot 6|Slot 7|Slot 8",
        kDiskSurfaceSourceSlot);

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY;
    PF_ADD_POPUP(
        "Back Source",
        static_cast<A_long>(kMaximumSurfaces + 1),
        1,
        "Same as Front|Slot 1|Slot 2|Slot 3|Slot 4|Slot 5|Slot 6|Slot 7|Slot 8",
        kDiskSurfaceBackSourceSlot);

    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Source Layers", kDiskSurfaceSourceLayersStart);

    const A_long source_layer_disk_ids[kMaximumSurfaces] = {
        kDiskSurfaceSourceLayer1,
        kDiskSurfaceSourceLayer2,
        kDiskSurfaceSourceLayer3,
        kDiskSurfaceSourceLayer4,
        kDiskSurfaceSourceLayer5,
        kDiskSurfaceSourceLayer6,
        kDiskSurfaceSourceLayer7,
        kDiskSurfaceSourceLayer8};
    for (std::uint32_t index = 0; index < kMaximumSurfaces; ++index) {
        char name[32];
        std::snprintf(name, sizeof(name), "Slot %u Layer", index + 1);
        AEFX_CLR_STRUCT(def);
        def.flags = PF_ParamFlag_CANNOT_TIME_VARY;
        PF_ADD_LAYER(
            name,
            PF_LayerDefault_MYSELF,
            source_layer_disk_ids[index]);
    }

    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(kDiskSurfaceSourceLayersEnd);

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_INTERP;
    PF_ADD_POPUP(
        "Image Size",
        3,
        static_cast<A_long>(kImageSizeStretch),
        "Stretch|Fill|Fit",
        kDiskSurfaceImageSize);

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_INTERP;
    PF_ADD_POPUP(
        "Image Border",
        4,
        static_cast<A_long>(kImageBorderClamp),
        "Clamp|Repeat|Mirror|Transparent",
        kDiskSurfaceImageBorder);

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_SUPERVISE;
    PF_ADD_FLOAT_SLIDERX(
        "Opacity",
        0.0,
        100.0,
        0.0,
        100.0,
        100.0,
        PF_Precision_TENTHS,
        PF_ValueDisplayFlag_PERCENT,
        PF_ParamFlag_SUPERVISE,
        kDiskSurfaceOpacity);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(
        "Diffuse",
        0.0,
        200.0,
        0.0,
        200.0,
        100.0,
        PF_Precision_TENTHS,
        PF_ValueDisplayFlag_PERCENT,
        PF_ParamFlag_SUPERVISE,
        kDiskSurfaceDiffuse);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(
        "Specular",
        0.0,
        200.0,
        0.0,
        200.0,
        50.0,
        PF_Precision_TENTHS,
        PF_ValueDisplayFlag_PERCENT,
        PF_ParamFlag_SUPERVISE,
        kDiskSurfaceSpecular);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(
        "Shininess",
        1.0,
        256.0,
        1.0,
        128.0,
        32.0,
        PF_Precision_TENTHS,
        0,
        PF_ParamFlag_SUPERVISE,
        kDiskSurfaceShininess);

    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(kDiskSurfaceMaterialEnd);

    for (std::uint32_t bank = 1; bank < kMaximumSurfaces; ++bank) {
        const PF_Err bank_error = AddSurfaceAnimationBankParams(in_data, bank);
        if (bank_error != PF_Err_NONE) {
            return bank_error;
        }
    }

    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(kDiskSurfacesEnd);

    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Camera", kDiskCameraStart);

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_CANNOT_TIME_VARY;
    PF_ADD_POPUP(
        "Camera Source",
        2,
        kCameraSourceInternal,
        "Internal|After Effects Active Camera",
        kDiskCameraSource);

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_CANNOT_TIME_VARY;
    PF_ADD_POPUP(
        "Coordinate Space",
        2,
        kCoordinateSpaceLayerLocal,
        "Layer Local (Legacy)|Composition World",
        kDiskCoordinateSpace);

    const char* camera_offset_names[3] = {
        "Camera Offset X",
        "Camera Offset Y",
        "Camera Offset Z"};
    const A_long camera_offset_disk_ids[3] = {
        kDiskCameraOffsetX,
        kDiskCameraOffsetY,
        kDiskCameraOffsetZ};
    for (int index = 0; index < 3; ++index) {
        AEFX_CLR_STRUCT(def);
        PF_ADD_FLOAT_SLIDERX(
            camera_offset_names[index],
            -10000.0,
            10000.0,
            -4000.0,
            4000.0,
            0.0,
            PF_Precision_TENTHS,
            0,
            PF_ParamFlag_NONE,
            camera_offset_disk_ids[index]);
    }

    AEFX_CLR_STRUCT(def);
    PF_ADD_ANGLE("Camera Rotation X", 0.0, kDiskCameraRotationX);
    AEFX_CLR_STRUCT(def);
    PF_ADD_ANGLE("Camera Rotation Y", 0.0, kDiskCameraRotationY);
    AEFX_CLR_STRUCT(def);
    PF_ADD_ANGLE("Camera Rotation Z", 0.0, kDiskCameraRotationZ);

    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(kDiskCameraEnd);

    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Lights", kDiskLightsStart);

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_CANNOT_TIME_VARY;
    PF_ADD_POPUP(
        "Light Source",
        2,
        kLightSourceInternal,
        "Internal|After Effects Comp Lights",
        kDiskLightSource);

    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOX(
        "Lighting",
        "Enable directional light",
        FALSE,
        0,
        kDiskLightingEnabled);

    AEFX_CLR_STRUCT(def);
    PF_ADD_ANGLE("Light Rotation X", -35.0, kDiskLightRotationX);
    AEFX_CLR_STRUCT(def);
    PF_ADD_ANGLE("Light Rotation Y", 25.0, kDiskLightRotationY);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(
        "Light Intensity",
        0.0,
        400.0,
        0.0,
        200.0,
        100.0,
        PF_Precision_TENTHS,
        PF_ValueDisplayFlag_PERCENT,
        PF_ParamFlag_NONE,
        kDiskLightIntensity);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(
        "Ambient Light",
        0.0,
        100.0,
        0.0,
        100.0,
        20.0,
        PF_Precision_TENTHS,
        PF_ValueDisplayFlag_PERCENT,
        PF_ParamFlag_NONE,
        kDiskAmbientLight);

    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(kDiskLightsEnd);

    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Render Settings", kDiskRenderSettingsStart);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP(
        "Texture Filter",
        2,
        kTextureFilterBilinear,
        "Nearest|Bilinear",
        kDiskTextureFilter);

    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOX(
        "Backface Culling",
        "Hide back-facing polygons",
        FALSE,
        0,
        kDiskBackfaceCulling);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP(
        "Output Bounds",
        3,
        kOutputBoundsAuto,
        "Source|Auto|Fixed Padding",
        kDiskOutputBoundsMode);

    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER(
        "Padding X",
        0,
        14000,
        0,
        4000,
        kDefaultOutputPadding,
        kDiskOutputPaddingX);

    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER(
        "Padding Y",
        0,
        14000,
        0,
        4000,
        kDefaultOutputPadding,
        kDiskOutputPaddingY);

    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(kDiskRenderSettingsEnd);

    PF_CustomUIInfo custom_ui;
    AEFX_CLR_STRUCT(custom_ui);
    custom_ui.events =
        PF_CustomEFlag_LAYER | PF_CustomEFlag_COMP | PF_CustomEFlag_EFFECT;
    custom_ui.comp_ui_width = 0;
    custom_ui.comp_ui_height = 0;
    custom_ui.comp_ui_alignment = PF_UIAlignment_NONE;
    custom_ui.layer_ui_width = 0;
    custom_ui.layer_ui_height = 0;
    custom_ui.layer_ui_alignment = PF_UIAlignment_NONE;
    custom_ui.preview_ui_width = 0;
    custom_ui.preview_ui_height = 0;
    custom_ui.preview_ui_alignment = PF_UIAlignment_NONE;
    const PF_Err custom_ui_error =
        in_data->inter.register_ui(in_data->effect_ref, &custom_ui);
    if (custom_ui_error != PF_Err_NONE) {
        std::snprintf(
            out_data->return_msg,
            sizeof(out_data->return_msg),
            "SurfaceLab custom UI registration failed (%d).",
            static_cast<int>(custom_ui_error));
        out_data->out_flags |= PF_OutFlag_DISPLAY_ERROR_MESSAGE;
        return custom_ui_error;
    }

    out_data->num_params = kParamCount;
    return PF_Err_NONE;
}
