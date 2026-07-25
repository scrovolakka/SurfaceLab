#pragma once

#include "SurfaceLab.h"
#include "AE_GeneralPlug.h"
#include "SurfaceLabGeometry.h"
#include "SurfaceLabModel.h"

#include <array>
#include <cstddef>
#include <cstdint>

struct GlobalData {
    AEGP_PluginID plugin_id{};
};

constexpr A_long kTextureFilterBilinear = 2;
constexpr A_long kTextureFilterNearest = 1;
constexpr A_long kRenderViewFinish = 1;
constexpr A_long kRenderViewDepth = 2;
constexpr A_long kRenderViewUv = 3;
constexpr A_long kRenderViewNormalsViewSpace = 4;
constexpr std::size_t kMaximumRenderLights = 8;

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
    SceneCoordinateTransform scene_transform{};
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
    double input_center_x{};
    double input_center_y{};
    double output_offset_x{};
    double output_offset_y{};
    Affine2D comp_to_output{};
    bool perspective{true};
    bool use_basis{};
    bool use_comp_to_output{};
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
    double shadow_darkness{1.0};
    double shadow_diffusion{};
    bool casts_shadows{};
};

struct LightingState {
    std::array<RenderLight, kMaximumRenderLights> lights{};
    std::size_t light_count{};
    Point3 camera_position{};
    Point3 ambient{1.0, 1.0, 1.0};
    bool enabled{true};
    bool backface_culling{};
    A_long texture_filter{kTextureFilterBilinear};
};

enum class TextureFace {
    Automatic,
    Front,
    Back
};

SceneData ResolveSceneForFrame(
    PF_InData* in_data,
    PF_ParamDef* params[],
    A_long input_width,
    A_long input_height);

void* LatticeRefcon(std::uint32_t surface);

PF_Err CreateLatticeHandle(
    PF_InData* in_data,
    PF_ArbitraryH* destination,
    double width,
    double height,
    std::uint32_t surface);

PF_Err HandleArbitrary(PF_InData* in_data, PF_ArbParamsExtra* extra);
