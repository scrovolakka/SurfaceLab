#pragma once

// Shared internal model for SurfaceLab.
//
// Extracted verbatim from SurfaceLab.cpp (phase 1 of the decomposition) so the
// geometry, scene-persistence, custom-UI, and rendering translation units can
// share one definition of the scene data model, its versioned on-disk structs,
// and the small pure parameter-mapping helpers. This is an INTERNAL header; the
// public plug-in surface stays in SurfaceLab.h.

#include "SurfaceLab.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

struct Point2 {
    double x{};
    double y{};
};

struct Point3 {
    double x{};
    double y{};
    double z{};
};

struct GlobalData {
    AEGP_PluginID plugin_id{};
};

constexpr std::uint32_t kSceneMagic = 0x534C4142U;  // "SLAB"
constexpr std::uint32_t kSceneSchemaVersion = 13;
constexpr std::size_t kAnimationStreamsInitializedIndex = 0;
constexpr std::size_t kAnimationBanksInitializedIndex = 1;
constexpr std::uint32_t kMaximumSurfaces = 8;
constexpr std::size_t kScenePrintSize = 96;
constexpr std::uint32_t kDefaultDivisions = 12;
constexpr std::uint32_t kMinimumDivisions = 2;
constexpr std::uint32_t kMaximumDivisions = 32;
constexpr std::uint32_t kImageSizeStretch = 1;
constexpr std::uint32_t kImageSizeFill = 2;
constexpr std::uint32_t kImageSizeFit = 3;
constexpr std::uint32_t kImageBorderClamp = 1;
constexpr std::uint32_t kImageBorderRepeat = 2;
constexpr std::uint32_t kImageBorderMirror = 3;
constexpr std::uint32_t kImageBorderTransparent = 4;
constexpr A_long kTextureFilterNearest = 1;
constexpr A_long kTextureFilterBilinear = 2;
constexpr A_long kOutputBoundsSource = 1;
constexpr A_long kOutputBoundsAuto = 2;
constexpr A_long kOutputBoundsFixed = 3;
constexpr A_long kDefaultOutputPadding = 32;
constexpr std::uint32_t kRollEdgeRight = 1;
constexpr std::uint32_t kRollEdgeLeft = 2;
constexpr std::uint32_t kRollEdgeBottom = 3;
constexpr std::uint32_t kRollEdgeTop = 4;
constexpr std::uint32_t kCornerTopLeft = 1;
constexpr std::uint32_t kCornerTopRight = 2;
constexpr std::uint32_t kCornerBottomRight = 3;
constexpr std::uint32_t kCornerBottomLeft = 4;
constexpr std::uint32_t kTwistEdgeLeft = 1;
constexpr std::uint32_t kTwistEdgeRight = 2;
constexpr std::uint32_t kTwistEdgeTop = 3;
constexpr std::uint32_t kTwistEdgeBottom = 4;
constexpr std::uint32_t kRotationOriginCenter = 1;
constexpr std::uint32_t kRotationOriginLeftEdge = 2;
constexpr std::uint32_t kRotationOriginRightEdge = 3;
constexpr std::uint32_t kRotationOriginTopEdge = 4;
constexpr std::uint32_t kRotationOriginBottomEdge = 5;
constexpr std::uint32_t kRotationOriginCustom = 6;
constexpr A_long kGizmoInteractionAll = 1;
constexpr A_long kGizmoInteractionSurface = 2;
constexpr A_long kGizmoInteractionControlPoints = 3;
constexpr A_long kGizmoInteractionDeform = 4;
constexpr A_long kGizmoToolAll = 1;
constexpr A_long kGizmoToolPosition = 2;
constexpr A_long kGizmoToolRotation = 3;
constexpr A_long kGizmoToolScale = 4;
constexpr A_long kCameraSourceInternal = 1;
constexpr A_long kCameraSourceAfterEffects = 2;
constexpr A_long kLightSourceInternal = 1;
constexpr A_long kLightSourceAfterEffects = 2;
constexpr std::size_t kMaximumRenderLights = 8;

constexpr std::array<PF_ParamIndex, 4> kCornerAmountParams = {
    kParamSurfaceCornerAmount,
    kParamSurfaceCorner2Amount,
    kParamSurfaceCorner3Amount,
    kParamSurfaceCorner4Amount};
constexpr std::array<PF_ParamIndex, 4> kCornerRadiusParams = {
    kParamSurfaceCornerRadius,
    kParamSurfaceCorner2Radius,
    kParamSurfaceCorner3Radius,
    kParamSurfaceCorner4Radius};
constexpr std::array<PF_ParamIndex, 4> kCornerDirectionParams = {
    kParamSurfaceCornerDirection,
    kParamSurfaceCorner2Direction,
    kParamSurfaceCorner3Direction,
    kParamSurfaceCorner4Direction};
constexpr std::array<PF_ParamIndex, 4> kCornerLengthParams = {
    kParamSurfaceCornerLength,
    kParamSurfaceCorner2Length,
    kParamSurfaceCorner3Length,
    kParamSurfaceCorner4Length};
constexpr std::array<PF_ParamIndex, 4> kTwistAngleParams = {
    kParamSurfaceTwistAngle,
    kParamSurfaceTwist2Angle,
    kParamSurfaceTwist3Angle,
    kParamSurfaceTwist4Angle};
constexpr std::array<PF_ParamIndex, 4> kTwistFalloffParams = {
    kParamSurfaceTwistFalloff,
    kParamSurfaceTwist2Falloff,
    kParamSurfaceTwist3Falloff,
    kParamSurfaceTwist4Falloff};

template <std::size_t Size>
bool ContainsParam(
    PF_ParamIndex index,
    const std::array<PF_ParamIndex, Size>& parameters) {
    return std::find(parameters.begin(), parameters.end(), index) !=
           parameters.end();
}

inline bool IsCornerCurlValueParam(PF_ParamIndex index) {
    return ContainsParam(index, kCornerAmountParams) ||
           ContainsParam(index, kCornerRadiusParams) ||
           ContainsParam(index, kCornerDirectionParams) ||
           ContainsParam(index, kCornerLengthParams);
}

inline bool IsEdgeTwistValueParam(PF_ParamIndex index) {
    return ContainsParam(index, kTwistAngleParams) ||
           ContainsParam(index, kTwistFalloffParams);
}

inline PF_ParamIndex PrimaryAnimationParam(std::size_t property) {
    if (property < 16) {
        return kParamPoint00 + static_cast<PF_ParamIndex>(property);
    }
    if (property < 32) {
        return kParamDepth00 + static_cast<PF_ParamIndex>(property - 16);
    }
    if (property < 35) {
        return kParamRotationX + static_cast<PF_ParamIndex>(property - 32);
    }
    switch (property) {
        case 35: return kParamSurfaceSizeX;
        case 36: return kParamSurfaceSizeY;
        case 37: return kParamSurfacePosition;
        case 38: return kParamSurfaceRotationOriginMode;
        case 39: return kParamSurfaceRotationOriginX;
        case 40: return kParamSurfaceRotationOriginY;
        case 41: return kParamSurfaceScaleX;
        case 42: return kParamSurfaceScaleY;
        case 43: return kParamSurfaceScaleZ;
        case 44: return kParamSurfaceDivisionsX;
        case 45: return kParamSurfaceDivisionsY;
        case 46: return kParamSurfaceThickness;
        case 47: return kParamSurfaceBendX;
        case 48: return kParamSurfaceBendY;
        case 49: return kParamSurfaceRollEdge;
        case 50: return kParamSurfaceRollAngle;
        case 51: return kParamSurfaceRollLength;
        default: break;
    }
    if (property < 68) {
        const std::size_t offset = property - 52;
        const std::size_t corner = offset / 4;
        switch (offset % 4) {
            case 0: return kCornerAmountParams[corner];
            case 1: return kCornerRadiusParams[corner];
            case 2: return kCornerDirectionParams[corner];
            default: return kCornerLengthParams[corner];
        }
    }
    if (property < 76) {
        const std::size_t offset = property - 68;
        const std::size_t edge = offset / 2;
        return offset % 2 == 0
                   ? kTwistAngleParams[edge]
                   : kTwistFalloffParams[edge];
    }
    switch (property) {
        case 76: return kParamSurfaceSourceSlot;
        case 77: return kParamSurfaceBackSourceSlot;
        case 78: return kParamSurfaceImageSize;
        case 79: return kParamSurfaceImageBorder;
        case 80: return kParamSurfaceOpacity;
        case 81: return kParamSurfaceDiffuse;
        case 82: return kParamSurfaceSpecular;
        default: return kParamSurfaceShininess;
    }
}

inline PF_ParamIndex AnimationBankTopicParam(std::uint32_t bank) {
    return kParamSurfaceAnimationBanksStart +
           static_cast<PF_ParamIndex>(bank - 1) *
               kSurfaceAnimationBankStride;
}

inline PF_ParamIndex AnimationBankParam(
    std::uint32_t bank,
    std::size_t property) {
    if (bank == 0) {
        return PrimaryAnimationParam(property);
    }
    return AnimationBankTopicParam(bank) + 1 +
           static_cast<PF_ParamIndex>(property);
}

struct AnimationParamAddress {
    bool valid{};
    std::uint32_t bank{};
    std::size_t property{};
    PF_ParamIndex canonical{};
};

inline AnimationParamAddress ResolveAnimationParam(PF_ParamIndex parameter) {
    if (parameter >= kParamSurfaceAnimationBanksStart &&
        parameter < kParamSurfacesEnd) {
        const PF_ParamIndex offset =
            parameter - kParamSurfaceAnimationBanksStart;
        const PF_ParamIndex within = offset % kSurfaceAnimationBankStride;
        if (within > 0 && within <= kSurfaceAnimationPropertyCount) {
            const std::size_t property = static_cast<std::size_t>(within - 1);
            return {
                true,
                static_cast<std::uint32_t>(offset /
                                           kSurfaceAnimationBankStride) + 1,
                property,
                PrimaryAnimationParam(property)};
        }
        return {};
    }
    for (std::size_t property = 0;
         property < static_cast<std::size_t>(kSurfaceAnimationPropertyCount);
         ++property) {
        if (PrimaryAnimationParam(property) == parameter) {
            return {true, 0, property, parameter};
        }
    }
    return {};
}

inline std::array<PF_ParamDef*, kParamCount> BuildAnimationParameterView(
    PF_ParamDef* params[],
    std::uint32_t bank) {
    std::array<PF_ParamDef*, kParamCount> view{};
    std::copy(params, params + kParamCount, view.begin());
    for (std::size_t property = 0;
         property < static_cast<std::size_t>(kSurfaceAnimationPropertyCount);
         ++property) {
        view[static_cast<std::size_t>(PrimaryAnimationParam(property))] =
            params[AnimationBankParam(bank, property)];
    }
    return view;
}

struct StoredPoint3 {
    float x{};
    float y{};
    float z{};
};

struct SurfaceDataV1 {
    std::uint32_t id{};
    std::uint32_t enabled{};
    StoredPoint3 control_points[16]{};
    float rotation_x{};
    float rotation_y{};
    float rotation_z{};
    std::uint32_t reserved[5]{};
};

struct SceneDataV1 {
    std::uint32_t magic{};
    std::uint32_t schema_version{};
    std::uint32_t active{};
    std::uint32_t surface_count{};
    std::uint32_t selected_surface{};
    std::uint32_t next_surface_id{};
    std::uint32_t reserved[10]{};
    SurfaceDataV1 surfaces[kMaximumSurfaces]{};
};

struct SurfaceDataV2 {
    std::uint32_t id{};
    std::uint32_t enabled{};
    StoredPoint3 control_points[16]{};
    float rotation_x{};
    float rotation_y{};
    float rotation_z{};
    float size_x{};
    float size_y{};
    float position_x{};
    float position_y{};
    float position_z{};
    float scale_x{100.0F};
    float scale_y{100.0F};
    float scale_z{100.0F};
    std::uint32_t transform_mode{};
    std::uint32_t reserved[3]{};
};

struct SceneDataV2 {
    std::uint32_t magic{};
    std::uint32_t schema_version{};
    std::uint32_t active{};
    std::uint32_t surface_count{};
    std::uint32_t selected_surface{};
    std::uint32_t next_surface_id{};
    std::uint32_t reserved[10]{};
    SurfaceDataV2 surfaces[kMaximumSurfaces]{};
};

struct SurfaceDataV3 {
    std::uint32_t id{};
    std::uint32_t enabled{};
    StoredPoint3 control_points[16]{};
    float rotation_x{};
    float rotation_y{};
    float rotation_z{};
    float size_x{};
    float size_y{};
    float position_x{};
    float position_y{};
    float position_z{};
    float scale_x{100.0F};
    float scale_y{100.0F};
    float scale_z{100.0F};
    std::uint32_t divisions_x{};
    std::uint32_t divisions_y{};
    std::uint32_t transform_mode{};
    std::uint32_t reserved[1]{};
};

struct SceneDataV3 {
    std::uint32_t magic{};
    std::uint32_t schema_version{};
    std::uint32_t active{};
    std::uint32_t surface_count{};
    std::uint32_t selected_surface{};
    std::uint32_t next_surface_id{};
    std::uint32_t reserved[10]{};
    SurfaceDataV3 surfaces[kMaximumSurfaces]{};
};

struct SurfaceDataV4 {
    std::uint32_t id{};
    std::uint32_t enabled{};
    StoredPoint3 control_points[16]{};
    float rotation_x{};
    float rotation_y{};
    float rotation_z{};
    float size_x{};
    float size_y{};
    float position_x{};
    float position_y{};
    float position_z{};
    float scale_x{100.0F};
    float scale_y{100.0F};
    float scale_z{100.0F};
    std::uint32_t divisions_x{};
    std::uint32_t divisions_y{};
    std::uint32_t transform_mode{};
    std::uint32_t source_slot{};
    std::uint32_t image_size_mode{kImageSizeStretch};
    std::uint32_t image_border_mode{kImageBorderClamp};
    float opacity{100.0F};
    std::uint32_t reserved[1]{};
};

struct SceneDataV4 {
    std::uint32_t magic{};
    std::uint32_t schema_version{};
    std::uint32_t active{};
    std::uint32_t surface_count{};
    std::uint32_t selected_surface{};
    std::uint32_t next_surface_id{};
    std::uint32_t reserved[10]{};
    SurfaceDataV4 surfaces[kMaximumSurfaces]{};
};

struct SurfaceDataV5 {
    std::uint32_t id{};
    std::uint32_t enabled{};
    StoredPoint3 control_points[16]{};
    float rotation_x{};
    float rotation_y{};
    float rotation_z{};
    float size_x{};
    float size_y{};
    float position_x{};
    float position_y{};
    float position_z{};
    float scale_x{100.0F};
    float scale_y{100.0F};
    float scale_z{100.0F};
    std::uint32_t divisions_x{};
    std::uint32_t divisions_y{};
    std::uint32_t transform_mode{};
    std::uint32_t source_slot{};
    std::uint32_t image_size_mode{kImageSizeStretch};
    std::uint32_t image_border_mode{kImageBorderClamp};
    float opacity{100.0F};
    float thickness{};
};

struct SceneDataV5 {
    std::uint32_t magic{};
    std::uint32_t schema_version{};
    std::uint32_t active{};
    std::uint32_t surface_count{};
    std::uint32_t selected_surface{};
    std::uint32_t next_surface_id{};
    std::uint32_t reserved[10]{};
    SurfaceDataV5 surfaces[kMaximumSurfaces]{};
};

struct SurfaceDataV6 {
    std::uint32_t id{};
    std::uint32_t enabled{};
    StoredPoint3 control_points[16]{};
    float rotation_x{};
    float rotation_y{};
    float rotation_z{};
    float size_x{};
    float size_y{};
    float position_x{};
    float position_y{};
    float position_z{};
    float scale_x{100.0F};
    float scale_y{100.0F};
    float scale_z{100.0F};
    std::uint32_t divisions_x{};
    std::uint32_t divisions_y{};
    std::uint32_t transform_mode{};
    std::uint32_t source_slot{};
    std::uint32_t image_size_mode{kImageSizeStretch};
    std::uint32_t image_border_mode{kImageBorderClamp};
    float opacity{100.0F};
    float thickness{};
    float diffuse{100.0F};
    float specular{50.0F};
    float shininess{32.0F};
};

struct SceneDataV6 {
    std::uint32_t magic{};
    std::uint32_t schema_version{};
    std::uint32_t active{};
    std::uint32_t surface_count{};
    std::uint32_t selected_surface{};
    std::uint32_t next_surface_id{};
    std::uint32_t reserved[10]{};
    SurfaceDataV6 surfaces[kMaximumSurfaces]{};
};

struct SurfaceDataV7 {
    std::uint32_t id{};
    std::uint32_t enabled{};
    StoredPoint3 control_points[16]{};
    float rotation_x{};
    float rotation_y{};
    float rotation_z{};
    float size_x{};
    float size_y{};
    float position_x{};
    float position_y{};
    float position_z{};
    float scale_x{100.0F};
    float scale_y{100.0F};
    float scale_z{100.0F};
    std::uint32_t divisions_x{};
    std::uint32_t divisions_y{};
    std::uint32_t transform_mode{};
    std::uint32_t source_slot{};
    std::uint32_t image_size_mode{kImageSizeStretch};
    std::uint32_t image_border_mode{kImageBorderClamp};
    float opacity{100.0F};
    float thickness{};
    float diffuse{100.0F};
    float specular{50.0F};
    float shininess{32.0F};
    float bend_x{};
    float bend_y{};
    float roll_angle{};
    float roll_length{25.0F};
    std::uint32_t roll_edge{kRollEdgeRight};
};

struct SceneDataV7 {
    std::uint32_t magic{};
    std::uint32_t schema_version{};
    std::uint32_t active{};
    std::uint32_t surface_count{};
    std::uint32_t selected_surface{};
    std::uint32_t next_surface_id{};
    std::uint32_t reserved[10]{};
    SurfaceDataV7 surfaces[kMaximumSurfaces]{};
};

struct CornerCurlData {
    float amount{};
    float radius{15.0F};
    float direction{45.0F};
    float length{30.0F};
};

struct SurfaceDataV8 {
    std::uint32_t id{};
    std::uint32_t enabled{};
    StoredPoint3 control_points[16]{};
    float rotation_x{};
    float rotation_y{};
    float rotation_z{};
    float size_x{};
    float size_y{};
    float position_x{};
    float position_y{};
    float position_z{};
    float scale_x{100.0F};
    float scale_y{100.0F};
    float scale_z{100.0F};
    std::uint32_t divisions_x{};
    std::uint32_t divisions_y{};
    std::uint32_t transform_mode{};
    std::uint32_t source_slot{};
    std::uint32_t image_size_mode{kImageSizeStretch};
    std::uint32_t image_border_mode{kImageBorderClamp};
    float opacity{100.0F};
    float thickness{};
    float diffuse{100.0F};
    float specular{50.0F};
    float shininess{32.0F};
    float bend_x{};
    float bend_y{};
    float roll_angle{};
    float roll_length{25.0F};
    std::uint32_t roll_edge{kRollEdgeRight};
    CornerCurlData corner_curls[4]{};
    std::uint32_t selected_corner{kCornerTopLeft};
};

struct SceneDataV8 {
    std::uint32_t magic{};
    std::uint32_t schema_version{};
    std::uint32_t active{};
    std::uint32_t surface_count{};
    std::uint32_t selected_surface{};
    std::uint32_t next_surface_id{};
    std::uint32_t reserved[10]{};
    SurfaceDataV8 surfaces[kMaximumSurfaces]{};
};

struct EdgeTwistData {
    float angle{};
    float falloff{100.0F};
};

struct SurfaceDataV9 {
    std::uint32_t id{};
    std::uint32_t enabled{};
    StoredPoint3 control_points[16]{};
    float rotation_x{};
    float rotation_y{};
    float rotation_z{};
    float size_x{};
    float size_y{};
    float position_x{};
    float position_y{};
    float position_z{};
    float scale_x{100.0F};
    float scale_y{100.0F};
    float scale_z{100.0F};
    std::uint32_t divisions_x{};
    std::uint32_t divisions_y{};
    std::uint32_t transform_mode{};
    std::uint32_t source_slot{};
    std::uint32_t image_size_mode{kImageSizeStretch};
    std::uint32_t image_border_mode{kImageBorderClamp};
    float opacity{100.0F};
    float thickness{};
    float diffuse{100.0F};
    float specular{50.0F};
    float shininess{32.0F};
    float bend_x{};
    float bend_y{};
    float roll_angle{};
    float roll_length{25.0F};
    std::uint32_t roll_edge{kRollEdgeRight};
    CornerCurlData corner_curls[4]{};
    std::uint32_t selected_corner{kCornerTopLeft};
    EdgeTwistData edge_twists[4]{};
    std::uint32_t selected_twist_edge{kTwistEdgeLeft};
};

struct SceneDataV9 {
    std::uint32_t magic{};
    std::uint32_t schema_version{};
    std::uint32_t active{};
    std::uint32_t surface_count{};
    std::uint32_t selected_surface{};
    std::uint32_t next_surface_id{};
    std::uint32_t reserved[10]{};
    SurfaceDataV9 surfaces[kMaximumSurfaces]{};
};

struct SurfaceDataV11 {
    std::uint32_t id{};
    std::uint32_t enabled{};
    StoredPoint3 control_points[16]{};
    float rotation_x{};
    float rotation_y{};
    float rotation_z{};
    float size_x{};
    float size_y{};
    float position_x{};
    float position_y{};
    float position_z{};
    float scale_x{100.0F};
    float scale_y{100.0F};
    float scale_z{100.0F};
    std::uint32_t divisions_x{};
    std::uint32_t divisions_y{};
    std::uint32_t transform_mode{};
    std::uint32_t source_slot{};
    std::uint32_t image_size_mode{kImageSizeStretch};
    std::uint32_t image_border_mode{kImageBorderClamp};
    float opacity{100.0F};
    float thickness{};
    float diffuse{100.0F};
    float specular{50.0F};
    float shininess{32.0F};
    float bend_x{};
    float bend_y{};
    float roll_angle{};
    float roll_length{25.0F};
    std::uint32_t roll_edge{kRollEdgeRight};
    CornerCurlData corner_curls[4]{};
    std::uint32_t selected_corner{kCornerTopLeft};
    EdgeTwistData edge_twists[4]{};
    std::uint32_t selected_twist_edge{kTwistEdgeLeft};
    std::uint32_t rotation_origin_mode{kRotationOriginCenter};
    float rotation_origin_x{50.0F};
    float rotation_origin_y{50.0F};
};

struct SceneDataV11 {
    std::uint32_t magic{};
    std::uint32_t schema_version{};
    std::uint32_t active{};
    std::uint32_t surface_count{};
    std::uint32_t selected_surface{};
    std::uint32_t next_surface_id{};
    std::uint32_t reserved[10]{};
    SurfaceDataV11 surfaces[kMaximumSurfaces]{};
};

struct SurfaceDataV12 {
    std::uint32_t id{};
    std::uint32_t enabled{};
    StoredPoint3 control_points[16]{};
    float rotation_x{};
    float rotation_y{};
    float rotation_z{};
    float size_x{};
    float size_y{};
    float position_x{};
    float position_y{};
    float position_z{};
    float scale_x{100.0F};
    float scale_y{100.0F};
    float scale_z{100.0F};
    std::uint32_t divisions_x{};
    std::uint32_t divisions_y{};
    std::uint32_t transform_mode{};
    std::uint32_t source_slot{};
    std::uint32_t image_size_mode{kImageSizeStretch};
    std::uint32_t image_border_mode{kImageBorderClamp};
    float opacity{100.0F};
    float thickness{};
    float diffuse{100.0F};
    float specular{50.0F};
    float shininess{32.0F};
    float bend_x{};
    float bend_y{};
    float roll_angle{};
    float roll_length{25.0F};
    std::uint32_t roll_edge{kRollEdgeRight};
    CornerCurlData corner_curls[4]{};
    std::uint32_t selected_corner{kCornerTopLeft};
    EdgeTwistData edge_twists[4]{};
    std::uint32_t selected_twist_edge{kTwistEdgeLeft};
    std::uint32_t rotation_origin_mode{kRotationOriginCenter};
    float rotation_origin_x{50.0F};
    float rotation_origin_y{50.0F};
    std::uint32_t back_source_slot{};
};

struct SceneDataV12 {
    std::uint32_t magic{};
    std::uint32_t schema_version{};
    std::uint32_t active{};
    std::uint32_t surface_count{};
    std::uint32_t selected_surface{};
    std::uint32_t next_surface_id{};
    std::uint32_t reserved[10]{};
    SurfaceDataV12 surfaces[kMaximumSurfaces]{};
};

struct SurfaceData {
    std::uint32_t id{};
    std::uint32_t enabled{};
    StoredPoint3 control_points[16]{};
    float rotation_x{};
    float rotation_y{};
    float rotation_z{};
    float size_x{};
    float size_y{};
    float position_x{};
    float position_y{};
    float position_z{};
    float scale_x{100.0F};
    float scale_y{100.0F};
    float scale_z{100.0F};
    std::uint32_t divisions_x{};
    std::uint32_t divisions_y{};
    std::uint32_t transform_mode{};
    std::uint32_t source_slot{};
    std::uint32_t image_size_mode{kImageSizeStretch};
    std::uint32_t image_border_mode{kImageBorderClamp};
    float opacity{100.0F};
    float thickness{};
    float diffuse{100.0F};
    float specular{50.0F};
    float shininess{32.0F};
    float bend_x{};
    float bend_y{};
    float roll_angle{};
    float roll_length{25.0F};
    std::uint32_t roll_edge{kRollEdgeRight};
    CornerCurlData corner_curls[4]{};
    std::uint32_t selected_corner{kCornerTopLeft};
    EdgeTwistData edge_twists[4]{};
    std::uint32_t selected_twist_edge{kTwistEdgeLeft};
    std::uint32_t rotation_origin_mode{kRotationOriginCenter};
    float rotation_origin_x{50.0F};
    float rotation_origin_y{50.0F};
    // 0 follows source_slot; 1..8 explicitly select Source Layer slots 1..8.
    std::uint32_t back_source_slot{};
    std::uint32_t animation_bank{};
};

struct SceneData {
    std::uint32_t magic{};
    std::uint32_t schema_version{};
    std::uint32_t active{};
    std::uint32_t surface_count{};
    std::uint32_t selected_surface{};
    std::uint32_t next_surface_id{};
    std::uint32_t reserved[10]{};
    SurfaceData surfaces[kMaximumSurfaces]{};
};

static_assert(std::is_trivially_copyable_v<SceneData>);
static_assert(std::is_standard_layout_v<SceneData>);
static_assert(std::is_trivially_copyable_v<SceneDataV1>);
static_assert(std::is_trivially_copyable_v<SceneDataV2>);
static_assert(std::is_trivially_copyable_v<SceneDataV3>);
static_assert(std::is_trivially_copyable_v<SceneDataV4>);
static_assert(std::is_trivially_copyable_v<SceneDataV5>);
static_assert(std::is_trivially_copyable_v<SceneDataV6>);
static_assert(std::is_trivially_copyable_v<SceneDataV7>);
static_assert(std::is_trivially_copyable_v<SceneDataV8>);
static_assert(std::is_trivially_copyable_v<SceneDataV9>);
static_assert(std::is_trivially_copyable_v<SceneDataV11>);
static_assert(std::is_trivially_copyable_v<SceneDataV12>);
static_assert(sizeof(SceneDataV1) == 1920);
static_assert(sizeof(SceneDataV2) == 2144);
static_assert(sizeof(SceneDataV3) == 2144);
static_assert(sizeof(SceneDataV4) == 2272);
static_assert(sizeof(SceneDataV5) == 2272);
static_assert(sizeof(SceneDataV6) == 2368);
static_assert(sizeof(SceneDataV7) == 2528);
static_assert(sizeof(SceneDataV8) == 3072);
static_assert(sizeof(SceneDataV9) == 3360);
static_assert(offsetof(SurfaceData, rotation_origin_mode) == sizeof(SurfaceDataV9));
static_assert(sizeof(SceneDataV11) == 3456);
static_assert(offsetof(SurfaceData, back_source_slot) == sizeof(SurfaceDataV11));
static_assert(sizeof(SceneDataV12) == 3488);
static_assert(offsetof(SurfaceData, animation_bank) == sizeof(SurfaceDataV12));
static_assert(sizeof(SceneData) == 3520);

struct Vertex {
    double x{};
    double y{};
    double u{};
    double v{};
    double inverse_depth{};
    Point3 world_position{};
    Point3 normal{};
    bool visible{};
};

struct CameraState {
    Point3 position{};
    Point3 right{1.0, 0.0, 0.0};
    Point3 down{0.0, 1.0, 0.0};
    Point3 forward{0.0, 0.0, 1.0};
    double rotation_x{};
    double rotation_y{};
    double rotation_z{};
    double focal_distance{};
    double center_x{};
    double center_y{};
    double output_offset_x{};
    double output_offset_y{};
    bool perspective{};
    bool use_basis{};
};

struct Bounds2D {
    double minimum_x{};
    double minimum_y{};
    double maximum_x{};
    double maximum_y{};
};

enum class RenderLightType {
    Directional,
    Point,
    Spot
};

struct RenderLight {
    RenderLightType type{RenderLightType::Directional};
    Point3 position{};
    Point3 direction{0.0, 0.0, -1.0};
    Point3 forward{0.0, 0.0, 1.0};
    Point3 color{1.0, 1.0, 1.0};
    double intensity{1.0};
    double cone_angle{90.0};
    double cone_feather{};
};

struct LightingState {
    std::array<RenderLight, kMaximumRenderLights> lights{};
    std::size_t light_count{};
    Point3 camera_position{};
    Point3 ambient{0.2, 0.2, 0.2};
    bool enabled{};
    bool backface_culling{};
    A_long texture_filter{kTextureFilterBilinear};
};

enum class TextureFace {
    Automatic,
    Front,
    Back
};
