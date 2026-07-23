#include "SurfaceLabModel.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

// Host-independent scene initialization, validation, and V1..V13 migration.
// Extracted verbatim from SurfaceLab.cpp; no behavioral change. Depends only on
// SurfaceLabModel.h, so this translation unit compiles and is tested off-host.

void UpdateDerivedTransform(SurfaceData& surface) {
    float minimum_x = surface.control_points[0].x;
    float maximum_x = minimum_x;
    float minimum_y = surface.control_points[0].y;
    float maximum_y = minimum_y;
    double total_z = 0.0;
    for (const StoredPoint3& point : surface.control_points) {
        minimum_x = std::min(minimum_x, point.x);
        maximum_x = std::max(maximum_x, point.x);
        minimum_y = std::min(minimum_y, point.y);
        maximum_y = std::max(maximum_y, point.y);
        total_z += point.z;
    }
    surface.size_x = std::max(0.001F, maximum_x - minimum_x);
    surface.size_y = std::max(0.001F, maximum_y - minimum_y);
    surface.position_x = (minimum_x + maximum_x) * 0.5F;
    surface.position_y = (minimum_y + maximum_y) * 0.5F;
    surface.position_z = static_cast<float>(total_z / 16.0);
}

void InitializeEdgeTwists(SurfaceData& surface) {
    for (EdgeTwistData& edge : surface.edge_twists) {
        edge.angle = 0.0F;
        edge.falloff = 100.0F;
    }
    surface.selected_twist_edge = kTwistEdgeLeft;
}

void InitializeCornerCurls(SurfaceData& surface) {
    for (CornerCurlData& corner : surface.corner_curls) {
        corner.amount = 0.0F;
        corner.radius = 15.0F;
        corner.direction = 45.0F;
        corner.length = 30.0F;
    }
    surface.selected_corner = kCornerTopLeft;
    InitializeEdgeTwists(surface);
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
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            const int index = row * 4 + column;
            surface.control_points[index].x = static_cast<float>(
                width * static_cast<double>(column) / 3.0);
            surface.control_points[index].y = static_cast<float>(
                height * static_cast<double>(row) / 3.0);
        }
    }
    surface.scale_x = 100.0F;
    surface.scale_y = 100.0F;
    surface.scale_z = 100.0F;
    surface.divisions_x = use_local_transform ? kDefaultDivisions : 0U;
    surface.divisions_y = use_local_transform ? kDefaultDivisions : 0U;
    surface.transform_mode = use_local_transform ? 1U : 0U;
    surface.source_slot = 0;
    surface.back_source_slot = 0;
    surface.animation_bank = 0;
    surface.image_size_mode = kImageSizeStretch;
    surface.image_border_mode = kImageBorderClamp;
    surface.opacity = 100.0F;
    surface.thickness = 0.0F;
    surface.diffuse = 100.0F;
    surface.specular = 50.0F;
    surface.shininess = 32.0F;
    surface.bend_x = 0.0F;
    surface.bend_y = 0.0F;
    surface.roll_angle = 0.0F;
    surface.roll_length = 25.0F;
    surface.roll_edge = kRollEdgeRight;
    InitializeCornerCurls(surface);
    UpdateDerivedTransform(surface);
}

void InitializeScene(SceneData& scene, double width, double height) {
    scene = SceneData{};
    scene.magic = kSceneMagic;
    scene.schema_version = kSceneSchemaVersion;
    scene.reserved[kAnimationStreamsInitializedIndex] = 1;
    scene.reserved[kAnimationBanksInitializedIndex] = 1;
    scene.surface_count = 1;
    scene.next_surface_id = 2;
    InitializeFlatSurface(scene.surfaces[0], 1, width, height, false);
}

bool IsValidScene(const SceneData& scene) {
    if (scene.magic != kSceneMagic ||
        scene.schema_version != kSceneSchemaVersion ||
        scene.surface_count < 1 ||
        scene.surface_count > kMaximumSurfaces ||
        scene.selected_surface >= scene.surface_count) {
        return false;
    }
    std::array<bool, kMaximumSurfaces> used_animation_banks{};
    for (std::uint32_t index = 0; index < scene.surface_count; ++index) {
        const SurfaceData& surface = scene.surfaces[index];
        for (const StoredPoint3& point : surface.control_points) {
            if (!std::isfinite(point.x) ||
                !std::isfinite(point.y) ||
                !std::isfinite(point.z)) {
                return false;
            }
        }
        if (!std::isfinite(surface.rotation_x) ||
            !std::isfinite(surface.rotation_y) ||
            !std::isfinite(surface.rotation_z) ||
            !std::isfinite(surface.size_x) ||
            !std::isfinite(surface.size_y) ||
            !std::isfinite(surface.position_x) ||
            !std::isfinite(surface.position_y) ||
            !std::isfinite(surface.position_z) ||
            !std::isfinite(surface.scale_x) ||
            !std::isfinite(surface.scale_y) ||
            !std::isfinite(surface.scale_z)) {
            return false;
        }
        if (surface.animation_bank >= kMaximumSurfaces ||
            used_animation_banks[surface.animation_bank]) {
            return false;
        }
        used_animation_banks[surface.animation_bank] = true;
        const bool valid_x = surface.divisions_x == 0 ||
                             (surface.divisions_x >= kMinimumDivisions &&
                              surface.divisions_x <= kMaximumDivisions);
        const bool valid_y = surface.divisions_y == 0 ||
                             (surface.divisions_y >= kMinimumDivisions &&
                              surface.divisions_y <= kMaximumDivisions);
        if (!valid_x || !valid_y) {
            return false;
        }
        if (surface.source_slot >= kMaximumSurfaces ||
            surface.back_source_slot > kMaximumSurfaces ||
            surface.image_size_mode < kImageSizeStretch ||
            surface.image_size_mode > kImageSizeFit ||
            surface.image_border_mode < kImageBorderClamp ||
            surface.image_border_mode > kImageBorderTransparent ||
            !std::isfinite(surface.opacity) ||
            surface.opacity < 0.0F ||
            surface.opacity > 100.0F) {
            return false;
        }
        if (!std::isfinite(surface.thickness) ||
            surface.thickness < 0.0F ||
            surface.thickness > 4000.0F) {
            return false;
        }
        if (!std::isfinite(surface.diffuse) ||
            !std::isfinite(surface.specular) ||
            !std::isfinite(surface.shininess) ||
            surface.diffuse < 0.0F || surface.diffuse > 200.0F ||
            surface.specular < 0.0F || surface.specular > 200.0F ||
            surface.shininess < 1.0F || surface.shininess > 256.0F) {
            return false;
        }
        if (!std::isfinite(surface.bend_x) ||
            !std::isfinite(surface.bend_y) ||
            !std::isfinite(surface.roll_angle) ||
            !std::isfinite(surface.roll_length) ||
            surface.bend_x < -720.0F || surface.bend_x > 720.0F ||
            surface.bend_y < -720.0F || surface.bend_y > 720.0F ||
            surface.roll_angle < -1080.0F || surface.roll_angle > 1080.0F ||
            surface.roll_length < 0.0F || surface.roll_length > 100.0F ||
            surface.roll_edge < kRollEdgeRight ||
            surface.roll_edge > kRollEdgeTop) {
            return false;
        }
        if (surface.selected_corner < kCornerTopLeft ||
            surface.selected_corner > kCornerBottomLeft) {
            return false;
        }
        for (const CornerCurlData& corner : surface.corner_curls) {
            if (!std::isfinite(corner.amount) ||
                !std::isfinite(corner.radius) ||
                !std::isfinite(corner.direction) ||
                !std::isfinite(corner.length) ||
                corner.amount < -100.0F || corner.amount > 100.0F ||
                corner.radius < 0.1F || corner.radius > 100.0F ||
                corner.direction < 0.0F || corner.direction > 90.0F ||
                corner.length < 0.0F || corner.length > 150.0F) {
                return false;
            }
        }
        if (surface.selected_twist_edge < kTwistEdgeLeft ||
            surface.selected_twist_edge > kTwistEdgeBottom) {
            return false;
        }
        for (const EdgeTwistData& edge : surface.edge_twists) {
            if (!std::isfinite(edge.angle) ||
                !std::isfinite(edge.falloff) ||
                edge.angle < -180.0F || edge.angle > 180.0F ||
                edge.falloff < 1.0F || edge.falloff > 100.0F) {
                return false;
            }
        }
        if (surface.rotation_origin_mode < kRotationOriginCenter ||
            surface.rotation_origin_mode > kRotationOriginCustom ||
            !std::isfinite(surface.rotation_origin_x) ||
            !std::isfinite(surface.rotation_origin_y) ||
            surface.rotation_origin_x < -1000.0F ||
            surface.rotation_origin_x > 1000.0F ||
            surface.rotation_origin_y < -1000.0F ||
            surface.rotation_origin_y > 1000.0F) {
            return false;
        }
    }
    return true;
}

bool IsValidSceneV1(const SceneDataV1& scene) {
    return scene.magic == kSceneMagic &&
           scene.schema_version == 1 &&
           scene.surface_count >= 1 &&
           scene.surface_count <= kMaximumSurfaces &&
           scene.selected_surface < scene.surface_count;
}

bool IsValidSceneV2(const SceneDataV2& scene) {
    return scene.magic == kSceneMagic &&
           scene.schema_version == 2 &&
           scene.surface_count >= 1 &&
           scene.surface_count <= kMaximumSurfaces &&
           scene.selected_surface < scene.surface_count;
}

bool IsValidSceneV3(const SceneDataV3& scene) {
    return scene.magic == kSceneMagic &&
           scene.schema_version == 3 &&
           scene.surface_count >= 1 &&
           scene.surface_count <= kMaximumSurfaces &&
           scene.selected_surface < scene.surface_count;
}

bool IsValidSceneV4(const SceneDataV4& scene) {
    return scene.magic == kSceneMagic &&
           scene.schema_version == 4 &&
           scene.surface_count >= 1 &&
           scene.surface_count <= kMaximumSurfaces &&
           scene.selected_surface < scene.surface_count;
}

bool IsValidSceneV5(const SceneDataV5& scene) {
    return scene.magic == kSceneMagic &&
           scene.schema_version == 5 &&
           scene.surface_count >= 1 &&
           scene.surface_count <= kMaximumSurfaces &&
           scene.selected_surface < scene.surface_count;
}

bool IsValidSceneV6(const SceneDataV6& scene) {
    return scene.magic == kSceneMagic &&
           scene.schema_version == 6 &&
           scene.surface_count >= 1 &&
           scene.surface_count <= kMaximumSurfaces &&
           scene.selected_surface < scene.surface_count;
}

bool IsValidSceneV7(const SceneDataV7& scene) {
    return scene.magic == kSceneMagic &&
           scene.schema_version == 7 &&
           scene.surface_count >= 1 &&
           scene.surface_count <= kMaximumSurfaces &&
           scene.selected_surface < scene.surface_count;
}

bool IsValidSceneV8(const SceneDataV8& scene) {
    return scene.magic == kSceneMagic &&
           scene.schema_version == 8 &&
           scene.surface_count >= 1 &&
           scene.surface_count <= kMaximumSurfaces &&
           scene.selected_surface < scene.surface_count;
}

bool IsValidSceneV9(const SceneDataV9& scene) {
    return scene.magic == kSceneMagic &&
           scene.schema_version == 9 &&
           scene.surface_count >= 1 &&
           scene.surface_count <= kMaximumSurfaces &&
           scene.selected_surface < scene.surface_count;
}

void InitializeMaterial(SurfaceData& surface) {
    surface.source_slot = 0;
    surface.image_size_mode = kImageSizeStretch;
    surface.image_border_mode = kImageBorderClamp;
    surface.opacity = 100.0F;
    surface.diffuse = 100.0F;
    surface.specular = 50.0F;
    surface.shininess = 32.0F;
}

namespace {

// Each schema version appended fields to the previous surface layout, so
// MigrateSceneVN copies every group its version already had and defaults the
// rest. The helpers below name those cumulative groups; they are templated
// because each version has its own struct type with identically named fields.

template <typename SceneV>
void CopySceneHeader(const SceneV& source, SceneData& destination) {
    destination = SceneData{};
    destination.magic = kSceneMagic;
    destination.schema_version = kSceneSchemaVersion;
    destination.active = source.active;
    destination.surface_count = source.surface_count;
    destination.selected_surface = source.selected_surface;
    destination.next_surface_id = source.next_surface_id;
}

// V1+: identity, control points, rotation.
template <typename SurfaceV>
void CopySurfaceIdentity(const SurfaceV& old_surface, SurfaceData& new_surface) {
    new_surface.id = old_surface.id;
    new_surface.enabled = old_surface.enabled;
    std::copy(
        std::begin(old_surface.control_points),
        std::end(old_surface.control_points),
        std::begin(new_surface.control_points));
    new_surface.rotation_x = old_surface.rotation_x;
    new_surface.rotation_y = old_surface.rotation_y;
    new_surface.rotation_z = old_surface.rotation_z;
}

// V2+: stored size/position and local transform.
template <typename SurfaceV>
void CopySurfaceTransform(const SurfaceV& old_surface, SurfaceData& new_surface) {
    new_surface.size_x = old_surface.size_x;
    new_surface.size_y = old_surface.size_y;
    new_surface.position_x = old_surface.position_x;
    new_surface.position_y = old_surface.position_y;
    new_surface.position_z = old_surface.position_z;
    new_surface.scale_x = old_surface.scale_x;
    new_surface.scale_y = old_surface.scale_y;
    new_surface.scale_z = old_surface.scale_z;
    new_surface.transform_mode = old_surface.transform_mode;
}

// V3+: per-surface tessellation.
template <typename SurfaceV>
void CopySurfaceDivisions(const SurfaceV& old_surface, SurfaceData& new_surface) {
    new_surface.divisions_x = old_surface.divisions_x;
    new_surface.divisions_y = old_surface.divisions_y;
}

// V4+: texture source and opacity.
template <typename SurfaceV>
void CopySurfaceImage(const SurfaceV& old_surface, SurfaceData& new_surface) {
    new_surface.source_slot = old_surface.source_slot;
    new_surface.image_size_mode = old_surface.image_size_mode;
    new_surface.image_border_mode = old_surface.image_border_mode;
    new_surface.opacity = old_surface.opacity;
}

// V6+: lighting material.
template <typename SurfaceV>
void CopySurfaceLighting(const SurfaceV& old_surface, SurfaceData& new_surface) {
    new_surface.diffuse = old_surface.diffuse;
    new_surface.specular = old_surface.specular;
    new_surface.shininess = old_surface.shininess;
}

// V7+: bend and roll deformation.
template <typename SurfaceV>
void CopySurfaceDeform(const SurfaceV& old_surface, SurfaceData& new_surface) {
    new_surface.bend_x = old_surface.bend_x;
    new_surface.bend_y = old_surface.bend_y;
    new_surface.roll_angle = old_surface.roll_angle;
    new_surface.roll_length = old_surface.roll_length;
    new_surface.roll_edge = old_surface.roll_edge;
}

void InitializeLightingDefaults(SurfaceData& surface) {
    surface.diffuse = 100.0F;
    surface.specular = 50.0F;
    surface.shininess = 32.0F;
}

void InitializeDeformDefaults(SurfaceData& surface) {
    surface.bend_x = 0.0F;
    surface.bend_y = 0.0F;
    surface.roll_angle = 0.0F;
    surface.roll_length = 25.0F;
    surface.roll_edge = kRollEdgeRight;
}

}  // namespace

void MigrateSceneV1(const SceneDataV1& source, SceneData& destination) {
    CopySceneHeader(source, destination);
    for (std::uint32_t index = 0; index < source.surface_count; ++index) {
        const SurfaceDataV1& old_surface = source.surfaces[index];
        SurfaceData& new_surface = destination.surfaces[index];
        CopySurfaceIdentity(old_surface, new_surface);
        new_surface.scale_x = 100.0F;
        new_surface.scale_y = 100.0F;
        new_surface.scale_z = 100.0F;
        new_surface.divisions_x = 0;
        new_surface.divisions_y = 0;
        new_surface.transform_mode = 0;
        InitializeMaterial(new_surface);
        UpdateDerivedTransform(new_surface);
        InitializeCornerCurls(new_surface);
    }
}

void MigrateSceneV2(const SceneDataV2& source, SceneData& destination) {
    CopySceneHeader(source, destination);
    for (std::uint32_t index = 0; index < source.surface_count; ++index) {
        const SurfaceDataV2& old_surface = source.surfaces[index];
        SurfaceData& new_surface = destination.surfaces[index];
        CopySurfaceIdentity(old_surface, new_surface);
        CopySurfaceTransform(old_surface, new_surface);
        new_surface.divisions_x = 0;
        new_surface.divisions_y = 0;
        InitializeMaterial(new_surface);
        InitializeCornerCurls(new_surface);
    }
}

void MigrateSceneV3(const SceneDataV3& source, SceneData& destination) {
    CopySceneHeader(source, destination);
    for (std::uint32_t index = 0; index < source.surface_count; ++index) {
        const SurfaceDataV3& old_surface = source.surfaces[index];
        SurfaceData& new_surface = destination.surfaces[index];
        CopySurfaceIdentity(old_surface, new_surface);
        CopySurfaceTransform(old_surface, new_surface);
        CopySurfaceDivisions(old_surface, new_surface);
        InitializeMaterial(new_surface);
        InitializeCornerCurls(new_surface);
    }
}

void MigrateSceneV4(const SceneDataV4& source, SceneData& destination) {
    CopySceneHeader(source, destination);
    for (std::uint32_t index = 0; index < source.surface_count; ++index) {
        const SurfaceDataV4& old_surface = source.surfaces[index];
        SurfaceData& new_surface = destination.surfaces[index];
        CopySurfaceIdentity(old_surface, new_surface);
        CopySurfaceTransform(old_surface, new_surface);
        CopySurfaceDivisions(old_surface, new_surface);
        CopySurfaceImage(old_surface, new_surface);
        new_surface.thickness = 0.0F;
        InitializeLightingDefaults(new_surface);
        InitializeCornerCurls(new_surface);
    }
}

void MigrateSceneV5(const SceneDataV5& source, SceneData& destination) {
    CopySceneHeader(source, destination);
    for (std::uint32_t index = 0; index < source.surface_count; ++index) {
        const SurfaceDataV5& old_surface = source.surfaces[index];
        SurfaceData& new_surface = destination.surfaces[index];
        CopySurfaceIdentity(old_surface, new_surface);
        CopySurfaceTransform(old_surface, new_surface);
        CopySurfaceDivisions(old_surface, new_surface);
        CopySurfaceImage(old_surface, new_surface);
        new_surface.thickness = old_surface.thickness;
        InitializeLightingDefaults(new_surface);
        InitializeCornerCurls(new_surface);
    }
}

void MigrateSceneV6(const SceneDataV6& source, SceneData& destination) {
    CopySceneHeader(source, destination);
    for (std::uint32_t index = 0; index < source.surface_count; ++index) {
        const SurfaceDataV6& old_surface = source.surfaces[index];
        SurfaceData& new_surface = destination.surfaces[index];
        CopySurfaceIdentity(old_surface, new_surface);
        CopySurfaceTransform(old_surface, new_surface);
        CopySurfaceDivisions(old_surface, new_surface);
        CopySurfaceImage(old_surface, new_surface);
        new_surface.thickness = old_surface.thickness;
        CopySurfaceLighting(old_surface, new_surface);
        InitializeDeformDefaults(new_surface);
        InitializeCornerCurls(new_surface);
    }
}

void MigrateSceneV7(const SceneDataV7& source, SceneData& destination) {
    CopySceneHeader(source, destination);
    for (std::uint32_t index = 0; index < source.surface_count; ++index) {
        const SurfaceDataV7& old_surface = source.surfaces[index];
        SurfaceData& new_surface = destination.surfaces[index];
        CopySurfaceIdentity(old_surface, new_surface);
        CopySurfaceTransform(old_surface, new_surface);
        CopySurfaceDivisions(old_surface, new_surface);
        CopySurfaceImage(old_surface, new_surface);
        new_surface.thickness = old_surface.thickness;
        CopySurfaceLighting(old_surface, new_surface);
        CopySurfaceDeform(old_surface, new_surface);
        InitializeCornerCurls(new_surface);
    }
}

void MigrateSceneV8(const SceneDataV8& source, SceneData& destination) {
    CopySceneHeader(source, destination);
    for (std::uint32_t index = 0; index < source.surface_count; ++index) {
        const SurfaceDataV8& old_surface = source.surfaces[index];
        SurfaceData& new_surface = destination.surfaces[index];
        CopySurfaceIdentity(old_surface, new_surface);
        CopySurfaceTransform(old_surface, new_surface);
        CopySurfaceDivisions(old_surface, new_surface);
        CopySurfaceImage(old_surface, new_surface);
        new_surface.thickness = old_surface.thickness;
        CopySurfaceLighting(old_surface, new_surface);
        CopySurfaceDeform(old_surface, new_surface);
        std::copy(
            std::begin(old_surface.corner_curls),
            std::end(old_surface.corner_curls),
            std::begin(new_surface.corner_curls));
        new_surface.selected_corner = old_surface.selected_corner;
        InitializeEdgeTwists(new_surface);
    }
}

void MigrateSceneV9(const SceneDataV9& source, SceneData& destination) {
    CopySceneHeader(source, destination);
    for (std::uint32_t index = 0; index < source.surface_count; ++index) {
        SurfaceData& new_surface = destination.surfaces[index];
        std::memcpy(
            &new_surface,
            &source.surfaces[index],
            sizeof(SurfaceDataV9));
        new_surface.rotation_origin_mode = kRotationOriginCenter;
        new_surface.rotation_origin_x = 50.0F;
        new_surface.rotation_origin_y = 50.0F;
    }
}

void MigrateSceneV11(const SceneDataV11& source, SceneData& destination) {
    destination = SceneData{};
    destination.magic = source.magic;
    destination.schema_version = kSceneSchemaVersion;
    destination.active = source.active;
    destination.surface_count = source.surface_count;
    destination.selected_surface = source.selected_surface;
    destination.next_surface_id = source.next_surface_id;
    std::copy(
        std::begin(source.reserved),
        std::end(source.reserved),
        std::begin(destination.reserved));
    for (std::uint32_t index = 0; index < source.surface_count; ++index) {
        std::memcpy(
            &destination.surfaces[index],
            &source.surfaces[index],
            sizeof(SurfaceDataV11));
        destination.surfaces[index].back_source_slot = 0;
    }
}

void MigrateSceneV10(const SceneDataV11& source, SceneData& destination) {
    MigrateSceneV11(source, destination);
    destination.schema_version = kSceneSchemaVersion;
    destination.reserved[kAnimationStreamsInitializedIndex] = 0;
}

void MigrateSceneV12(const SceneDataV12& source, SceneData& destination) {
    destination = SceneData{};
    destination.magic = source.magic;
    destination.schema_version = kSceneSchemaVersion;
    destination.active = source.active;
    destination.surface_count = source.surface_count;
    destination.selected_surface = source.selected_surface;
    destination.next_surface_id = source.next_surface_id;
    std::copy(
        std::begin(source.reserved),
        std::end(source.reserved),
        std::begin(destination.reserved));
    for (std::uint32_t index = 0; index < source.surface_count; ++index) {
        std::memcpy(
            &destination.surfaces[index],
            &source.surfaces[index],
            sizeof(SurfaceDataV12));
    }
}

void AssignLegacyAnimationBanks(SceneData& scene) {
    std::uint32_t next_bank = 1;
    for (std::uint32_t index = 0; index < scene.surface_count; ++index) {
        if (index == scene.selected_surface) {
            scene.surfaces[index].animation_bank = 0;
        } else {
            scene.surfaces[index].animation_bank = next_bank++;
        }
    }
    scene.reserved[kAnimationBanksInitializedIndex] = 0;
}
