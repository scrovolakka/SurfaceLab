#pragma once

// Host-independent SurfaceLab v1 model. There is deliberately no 0.x
// migration surface here: v1 is a new, incompatible format.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

struct Point2 {
    double x{};
    double y{};
};

struct Point3 {
    double x{};
    double y{};
    double z{};
};

// Transient row-vector affine transform. Root-controller state uses this at
// render time; the persisted lattice payload remains unchanged.
struct Affine3D {
    double xx{1.0};
    double xy{};
    double xz{};
    double yx{};
    double yy{1.0};
    double yz{};
    double zx{};
    double zy{};
    double zz{1.0};
    double tx{};
    double ty{};
    double tz{};
};

struct StoredPoint3 {
    float x{};
    float y{};
    float z{};
};

constexpr std::uint32_t kMaximumSurfaces = 8;
constexpr std::uint32_t kImageSizeStretch = 1;
constexpr std::uint32_t kImageSizeFill = 2;
constexpr std::uint32_t kImageSizeFit = 3;
constexpr std::uint32_t kImageBorderClamp = 1;
constexpr std::uint32_t kImageBorderRepeat = 2;
constexpr std::uint32_t kImageBorderMirror = 3;
constexpr std::uint32_t kImageBorderTransparent = 4;
constexpr std::uint32_t kRotationOriginCenter = 1;
constexpr std::uint32_t kRotationOriginLeftEdge = 2;
constexpr std::uint32_t kRotationOriginRightEdge = 3;
constexpr std::uint32_t kRotationOriginTopEdge = 4;
constexpr std::uint32_t kRotationOriginBottomEdge = 5;
constexpr std::uint32_t kRotationOriginCustom = 6;

constexpr std::uint32_t kLatticeMagic = 0x534C5631U;  // "SLV1"
constexpr std::uint16_t kLatticeSchemaVersion = 1;
constexpr std::uint16_t kLatticeFlagNeedsInputSize = 1U;
constexpr std::uint16_t kMinimumLatticeDivisions = 1;
constexpr std::uint16_t kMaximumLatticeDivisions = 16;
constexpr std::uint16_t kMinimumMeshQuality = 1;
constexpr std::uint16_t kMaximumMeshQuality = 8;
constexpr std::size_t kMaximumLatticeAxisPoints =
    static_cast<std::size_t>(kMaximumLatticeDivisions) + 1;
constexpr std::size_t kMaximumLatticePoints =
    kMaximumLatticeAxisPoints * kMaximumLatticeAxisPoints;

struct LatticeData {
    std::uint32_t magic{kLatticeMagic};
    std::uint16_t schema_version{kLatticeSchemaVersion};
    std::uint16_t divisions_x{3};
    std::uint16_t divisions_y{3};
    std::uint16_t point_count{16};
    std::uint16_t reserved{};
    std::uint64_t surface_id{};
    StoredPoint3 points[kMaximumLatticePoints]{};
};

static_assert(std::is_trivially_copyable_v<LatticeData>);

constexpr std::size_t LatticePointCount(
    std::uint16_t divisions_x,
    std::uint16_t divisions_y) {
    return (static_cast<std::size_t>(divisions_x) + 1) *
           (static_cast<std::size_t>(divisions_y) + 1);
}

constexpr std::size_t LatticePointIndex(
    std::uint16_t divisions_x,
    std::uint16_t row,
    std::uint16_t column) {
    return static_cast<std::size_t>(row) *
               (static_cast<std::size_t>(divisions_x) + 1) +
           column;
}

struct SurfaceData {
    std::uint32_t id{};
    std::uint32_t enabled{};
    std::uint32_t source_slot{};
    std::uint32_t back_source_slot{};
    float position_x{};
    float position_y{};
    float position_z{};
    float rotation_x{};
    float rotation_y{};
    float rotation_z{};
    float scale_x{100.0F};
    float scale_y{100.0F};
    float scale_z{100.0F};
    float size_x{};
    float size_y{};
    std::uint32_t transform_mode{1};
    std::uint32_t rotation_origin_mode{kRotationOriginCenter};
    float rotation_origin_x{50.0F};
    float rotation_origin_y{50.0F};
    std::uint32_t divisions_x{3};
    std::uint32_t divisions_y{3};
    std::uint32_t image_size_mode{kImageSizeStretch};
    std::uint32_t image_border_mode{kImageBorderClamp};
    float image_position_x{};
    float image_position_y{};
    float image_rotation{};
    float image_scale{100.0F};
    float opacity{100.0F};
    float thickness{};
    float diffuse{100.0F};
    float specular{};
    float metalness{};
    float shininess{32.0F};
    // Procedural roll layer (degrees / cage units). Not part of lattice wire.
    float roll_angle{};
    float roll_tilt{};
    float roll_radius{200.0F};
    float roll_expand{};
    LatticeData lattice{};
    std::uint16_t mesh_quality{4};
    std::uint16_t reserved{};
    std::uint32_t root_transform_enabled{};
    Affine3D root_world_transform{};
};

struct SceneData {
    std::uint32_t surface_count{1};
    SurfaceData surfaces[kMaximumSurfaces]{};
};

inline std::uint32_t ResolveDivisions(
    std::uint32_t divisions,
    std::uint32_t) {
    return std::clamp<std::uint32_t>(
        divisions,
        kMinimumLatticeDivisions,
        kMaximumLatticeDivisions);
}

void InitializeLattice(
    LatticeData& lattice,
    std::uint16_t divisions_x,
    std::uint16_t divisions_y,
    double width,
    double height,
    std::uint64_t surface_id = 0);

bool IsValidLattice(const LatticeData& lattice);

// ParamsSetup does not expose reliable input dimensions. New lattices carry a
// persistent one-shot flag until the first real frame, and v1.2.1-v1.2.4 may
// contain a canonical 1x1 placeholder. Both need input-sized initialization.
bool NeedsInputSizedInitialization(const LatticeData& lattice);

bool ResizeLattice(
    const LatticeData& source,
    std::uint16_t divisions_x,
    std::uint16_t divisions_y,
    LatticeData& destination);

bool InterpolateLattice(
    const LatticeData& left,
    const LatticeData& right,
    double amount,
    LatticeData& destination);

bool CompareLattices(
    const LatticeData& first,
    const LatticeData& second,
    double epsilon = 0.0);

std::vector<std::uint8_t> FlattenLattice(const LatticeData& lattice);

bool UnflattenLattice(
    const void* bytes,
    std::size_t byte_count,
    LatticeData& lattice);

void UpdateDerivedTransform(SurfaceData& surface);

void InitializeFlatSurface(
    SurfaceData& surface,
    std::uint32_t id,
    double width,
    double height,
    bool use_local_transform = true);

void InitializeScene(SceneData& scene, double width, double height);

bool IsValidScene(const SceneData& scene);

Point2 TransformImageCoordinates(
    const SurfaceData& surface,
    double u,
    double v);

std::vector<std::int64_t> BuildSubframeSampleTimes(
    std::int64_t current_time,
    std::int64_t time_step,
    double shutter_angle,
    double shutter_phase,
    std::uint32_t requested_samples);
