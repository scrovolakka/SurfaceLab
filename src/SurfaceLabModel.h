#pragma once

// Host-independent SurfaceLab scene data model.
//
// This header deliberately depends on NOTHING from the After Effects SDK: only
// fixed-width integers and floats. It defines the persisted scene model and its
// full chain of versioned on-disk structs (V1..V13) plus the tuning constants
// baked into their defaults. Keeping it AE-free lets the migration and geometry
// logic be compiled and unit-tested on any platform (see tests/), independent
// of the licensed macOS-only plug-in build.

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

// A stored division count of 0 follows the legacy tessellation setting. Clamp
// the resolved value at the point of use as a second line of defense against a
// corrupt or partially initialized scene reaching a fixed 33 x 33 grid.
inline std::uint32_t ResolveDivisions(
    std::uint32_t divisions,
    std::uint32_t legacy_tessellation) {
    return std::clamp(
        divisions == 0 ? legacy_tessellation : divisions,
        kMinimumDivisions,
        kMaximumDivisions);
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

// --- Scene model operations (defined in SurfaceLabModel.cpp) --------------
// Pure, host-independent: initialization, validation, and the V1..V13 on-disk
// migration chain. No After Effects SDK dependency, so they are unit-tested
// directly (see tests/model_tests.cpp).

void UpdateDerivedTransform(SurfaceData& surface);
void InitializeEdgeTwists(SurfaceData& surface);
void InitializeCornerCurls(SurfaceData& surface);
void InitializeMaterial(SurfaceData& surface);
void InitializeFlatSurface(
    SurfaceData& surface,
    std::uint32_t id,
    double width,
    double height,
    bool use_local_transform);
void InitializeScene(SceneData& scene, double width, double height);
void AssignLegacyAnimationBanks(SceneData& scene);

bool IsValidScene(const SceneData& scene);
bool IsValidSceneV1(const SceneDataV1& scene);
bool IsValidSceneV2(const SceneDataV2& scene);
bool IsValidSceneV3(const SceneDataV3& scene);
bool IsValidSceneV4(const SceneDataV4& scene);
bool IsValidSceneV5(const SceneDataV5& scene);
bool IsValidSceneV6(const SceneDataV6& scene);
bool IsValidSceneV7(const SceneDataV7& scene);
bool IsValidSceneV8(const SceneDataV8& scene);
bool IsValidSceneV9(const SceneDataV9& scene);

void MigrateSceneV1(const SceneDataV1& source, SceneData& destination);
void MigrateSceneV2(const SceneDataV2& source, SceneData& destination);
void MigrateSceneV3(const SceneDataV3& source, SceneData& destination);
void MigrateSceneV4(const SceneDataV4& source, SceneData& destination);
void MigrateSceneV5(const SceneDataV5& source, SceneData& destination);
void MigrateSceneV6(const SceneDataV6& source, SceneData& destination);
void MigrateSceneV7(const SceneDataV7& source, SceneData& destination);
void MigrateSceneV8(const SceneDataV8& source, SceneData& destination);
void MigrateSceneV9(const SceneDataV9& source, SceneData& destination);
void MigrateSceneV10(const SceneDataV11& source, SceneData& destination);
void MigrateSceneV11(const SceneDataV11& source, SceneData& destination);
void MigrateSceneV12(const SceneDataV12& source, SceneData& destination);
