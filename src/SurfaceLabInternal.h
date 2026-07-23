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

// A surface's stored division count of 0 means "follow the legacy
// tessellation parameter". Shared by the UI bridges and the renderer.
inline std::uint32_t ResolveDivisions(
    std::uint32_t divisions,
    std::uint32_t legacy_tessellation) {
    return divisions == 0 ? legacy_tessellation : divisions;
}

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

// Animation property index -> canonical (bank 0) parameter index. The layout
// is the wire order of SurfaceData's animatable fields; see the boundary
// static_asserts below when appending properties.
constexpr std::array<PF_ParamIndex, kSurfaceAnimationPropertyCount>
    kPrimaryAnimationParams = {
        // 0-15: control points, row-major 4x4
        kParamPoint00, kParamPoint01, kParamPoint02, kParamPoint03,
        kParamPoint10, kParamPoint11, kParamPoint12, kParamPoint13,
        kParamPoint20, kParamPoint21, kParamPoint22, kParamPoint23,
        kParamPoint30, kParamPoint31, kParamPoint32, kParamPoint33,
        // 16-31: control-point depths, row-major 4x4
        kParamDepth00, kParamDepth01, kParamDepth02, kParamDepth03,
        kParamDepth10, kParamDepth11, kParamDepth12, kParamDepth13,
        kParamDepth20, kParamDepth21, kParamDepth22, kParamDepth23,
        kParamDepth30, kParamDepth31, kParamDepth32, kParamDepth33,
        // 32-34: rotation
        kParamRotationX, kParamRotationY, kParamRotationZ,
        // 35-46: size, position, rotation origin, scale, divisions, thickness
        kParamSurfaceSizeX, kParamSurfaceSizeY, kParamSurfacePosition,
        kParamSurfaceRotationOriginMode, kParamSurfaceRotationOriginX,
        kParamSurfaceRotationOriginY,
        kParamSurfaceScaleX, kParamSurfaceScaleY, kParamSurfaceScaleZ,
        kParamSurfaceDivisionsX, kParamSurfaceDivisionsY,
        kParamSurfaceThickness,
        // 47-51: bend and roll
        kParamSurfaceBendX, kParamSurfaceBendY,
        kParamSurfaceRollEdge, kParamSurfaceRollAngle, kParamSurfaceRollLength,
        // 52-67: corner curls, four per corner
        kParamSurfaceCornerAmount, kParamSurfaceCornerRadius,
        kParamSurfaceCornerDirection, kParamSurfaceCornerLength,
        kParamSurfaceCorner2Amount, kParamSurfaceCorner2Radius,
        kParamSurfaceCorner2Direction, kParamSurfaceCorner2Length,
        kParamSurfaceCorner3Amount, kParamSurfaceCorner3Radius,
        kParamSurfaceCorner3Direction, kParamSurfaceCorner3Length,
        kParamSurfaceCorner4Amount, kParamSurfaceCorner4Radius,
        kParamSurfaceCorner4Direction, kParamSurfaceCorner4Length,
        // 68-75: edge twists, two per edge
        kParamSurfaceTwistAngle, kParamSurfaceTwistFalloff,
        kParamSurfaceTwist2Angle, kParamSurfaceTwist2Falloff,
        kParamSurfaceTwist3Angle, kParamSurfaceTwist3Falloff,
        kParamSurfaceTwist4Angle, kParamSurfaceTwist4Falloff,
        // 76-83: material
        kParamSurfaceSourceSlot, kParamSurfaceBackSourceSlot,
        kParamSurfaceImageSize, kParamSurfaceImageBorder,
        kParamSurfaceOpacity, kParamSurfaceDiffuse,
        kParamSurfaceSpecular, kParamSurfaceShininess};

static_assert(kPrimaryAnimationParams[15] == kParamPoint33);
static_assert(kPrimaryAnimationParams[31] == kParamDepth33);
static_assert(kPrimaryAnimationParams[34] == kParamRotationZ);
static_assert(kPrimaryAnimationParams[51] == kParamSurfaceRollLength);
static_assert(kPrimaryAnimationParams[67] == kParamSurfaceCorner4Length);
static_assert(kPrimaryAnimationParams[75] == kParamSurfaceTwist4Falloff);
static_assert(kPrimaryAnimationParams[83] == kParamSurfaceShininess);

inline PF_ParamIndex PrimaryAnimationParam(std::size_t property) {
    // Out-of-range clamps to the last entry, matching the old switch default.
    return kPrimaryAnimationParams[
        std::min(property, kPrimaryAnimationParams.size() - 1)];
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

// Resolves the scene to render for the current frame: the active arbitrary
// scene when valid, otherwise the legacy per-parameter surface. Defined in
// SurfaceLab.cpp next to the arbitrary-data glue; also called from the
// rendering core (FrameSetup / RenderSurface).
SceneData ResolveSceneForFrame(
    PF_InData* in_data,
    PF_ParamDef* params[],
    A_long input_width,
    A_long input_height);

// Arbitrary-data glue, defined in SurfaceLab.cpp. ParamsSetup (in
// SurfaceLabUI.cpp) registers the scene parameter with these.
void* SceneRefcon();
PF_Err CreateSceneHandle(
    PF_InData* in_data,
    PF_ArbitraryH* destination,
    double width,
    double height);
