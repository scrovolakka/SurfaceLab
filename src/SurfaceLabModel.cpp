#include "SurfaceLabModel.h"
#include "SurfaceLabGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace {

void AppendU16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
    bytes.push_back(static_cast<std::uint8_t>(value));
}

void AppendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value >> 24U));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16U));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
    bytes.push_back(static_cast<std::uint8_t>(value));
}

void AppendU64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    AppendU32(bytes, static_cast<std::uint32_t>(value >> 32U));
    AppendU32(bytes, static_cast<std::uint32_t>(value));
}

void AppendFloat(std::vector<std::uint8_t>& bytes, float value) {
    std::uint32_t bits{};
    std::memcpy(&bits, &value, sizeof(bits));
    AppendU32(bytes, bits);
}

bool ReadU16(
    const std::uint8_t*& cursor,
    const std::uint8_t* end,
    std::uint16_t& value) {
    if (end - cursor < 2) {
        return false;
    }
    value = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(cursor[0]) << 8U) | cursor[1]);
    cursor += 2;
    return true;
}

bool ReadU32(
    const std::uint8_t*& cursor,
    const std::uint8_t* end,
    std::uint32_t& value) {
    if (end - cursor < 4) {
        return false;
    }
    value = (static_cast<std::uint32_t>(cursor[0]) << 24U) |
            (static_cast<std::uint32_t>(cursor[1]) << 16U) |
            (static_cast<std::uint32_t>(cursor[2]) << 8U) |
            cursor[3];
    cursor += 4;
    return true;
}

bool ReadU64(
    const std::uint8_t*& cursor,
    const std::uint8_t* end,
    std::uint64_t& value) {
    std::uint32_t high{};
    std::uint32_t low{};
    if (!ReadU32(cursor, end, high) || !ReadU32(cursor, end, low)) {
        return false;
    }
    value = (static_cast<std::uint64_t>(high) << 32U) | low;
    return true;
}

bool ReadFloat(
    const std::uint8_t*& cursor,
    const std::uint8_t* end,
    float& value) {
    std::uint32_t bits{};
    if (!ReadU32(cursor, end, bits)) {
        return false;
    }
    std::memcpy(&value, &bits, sizeof(value));
    return true;
}

bool IsFiniteTransform(const SurfaceData& surface) {
    return std::isfinite(surface.position_x) &&
           std::isfinite(surface.position_y) &&
           std::isfinite(surface.position_z) &&
           std::isfinite(surface.rotation_x) &&
           std::isfinite(surface.rotation_y) &&
           std::isfinite(surface.rotation_z) &&
           std::isfinite(surface.scale_x) &&
           std::isfinite(surface.scale_y) &&
           std::isfinite(surface.scale_z);
}

}  // namespace

void InitializeLattice(
    LatticeData& lattice,
    std::uint16_t divisions_x,
    std::uint16_t divisions_y,
    double width,
    double height,
    std::uint64_t surface_id) {
    lattice = {};
    lattice.magic = kLatticeMagic;
    lattice.schema_version = kLatticeSchemaVersion;
    lattice.divisions_x = std::clamp(
        divisions_x,
        kMinimumLatticeDivisions,
        kMaximumLatticeDivisions);
    lattice.divisions_y = std::clamp(
        divisions_y,
        kMinimumLatticeDivisions,
        kMaximumLatticeDivisions);
    lattice.point_count = static_cast<std::uint16_t>(
        LatticePointCount(lattice.divisions_x, lattice.divisions_y));
    lattice.surface_id = surface_id;
    for (std::uint16_t row = 0; row <= lattice.divisions_y; ++row) {
        for (std::uint16_t column = 0;
             column <= lattice.divisions_x;
             ++column) {
            StoredPoint3& point = lattice.points[LatticePointIndex(
                lattice.divisions_x,
                row,
                column)];
            point.x = static_cast<float>(
                width * column / lattice.divisions_x);
            point.y = static_cast<float>(
                height * row / lattice.divisions_y);
        }
    }
}

bool IsValidLattice(const LatticeData& lattice) {
    if (lattice.magic != kLatticeMagic ||
        lattice.schema_version != kLatticeSchemaVersion ||
        lattice.divisions_x < kMinimumLatticeDivisions ||
        lattice.divisions_x > kMaximumLatticeDivisions ||
        lattice.divisions_y < kMinimumLatticeDivisions ||
        lattice.divisions_y > kMaximumLatticeDivisions ||
        lattice.point_count !=
            LatticePointCount(lattice.divisions_x, lattice.divisions_y)) {
        return false;
    }
    for (std::size_t index = 0; index < lattice.point_count; ++index) {
        const StoredPoint3& point = lattice.points[index];
        if (!std::isfinite(point.x) ||
            !std::isfinite(point.y) ||
            !std::isfinite(point.z)) {
            return false;
        }
    }
    return true;
}

bool NeedsInputSizedInitialization(const LatticeData& lattice) {
    if (!IsValidLattice(lattice) || lattice.point_count == 0) {
        return false;
    }
    if ((lattice.reserved & kLatticeFlagNeedsInputSize) != 0) {
        return true;
    }

    StoredPoint3 minimum = lattice.points[0];
    StoredPoint3 maximum = minimum;
    for (std::size_t index = 1; index < lattice.point_count; ++index) {
        const StoredPoint3& point = lattice.points[index];
        minimum.x = std::min(minimum.x, point.x);
        minimum.y = std::min(minimum.y, point.y);
        minimum.z = std::min(minimum.z, point.z);
        maximum.x = std::max(maximum.x, point.x);
        maximum.y = std::max(maximum.y, point.y);
        maximum.z = std::max(maximum.z, point.z);
    }
    constexpr double kPlaceholderEpsilon = 1.0e-4;
    if (maximum.x - minimum.x <= kPlaceholderEpsilon &&
        maximum.y - minimum.y <= kPlaceholderEpsilon &&
        maximum.z - minimum.z <= kPlaceholderEpsilon) {
        return true;
    }

    // v1.2.1-v1.2.4 clamped an unknown ParamsSetup size to 1x1. Restrict the
    // repair to the exact canonical grid so a deliberately tiny deformation
    // is not mistaken for an uninitialized payload.
    for (std::uint16_t row = 0; row <= lattice.divisions_y; ++row) {
        for (std::uint16_t column = 0;
             column <= lattice.divisions_x;
             ++column) {
            const StoredPoint3& point = lattice.points[LatticePointIndex(
                lattice.divisions_x,
                row,
                column)];
            const double expected_x =
                static_cast<double>(column) / lattice.divisions_x;
            const double expected_y =
                static_cast<double>(row) / lattice.divisions_y;
            if (std::abs(point.x - expected_x) > kPlaceholderEpsilon ||
                std::abs(point.y - expected_y) > kPlaceholderEpsilon ||
                std::abs(point.z) > kPlaceholderEpsilon) {
                return false;
            }
        }
    }
    return true;
}

bool ResizeLattice(
    const LatticeData& source,
    std::uint16_t divisions_x,
    std::uint16_t divisions_y,
    LatticeData& destination) {
    if (!IsValidLattice(source) ||
        divisions_x < kMinimumLatticeDivisions ||
        divisions_x > kMaximumLatticeDivisions ||
        divisions_y < kMinimumLatticeDivisions ||
        divisions_y > kMaximumLatticeDivisions) {
        return false;
    }
    LatticeData resized{};
    InitializeLattice(
        resized,
        divisions_x,
        divisions_y,
        1.0,
        1.0,
        source.surface_id);
    for (std::uint16_t row = 0; row <= divisions_y; ++row) {
        const double v = static_cast<double>(row) / divisions_y;
        for (std::uint16_t column = 0; column <= divisions_x; ++column) {
            const double u = static_cast<double>(column) / divisions_x;
            const Point3 point = EvaluateLattice(source, u, v);
            StoredPoint3& stored = resized.points[LatticePointIndex(
                divisions_x,
                row,
                column)];
            stored = {
                static_cast<float>(point.x),
                static_cast<float>(point.y),
                static_cast<float>(point.z)};
        }
    }
    destination = resized;
    return true;
}

bool InterpolateLattice(
    const LatticeData& left,
    const LatticeData& right,
    double amount,
    LatticeData& destination) {
    if (!IsValidLattice(left) || !IsValidLattice(right) ||
        left.divisions_x != right.divisions_x ||
        left.divisions_y != right.divisions_y ||
        left.surface_id != right.surface_id ||
        !std::isfinite(amount)) {
        return false;
    }
    const double t = std::clamp(amount, 0.0, 1.0);
    destination = left;
    for (std::size_t index = 0; index < left.point_count; ++index) {
        destination.points[index].x = static_cast<float>(
            left.points[index].x +
            (right.points[index].x - left.points[index].x) * t);
        destination.points[index].y = static_cast<float>(
            left.points[index].y +
            (right.points[index].y - left.points[index].y) * t);
        destination.points[index].z = static_cast<float>(
            left.points[index].z +
            (right.points[index].z - left.points[index].z) * t);
    }
    return true;
}

bool CompareLattices(
    const LatticeData& first,
    const LatticeData& second,
    double epsilon) {
    if (!IsValidLattice(first) || !IsValidLattice(second) ||
        first.divisions_x != second.divisions_x ||
        first.divisions_y != second.divisions_y ||
        first.surface_id != second.surface_id) {
        return false;
    }
    const double tolerance = std::max(0.0, epsilon);
    for (std::size_t index = 0; index < first.point_count; ++index) {
        if (std::abs(first.points[index].x - second.points[index].x) >
                tolerance ||
            std::abs(first.points[index].y - second.points[index].y) >
                tolerance ||
            std::abs(first.points[index].z - second.points[index].z) >
                tolerance) {
            return false;
        }
    }
    return true;
}

std::vector<std::uint8_t> FlattenLattice(const LatticeData& lattice) {
    if (!IsValidLattice(lattice)) {
        return {};
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(22 + lattice.point_count * 12);
    AppendU32(bytes, lattice.magic);
    AppendU16(bytes, lattice.schema_version);
    AppendU16(bytes, lattice.divisions_x);
    AppendU16(bytes, lattice.divisions_y);
    AppendU16(bytes, lattice.point_count);
    AppendU16(bytes, lattice.reserved);
    AppendU64(bytes, lattice.surface_id);
    for (std::size_t index = 0; index < lattice.point_count; ++index) {
        AppendFloat(bytes, lattice.points[index].x);
        AppendFloat(bytes, lattice.points[index].y);
        AppendFloat(bytes, lattice.points[index].z);
    }
    return bytes;
}

bool UnflattenLattice(
    const void* bytes,
    std::size_t byte_count,
    LatticeData& lattice) {
    if (!bytes) {
        return false;
    }
    const auto* cursor = static_cast<const std::uint8_t*>(bytes);
    const auto* end = cursor + byte_count;
    LatticeData decoded{};
    if (!ReadU32(cursor, end, decoded.magic) ||
        !ReadU16(cursor, end, decoded.schema_version) ||
        !ReadU16(cursor, end, decoded.divisions_x) ||
        !ReadU16(cursor, end, decoded.divisions_y) ||
        !ReadU16(cursor, end, decoded.point_count) ||
        decoded.point_count > kMaximumLatticePoints) {
        return false;
    }
    const std::size_t legacy_size =
        20 + static_cast<std::size_t>(decoded.point_count) * 12;
    const std::size_t flagged_size =
        22 + static_cast<std::size_t>(decoded.point_count) * 12;
    if (byte_count == flagged_size) {
        if (!ReadU16(cursor, end, decoded.reserved)) {
            return false;
        }
    } else if (byte_count != legacy_size) {
        return false;
    }
    if (!ReadU64(cursor, end, decoded.surface_id)) {
        return false;
    }
    for (std::size_t index = 0; index < decoded.point_count; ++index) {
        if (!ReadFloat(cursor, end, decoded.points[index].x) ||
            !ReadFloat(cursor, end, decoded.points[index].y) ||
            !ReadFloat(cursor, end, decoded.points[index].z)) {
            return false;
        }
    }
    if (cursor != end || !IsValidLattice(decoded)) {
        return false;
    }
    lattice = decoded;
    return true;
}

void UpdateDerivedTransform(SurfaceData& surface) {
    if (!IsValidLattice(surface.lattice)) {
        surface.size_x = 1.0F;
        surface.size_y = 1.0F;
        return;
    }
    float minimum_x = std::numeric_limits<float>::infinity();
    float minimum_y = std::numeric_limits<float>::infinity();
    float maximum_x = -std::numeric_limits<float>::infinity();
    float maximum_y = -std::numeric_limits<float>::infinity();
    for (std::size_t index = 0;
         index < surface.lattice.point_count;
         ++index) {
        const StoredPoint3& point = surface.lattice.points[index];
        minimum_x = std::min(minimum_x, point.x);
        minimum_y = std::min(minimum_y, point.y);
        maximum_x = std::max(maximum_x, point.x);
        maximum_y = std::max(maximum_y, point.y);
    }
    surface.size_x = std::max(0.001F, maximum_x - minimum_x);
    surface.size_y = std::max(0.001F, maximum_y - minimum_y);
}

void InitializeFlatSurface(
    SurfaceData& surface,
    std::uint32_t id,
    double width,
    double height,
    bool use_local_transform) {
    surface = {};
    surface.id = id;
    surface.enabled = 1;
    surface.transform_mode = use_local_transform ? 1U : 0U;
    surface.position_x = static_cast<float>(width * 0.5);
    surface.position_y = static_cast<float>(height * 0.5);
    surface.scale_x = 100.0F;
    surface.scale_y = 100.0F;
    surface.scale_z = 100.0F;
    surface.opacity = 100.0F;
    surface.diffuse = 100.0F;
    surface.shininess = 32.0F;
    surface.mesh_quality = 4;
    InitializeLattice(surface.lattice, 3, 3, width, height, id);
    surface.divisions_x = surface.lattice.divisions_x;
    surface.divisions_y = surface.lattice.divisions_y;
    UpdateDerivedTransform(surface);
}

void InitializeScene(SceneData& scene, double width, double height) {
    scene = {};
    scene.surface_count = 1;
    InitializeFlatSurface(scene.surfaces[0], 1, width, height, true);
}

bool IsValidScene(const SceneData& scene) {
    if (scene.surface_count < 1 ||
        scene.surface_count > kMaximumSurfaces) {
        return false;
    }
    for (std::uint32_t index = 0; index < scene.surface_count; ++index) {
        const SurfaceData& surface = scene.surfaces[index];
        if (!IsValidLattice(surface.lattice) ||
            !IsFiniteTransform(surface) ||
            surface.mesh_quality < kMinimumMeshQuality ||
            surface.mesh_quality > kMaximumMeshQuality ||
            surface.source_slot >= kMaximumSurfaces ||
            surface.back_source_slot > kMaximumSurfaces ||
            surface.image_size_mode < kImageSizeStretch ||
            surface.image_size_mode > kImageSizeFit ||
            surface.image_border_mode < kImageBorderClamp ||
            surface.image_border_mode > kImageBorderTransparent ||
            !std::isfinite(surface.opacity) ||
            !std::isfinite(surface.thickness) ||
            !std::isfinite(surface.diffuse) ||
            !std::isfinite(surface.specular) ||
            !std::isfinite(surface.metalness) ||
            !std::isfinite(surface.shininess) ||
            surface.opacity < 0.0F ||
            surface.opacity > 100.0F ||
            surface.thickness < 0.0F ||
            surface.thickness > 1000.0F ||
            surface.diffuse < 0.0F ||
            surface.diffuse > 100.0F ||
            surface.specular < 0.0F ||
            surface.specular > 100.0F ||
            surface.metalness < 0.0F ||
            surface.metalness > 100.0F ||
            surface.shininess < 1.0F) {
            return false;
        }
    }
    return true;
}

std::vector<std::int64_t> BuildSubframeSampleTimes(
    std::int64_t current_time,
    std::int64_t time_step,
    double shutter_angle,
    double shutter_phase,
    std::uint32_t requested_samples) {
    if (time_step <= 0 ||
        !std::isfinite(shutter_angle) ||
        !std::isfinite(shutter_phase) ||
        shutter_angle <= 1.0e-6 ||
        requested_samples < 2) {
        return {current_time};
    }
    const std::uint32_t sample_count =
        std::clamp<std::uint32_t>(requested_samples, 2, 32);
    const double clamped_angle =
        std::clamp(shutter_angle, 0.0, 1.0);
    std::vector<std::int64_t> times;
    times.reserve(sample_count);
    for (std::uint32_t index = 0; index < sample_count; ++index) {
        const double fraction =
            shutter_phase +
            clamped_angle *
                (static_cast<double>(index) + 0.5) /
                static_cast<double>(sample_count);
        const std::int64_t sample_time =
            current_time +
            static_cast<std::int64_t>(std::llround(
                fraction * static_cast<double>(time_step)));
        if (times.empty() || times.back() != sample_time) {
            times.push_back(sample_time);
        }
    }
    if (times.empty()) {
        times.push_back(current_time);
    }
    return times;
}
