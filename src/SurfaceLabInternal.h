#pragma once

// AE-coupled internal declarations for SurfaceLab.
//
// Everything here needs the After Effects SDK (PF_/AEGP/A_long types): the
// global-data block, render-time helper structs, the parameter-index tables,
// and the pure parameter-mapping helpers. The host-independent scene data model
// lives in SurfaceLabModel.h, which this header re-exports. The public plug-in
// surface stays in SurfaceLab.h.

#include "SurfaceLab.h"
#include "SurfaceLabModel.h"
#include "SurfaceLabGeometry.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

struct GlobalData {
    AEGP_PluginID plugin_id{};
};

constexpr A_long kTextureFilterNearest = 1;
constexpr A_long kTextureFilterBilinear = 2;
constexpr A_long kOutputBoundsSource = 1;
constexpr A_long kOutputBoundsAuto = 2;
constexpr A_long kOutputBoundsFixed = 3;
constexpr A_long kDefaultOutputPadding = 32;
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
