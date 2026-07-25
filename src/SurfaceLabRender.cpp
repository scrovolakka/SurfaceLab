#include "SurfaceLab.h"
#include "AEGP_SuiteHandler.h"
#include "AEFX_SuiteHelper.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "SurfaceLabRender.h"

namespace {

bool IsFinitePoint3(const Point3& point) {
    return std::isfinite(point.x) &&
           std::isfinite(point.y) &&
           std::isfinite(point.z);
}

}  // namespace

Vertex ProjectVertex(
    const Point3& point,
    Point3 normal,
    double u,
    double v,
    const CameraState& camera) {
    const Point3 scene_point =
        ApplyScenePointTransform(point, camera.scene_transform);
    normal = ApplySceneNormalTransform(normal, camera.scene_transform);
    Point3 camera_point{
        scene_point.x - camera.position.x,
        scene_point.y - camera.position.y,
        scene_point.z - camera.position.z};
    if (camera.use_basis) {
        const Point3 delta = camera_point;
        camera_point = {
            Dot(delta, camera.right),
            Dot(delta, camera.down),
            Dot(delta, camera.forward)};
    } else {
        camera_point = InverseRotateVector(
            camera_point,
            camera.rotation_x,
            camera.rotation_y,
            camera.rotation_z);
    }
    const double camera_depth = camera_point.z;
    Vertex vertex;
    vertex.u = u;
    vertex.v = v;
    vertex.world_position = scene_point;
    vertex.normal = Normalize(normal);
    vertex.visible = IsFinitePoint3(camera_point) && camera_depth > 1.0;
    if (vertex.visible) {
        vertex.inverse_depth = 1.0 / camera_depth;
        Point2 projected;
        if (camera.perspective) {
            const double projection_scale = camera.focal_distance * vertex.inverse_depth;
            projected = {
                camera.center_x + camera_point.x * projection_scale,
                camera.center_y + camera_point.y * projection_scale};
        } else {
            projected = {
                camera.center_x + camera_point.x,
                camera.center_y + camera_point.y};
        }
        if (camera.use_comp_to_output) {
            projected = ApplyAffine2D(camera.comp_to_output, projected);
        }
        vertex.x = projected.x + camera.output_offset_x;
        vertex.y = projected.y + camera.output_offset_y;
        vertex.visible = std::isfinite(vertex.x) &&
                         std::isfinite(vertex.y) &&
                         std::isfinite(vertex.inverse_depth) &&
                         std::isfinite(vertex.u) &&
                         std::isfinite(vertex.v) &&
                         IsFinitePoint3(vertex.world_position) &&
                         IsFinitePoint3(vertex.normal);
    }
    return vertex;
}

namespace {
double Edge(const Vertex& a, const Vertex& b, double x, double y) {
    return (x - a.x) * (b.y - a.y) - (y - a.y) * (b.x - a.x);
}

bool IsFiniteRasterVertex(const Vertex& vertex) {
    return std::isfinite(vertex.x) &&
           std::isfinite(vertex.y) &&
           std::isfinite(vertex.u) &&
           std::isfinite(vertex.v) &&
           std::isfinite(vertex.inverse_depth) &&
           IsFinitePoint3(vertex.world_position) &&
           IsFinitePoint3(vertex.normal);
}

Point2 MapImageCoordinates(
    const SurfaceData& surface,
    const PF_LayerDef& input,
    double u,
    double v) {
    double pixel_aspect = 1.0;
    if (input.pix_aspect_ratio.den != 0) {
        pixel_aspect = static_cast<double>(input.pix_aspect_ratio.num) /
                       static_cast<double>(input.pix_aspect_ratio.den);
    }
    const double image_aspect = std::max(
        1.0e-6,
        static_cast<double>(input.width) * pixel_aspect /
            std::max(1.0, static_cast<double>(input.height)));
    const double scaled_width = std::abs(
        static_cast<double>(surface.size_x) *
        static_cast<double>(surface.scale_x) / 100.0);
    const double scaled_height = std::abs(
        static_cast<double>(surface.size_y) *
        static_cast<double>(surface.scale_y) / 100.0);
    const double surface_aspect = std::max(1.0e-6, scaled_width) /
                                  std::max(1.0e-6, scaled_height);

    if (surface.image_size_mode == kImageSizeFill) {
        if (image_aspect > surface_aspect) {
            u = 0.5 + (u - 0.5) * (surface_aspect / image_aspect);
        } else {
            v = 0.5 + (v - 0.5) * (image_aspect / surface_aspect);
        }
    } else if (surface.image_size_mode == kImageSizeFit) {
        if (image_aspect > surface_aspect) {
            const double content_height = surface_aspect / image_aspect;
            v = 0.5 + (v - 0.5) / std::max(1.0e-6, content_height);
        } else {
            const double content_width = image_aspect / surface_aspect;
            u = 0.5 + (u - 0.5) / std::max(1.0e-6, content_width);
        }
    }
    return {u, v};
}

bool ResolveBorderCoordinate(double& coordinate, std::uint32_t border_mode) {
    // A non-finite coordinate (degenerate surface math upstream) must never
    // reach the pixel-index arithmetic: clamp/repeat/mirror would all keep it
    // NaN and the float-to-int casts below SampleTexture are undefined for it.
    // Treat it like an out-of-range transparent sample instead.
    if (!std::isfinite(coordinate)) {
        return false;
    }
    if (coordinate >= 0.0 && coordinate <= 1.0) {
        return true;
    }
    if (border_mode == kImageBorderTransparent) {
        return false;
    }
    if (border_mode == kImageBorderClamp) {
        coordinate = std::clamp(coordinate, 0.0, 1.0);
    } else if (border_mode == kImageBorderRepeat) {
        coordinate -= std::floor(coordinate);
    } else if (border_mode == kImageBorderMirror) {
        double mirrored = std::fmod(coordinate, 2.0);
        if (mirrored < 0.0) {
            mirrored += 2.0;
        }
        coordinate = mirrored <= 1.0 ? mirrored : 2.0 - mirrored;
    } else {
        return false;
    }
    return true;
}

template <typename Pixel>
constexpr double PixelChannelMaximum() {
    if constexpr (std::is_same_v<Pixel, PF_PixelFloat>) {
        return static_cast<double>(
            std::numeric_limits<PF_FpShort>::max());
    } else if constexpr (std::is_same_v<Pixel, PF_Pixel16>) {
        return static_cast<double>(PF_MAX_CHAN16);
    } else {
        return static_cast<double>(PF_MAX_CHAN8);
    }
}

template <typename Pixel>
constexpr double PixelChannelWhite() {
    if constexpr (std::is_same_v<Pixel, PF_PixelFloat>) {
        return 1.0;
    }
    return PixelChannelMaximum<Pixel>();
}

template <typename Pixel>
double QuantizePixelChannel(double value) {
    if constexpr (std::is_same_v<Pixel, PF_PixelFloat>) {
        return value;
    } else {
        return std::lround(value);
    }
}

template <typename Pixel>
Pixel MakeOpaqueViewPixel(double red, double green, double blue) {
    const double white = PixelChannelWhite<Pixel>();
    const auto channel = [&](double value) {
        return QuantizePixelChannel<Pixel>(
            std::clamp(value, 0.0, 1.0) * white);
    };
    Pixel pixel{};
    pixel.alpha = static_cast<decltype(pixel.alpha)>(white);
    pixel.red = static_cast<decltype(pixel.red)>(channel(red));
    pixel.green = static_cast<decltype(pixel.green)>(channel(green));
    pixel.blue = static_cast<decltype(pixel.blue)>(channel(blue));
    return pixel;
}

Point3 DirectionToCameraSpace(
    const Point3& direction,
    const CameraState& camera) {
    if (camera.use_basis) {
        return Normalize({
            Dot(direction, camera.right),
            Dot(direction, camera.down),
            Dot(direction, camera.forward)});
    }
    return Normalize(InverseRotateVector(
        direction,
        camera.rotation_x,
        camera.rotation_y,
        camera.rotation_z));
}

struct ShadowBounds {
    Point3 minimum{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity()};
    Point3 maximum{
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity()};

    void Expand(const Point3& point) {
        minimum.x = std::min(minimum.x, point.x);
        minimum.y = std::min(minimum.y, point.y);
        minimum.z = std::min(minimum.z, point.z);
        maximum.x = std::max(maximum.x, point.x);
        maximum.y = std::max(maximum.y, point.y);
        maximum.z = std::max(maximum.z, point.z);
    }

    void Expand(const ShadowBounds& bounds) {
        Expand(bounds.minimum);
        Expand(bounds.maximum);
    }
};

struct ShadowTriangle {
    Point3 a{};
    Point3 b{};
    Point3 c{};
    Point3 centroid{};
    ShadowBounds bounds{};
};

struct ShadowBvhNode {
    ShadowBounds bounds{};
    std::uint32_t first{};
    std::uint32_t count{};
    std::uint32_t left{};
    std::uint32_t right{};
};

struct ShadowScene {
    std::vector<ShadowTriangle> triangles;
    std::vector<ShadowBvhNode> nodes;

    bool Occluded(
        const Point3& origin,
        const Point3& direction,
        double maximum_distance) const {
        if (nodes.empty() ||
            !IsFinitePoint3(origin) ||
            !IsFinitePoint3(direction) ||
            (!std::isfinite(maximum_distance) &&
             maximum_distance !=
                 std::numeric_limits<double>::infinity())) {
            return false;
        }
        constexpr double kMinimumDistance = 1.0e-5;
        std::array<std::uint32_t, 128> pending{};
        std::size_t pending_count = 1;
        pending[0] = 0;
        while (pending_count != 0) {
            const std::uint32_t node_index =
                pending[--pending_count];
            if (node_index >= nodes.size()) {
                continue;
            }
            const ShadowBvhNode& node = nodes[node_index];
            if (!RayIntersectsBounds(
                    origin,
                    direction,
                    node.bounds.minimum,
                    node.bounds.maximum,
                    kMinimumDistance,
                    maximum_distance)) {
                continue;
            }
            if (node.count != 0) {
                const std::uint32_t end =
                    std::min<std::uint32_t>(
                        node.first + node.count,
                        static_cast<std::uint32_t>(triangles.size()));
                for (std::uint32_t index = node.first;
                     index < end;
                     ++index) {
                    const ShadowTriangle& triangle = triangles[index];
                    if (RayIntersectsTriangle(
                            origin,
                            direction,
                            triangle.a,
                            triangle.b,
                            triangle.c,
                            kMinimumDistance,
                            maximum_distance)) {
                        return true;
                    }
                }
            } else {
                if (pending_count + 2 <= pending.size()) {
                    pending[pending_count++] = node.left;
                    pending[pending_count++] = node.right;
                }
            }
        }
        return false;
    }
};

bool ShadowRayOccluded(
    const Point3& normal,
    const Point3& world_position,
    const Point3& light_direction,
    double light_distance,
    Point3 origin_offset,
    const ShadowScene& shadow_scene) {
    const double normal_alignment =
        std::clamp(Dot(normal, light_direction), 0.0, 1.0);
    const double bias = 0.35 + (1.0 - normal_alignment) * 0.65;
    const Point3 origin{
        world_position.x + normal.x * bias +
            light_direction.x * 1.0e-4 + origin_offset.x,
        world_position.y + normal.y * bias +
            light_direction.y * 1.0e-4 + origin_offset.y,
        world_position.z + normal.z * bias +
            light_direction.z * 1.0e-4 + origin_offset.z};
    const double maximum_distance =
        std::isfinite(light_distance)
            ? std::max(1.0e-5, light_distance - bias)
            : std::numeric_limits<double>::infinity();
    return shadow_scene.Occluded(
        origin,
        light_direction,
        maximum_distance);
}

double ShadowVisibility(
    const RenderLight& light,
    const Point3& normal,
    const Point3& world_position,
    const Point3& light_direction,
    double light_distance,
    const ShadowScene* shadow_scene) {
    if (!light.casts_shadows ||
        !shadow_scene ||
        shadow_scene->nodes.empty()) {
        return 1.0;
    }

    const double diffusion = std::clamp(
        light.shadow_diffusion,
        0.0,
        1000.0);
    constexpr std::size_t kSoftShadowSampleCount = 25U;
    constexpr double kGoldenAngle = 2.39996322972865332;
    const std::size_t sample_count =
        diffusion > 1.0e-3 ? kSoftShadowSampleCount : 1U;
    const Point3 reference =
        std::abs(light_direction.z) < 0.95
            ? Point3{0.0, 0.0, 1.0}
            : Point3{0.0, 1.0, 0.0};
    const Point3 tangent = Normalize(Cross(reference, light_direction));
    const Point3 bitangent = Normalize(Cross(light_direction, tangent));
    std::size_t occluded_count{};

    for (std::size_t sample = 0; sample < sample_count; ++sample) {
        Point2 disk{};
        if (sample > 0U) {
            const double radius = std::sqrt(
                (static_cast<double>(sample) - 0.5) /
                static_cast<double>(kSoftShadowSampleCount - 1U));
            const double angle =
                static_cast<double>(sample) * kGoldenAngle;
            disk = {
                std::cos(angle) * radius,
                std::sin(angle) * radius};
        }
        const Point3 disk_offset{
            (tangent.x * disk.x + bitangent.x * disk.y) * diffusion,
            (tangent.y * disk.x + bitangent.y * disk.y) * diffusion,
            (tangent.z * disk.x + bitangent.z * disk.y) * diffusion};

        Point3 sampled_direction = light_direction;
        double sampled_distance = light_distance;
        Point3 origin_offset{};
        if (light.type == RenderLightType::Directional) {
            // A directional light has no finite emitter position. Sampling a
            // receiver-space disk gives AE's diffusion control a stable,
            // distance-independent blur radius.
            origin_offset = disk_offset;
        } else {
            const Point3 sampled_to_light{
                light.position.x + disk_offset.x - world_position.x,
                light.position.y + disk_offset.y - world_position.y,
                light.position.z + disk_offset.z - world_position.z};
            sampled_distance = std::sqrt(
                Dot(sampled_to_light, sampled_to_light));
            sampled_direction = Normalize(sampled_to_light);
        }
        if (ShadowRayOccluded(
                normal,
                world_position,
                sampled_direction,
                sampled_distance,
                origin_offset,
                *shadow_scene)) {
            ++occluded_count;
        }
    }

    const double occlusion =
        static_cast<double>(occluded_count) /
        static_cast<double>(sample_count);
    return 1.0 -
           std::clamp(light.shadow_darkness, 0.0, 1.0) * occlusion;
}

template <typename Pixel>
Pixel ApplyOpacity(Pixel pixel, float opacity_percent) {
    const double multiplier = std::clamp(
        static_cast<double>(opacity_percent) / 100.0,
        0.0,
        1.0);
    pixel.alpha = static_cast<decltype(pixel.alpha)>(
        QuantizePixelChannel<Pixel>(
            static_cast<double>(pixel.alpha) * multiplier));
    pixel.red = static_cast<decltype(pixel.red)>(
        QuantizePixelChannel<Pixel>(
            static_cast<double>(pixel.red) * multiplier));
    pixel.green = static_cast<decltype(pixel.green)>(
        QuantizePixelChannel<Pixel>(
            static_cast<double>(pixel.green) * multiplier));
    pixel.blue = static_cast<decltype(pixel.blue)>(
        QuantizePixelChannel<Pixel>(
            static_cast<double>(pixel.blue) * multiplier));
    return pixel;
}

template <typename Pixel>
Pixel ApplyLighting(
    Pixel pixel,
    const SurfaceData& surface,
    Point3 normal,
    Point3 world_position,
    const LightingState& lighting,
    const ShadowScene* shadow_scene) {
    if (!lighting.enabled) {
        return pixel;
    }
    normal = Normalize(normal);
    const Point3 view_direction = Normalize({
        lighting.camera_position.x - world_position.x,
        lighting.camera_position.y - world_position.y,
        lighting.camera_position.z - world_position.z});
    Point3 diffuse_light = lighting.ambient;
    Point3 specular_light{};
    for (std::size_t index = 0; index < lighting.light_count; ++index) {
        const RenderLight& light = lighting.lights[index];
        Point3 light_direction = light.direction;
        double light_distance =
            std::numeric_limits<double>::infinity();
        double spot_factor = 1.0;
        if (light.type != RenderLightType::Directional) {
            const Point3 to_light{
                light.position.x - world_position.x,
                light.position.y - world_position.y,
                light.position.z - world_position.z};
            light_distance = std::sqrt(Dot(to_light, to_light));
            light_direction = Normalize(to_light);
            if (light.type == RenderLightType::Spot) {
                const Point3 from_light{-light_direction.x,
                                        -light_direction.y,
                                        -light_direction.z};
                const double half_angle = std::clamp(
                    light.cone_angle * 0.5,
                    0.1,
                    179.0) * 3.14159265358979323846 / 180.0;
                const double outer_cosine = std::cos(half_angle);
                const double feather = std::clamp(
                    light.cone_feather / 100.0,
                    0.0,
                    1.0);
                const double inner_angle = half_angle * (1.0 - feather);
                const double inner_cosine = std::cos(inner_angle);
                const double cone_cosine = Dot(light.forward, from_light);
                if (cone_cosine <= outer_cosine) {
                    spot_factor = 0.0;
                } else if (cone_cosine < inner_cosine) {
                    const double range = std::max(
                        1.0e-6,
                        inner_cosine - outer_cosine);
                    const double value = std::clamp(
                        (cone_cosine - outer_cosine) / range,
                        0.0,
                        1.0);
                    spot_factor = value * value * (3.0 - 2.0 * value);
                }
            }
        }
        const double diffuse_term =
            std::max(0.0, Dot(normal, light_direction)) * spot_factor;
        if (diffuse_term <= 0.0) {
            continue;
        }
        const double shadow_visibility = ShadowVisibility(
            light,
            normal,
            world_position,
            light_direction,
            light_distance,
            shadow_scene);
        if (shadow_visibility <= 0.0) {
            continue;
        }
        const Point3 half_vector = Normalize({
            light_direction.x + view_direction.x,
            light_direction.y + view_direction.y,
            light_direction.z + view_direction.z});
        const double specular_term =
            std::pow(
                std::max(0.0, Dot(normal, half_vector)),
                std::max(1.0, static_cast<double>(surface.shininess))) *
            spot_factor;
        const double diffuse_strength =
            light.intensity * diffuse_term * shadow_visibility;
        const double specular_strength =
            light.intensity * specular_term * shadow_visibility;
        diffuse_light.x += light.color.x * diffuse_strength;
        diffuse_light.y += light.color.y * diffuse_strength;
        diffuse_light.z += light.color.z * diffuse_strength;
        specular_light.x += light.color.x * specular_strength;
        specular_light.y += light.color.y * specular_strength;
        specular_light.z += light.color.z * specular_strength;
    }
    const double diffuse_coefficient =
        static_cast<double>(surface.diffuse) / 100.0;
    const double specular_coefficient =
        static_cast<double>(surface.specular) / 100.0;
    const double metalness = std::clamp(
        static_cast<double>(surface.metalness) / 100.0,
        0.0,
        1.0);
    const double alpha = static_cast<double>(pixel.alpha);
    const auto shade = [&](auto channel, double diffuse, double specular) {
        const double base = static_cast<double>(channel);
        const double specular_tint =
            alpha * (1.0 - metalness) + base * metalness;
        const double value =
            base * diffuse * diffuse_coefficient * (1.0 - metalness) +
            specular_tint * specular * specular_coefficient;
        return QuantizePixelChannel<Pixel>(std::clamp(
            value,
            0.0,
            PixelChannelMaximum<Pixel>()));
    };
    pixel.red = static_cast<decltype(pixel.red)>(
        shade(pixel.red, diffuse_light.x, specular_light.x));
    pixel.green = static_cast<decltype(pixel.green)>(
        shade(pixel.green, diffuse_light.y, specular_light.y));
    pixel.blue = static_cast<decltype(pixel.blue)>(
        shade(pixel.blue, diffuse_light.z, specular_light.z));
    return pixel;
}

template <typename Pixel>
Pixel SampleTexture(
    const SurfaceData& surface,
    const PF_LayerDef& input,
    double u,
    double v,
    A_long texture_filter) {
    const Point2 mapped = MapImageCoordinates(surface, input, u, v);
    u = mapped.x;
    v = mapped.y;
    if (!ResolveBorderCoordinate(u, surface.image_border_mode) ||
        !ResolveBorderCoordinate(v, surface.image_border_mode)) {
        return {};
    }
    const auto pixel_at = [&](int x, int y) -> Pixel {
        const auto* row = reinterpret_cast<const Pixel*>(
            reinterpret_cast<const A_u_char*>(input.data) +
            static_cast<std::ptrdiff_t>(y) *
                static_cast<std::ptrdiff_t>(input.rowbytes));
        return row[x];
    };

    if (texture_filter == kTextureFilterNearest) {
        const int x = std::clamp(
            static_cast<int>(std::lround(u * static_cast<double>(input.width - 1))),
            0,
            static_cast<int>(input.width - 1));
        const int y = std::clamp(
            static_cast<int>(std::lround(v * static_cast<double>(input.height - 1))),
            0,
            static_cast<int>(input.height - 1));
        return ApplyOpacity(pixel_at(x, y), surface.opacity);
    }

    const double sample_x = u * static_cast<double>(input.width - 1);
    const double sample_y = v * static_cast<double>(input.height - 1);
    const int x0 = std::clamp(
        static_cast<int>(std::floor(sample_x)),
        0,
        static_cast<int>(input.width - 1));
    const int y0 = std::clamp(
        static_cast<int>(std::floor(sample_y)),
        0,
        static_cast<int>(input.height - 1));
    const int x1 = std::min(x0 + 1, static_cast<int>(input.width - 1));
    const int y1 = std::min(y0 + 1, static_cast<int>(input.height - 1));
    const double tx = sample_x - std::floor(sample_x);
    const double ty = sample_y - std::floor(sample_y);
    const Pixel p00 = pixel_at(x0, y0);
    const Pixel p10 = pixel_at(x1, y0);
    const Pixel p01 = pixel_at(x0, y1);
    const Pixel p11 = pixel_at(x1, y1);
    const auto interpolate = [&](auto c00, auto c10, auto c01, auto c11) {
        const double top = static_cast<double>(c00) * (1.0 - tx) +
                           static_cast<double>(c10) * tx;
        const double bottom = static_cast<double>(c01) * (1.0 - tx) +
                              static_cast<double>(c11) * tx;
        return QuantizePixelChannel<Pixel>(
            top * (1.0 - ty) + bottom * ty);
    };
    Pixel result{};
    result.alpha = static_cast<decltype(result.alpha)>(
        interpolate(p00.alpha, p10.alpha, p01.alpha, p11.alpha));
    result.red = static_cast<decltype(result.red)>(
        interpolate(p00.red, p10.red, p01.red, p11.red));
    result.green = static_cast<decltype(result.green)>(
        interpolate(p00.green, p10.green, p01.green, p11.green));
    result.blue = static_cast<decltype(result.blue)>(
        interpolate(p00.blue, p10.blue, p01.blue, p11.blue));
    return ApplyOpacity(result, surface.opacity);
}

template <typename Pixel>
void ClearWorld(PF_LayerDef& output) {
    for (A_long y = 0; y < output.height; ++y) {
        auto* row = reinterpret_cast<Pixel*>(
            reinterpret_cast<A_u_char*>(output.data) +
            static_cast<std::ptrdiff_t>(y) *
                static_cast<std::ptrdiff_t>(output.rowbytes));
        std::fill(row, row + output.width, Pixel{});
    }
}

template <typename Pixel>
void FinalizeDepthView(
    PF_LayerDef& output,
    const std::vector<float>& depth_buffer) {
    float minimum_inverse_depth = std::numeric_limits<float>::infinity();
    float maximum_inverse_depth = -std::numeric_limits<float>::infinity();
    for (float inverse_depth : depth_buffer) {
        if (std::isfinite(inverse_depth) && inverse_depth > 0.0F) {
            minimum_inverse_depth =
                std::min(minimum_inverse_depth, inverse_depth);
            maximum_inverse_depth =
                std::max(maximum_inverse_depth, inverse_depth);
        }
    }
    if (!std::isfinite(minimum_inverse_depth) ||
        !std::isfinite(maximum_inverse_depth)) {
        return;
    }
    const double range = static_cast<double>(
        maximum_inverse_depth - minimum_inverse_depth);
    for (A_long y = 0; y < output.height; ++y) {
        auto* row = reinterpret_cast<Pixel*>(
            reinterpret_cast<A_u_char*>(output.data) +
            static_cast<std::ptrdiff_t>(y) *
                static_cast<std::ptrdiff_t>(output.rowbytes));
        for (A_long x = 0; x < output.width; ++x) {
            const float inverse_depth =
                depth_buffer[static_cast<std::size_t>(y) *
                                 static_cast<std::size_t>(output.width) +
                             static_cast<std::size_t>(x)];
            if (!std::isfinite(inverse_depth) || inverse_depth <= 0.0F) {
                continue;
            }
            const double normalized =
                range > 1.0e-12
                    ? (static_cast<double>(inverse_depth) -
                       minimum_inverse_depth) /
                          range
                    : 1.0;
            row[x] = MakeOpaqueViewPixel<Pixel>(
                normalized,
                normalized,
                normalized);
        }
    }
}

template <typename Pixel>
void RasterizeTriangle(
    const Vertex& a,
    const Vertex& b,
    const Vertex& c,
    const SurfaceData& surface,
    const PF_LayerDef& front_input,
    const PF_LayerDef& back_input,
    PF_LayerDef& output,
    std::vector<float>& depth_buffer,
    bool perspective,
    const CameraState& camera,
    const LightingState& lighting,
    const ShadowScene* shadow_scene,
    A_long render_view,
    TextureFace texture_face) {
    if (!a.visible || !b.visible || !c.visible ||
        !IsFiniteRasterVertex(a) ||
        !IsFiniteRasterVertex(b) ||
        !IsFiniteRasterVertex(c)) {
        return;
    }
    const double area = Edge(a, b, c.x, c.y);
    if (!std::isfinite(area) || std::abs(area) < 1.0e-8) {
        return;
    }

    const double min_x_value = std::max(
        0.0,
        std::floor(std::min({a.x, b.x, c.x})));
    const double max_x_value = std::min(
        static_cast<double>(output.width - 1),
        std::ceil(std::max({a.x, b.x, c.x})));
    const double min_y_value = std::max(
        0.0,
        std::floor(std::min({a.y, b.y, c.y})));
    const double max_y_value = std::min(
        static_cast<double>(output.height - 1),
        std::ceil(std::max({a.y, b.y, c.y})));
    if (min_x_value > max_x_value || min_y_value > max_y_value) {
        return;
    }
    const int min_x = static_cast<int>(min_x_value);
    const int max_x = static_cast<int>(max_x_value);
    const int min_y = static_cast<int>(min_y_value);
    const int max_y = static_cast<int>(max_y_value);

    for (int y = min_y; y <= max_y; ++y) {
        auto* output_row = reinterpret_cast<Pixel*>(
            reinterpret_cast<A_u_char*>(output.data) +
            static_cast<std::ptrdiff_t>(y) *
                static_cast<std::ptrdiff_t>(output.rowbytes));
        for (int x = min_x; x <= max_x; ++x) {
            const double px = static_cast<double>(x) + 0.5;
            const double py = static_cast<double>(y) + 0.5;
            const double w0 = Edge(b, c, px, py) / area;
            const double w1 = Edge(c, a, px, py) / area;
            const double w2 = Edge(a, b, px, py) / area;
            if (w0 >= -1.0e-6 && w1 >= -1.0e-6 && w2 >= -1.0e-6) {
                const double inverse_depth =
                    w0 * a.inverse_depth + w1 * b.inverse_depth + w2 * c.inverse_depth;
                const size_t depth_index =
                    static_cast<size_t>(y) * static_cast<size_t>(output.width) +
                    static_cast<size_t>(x);
                if (inverse_depth <= depth_buffer[depth_index]) {
                    continue;
                }

                double u = w0 * a.u + w1 * b.u + w2 * c.u;
                double v = w0 * a.v + w1 * b.v + w2 * c.v;
                if (perspective && inverse_depth > 1.0e-12) {
                    u = (w0 * a.u * a.inverse_depth +
                         w1 * b.u * b.inverse_depth +
                         w2 * c.u * c.inverse_depth) /
                        inverse_depth;
                    v = (w0 * a.v * a.inverse_depth +
                         w1 * b.v * b.inverse_depth +
                         w2 * c.v * c.inverse_depth) /
                        inverse_depth;
                }
                const auto interpolate_attribute = [&](double av, double bv, double cv) {
                    if (perspective && inverse_depth > 1.0e-12) {
                        return (w0 * av * a.inverse_depth +
                                w1 * bv * b.inverse_depth +
                                w2 * cv * c.inverse_depth) /
                               inverse_depth;
                    }
                    return w0 * av + w1 * bv + w2 * cv;
                };
                Point3 normal{
                    interpolate_attribute(a.normal.x, b.normal.x, c.normal.x),
                    interpolate_attribute(a.normal.y, b.normal.y, c.normal.y),
                    interpolate_attribute(a.normal.z, b.normal.z, c.normal.z)};
                const Point3 world_position{
                    interpolate_attribute(
                        a.world_position.x,
                        b.world_position.x,
                        c.world_position.x),
                    interpolate_attribute(
                        a.world_position.y,
                        b.world_position.y,
                        c.world_position.y),
                    interpolate_attribute(
                        a.world_position.z,
                        b.world_position.z,
                        c.world_position.z)};
                normal = Normalize(normal);
                const Point3 view_direction = Normalize({
                    lighting.camera_position.x - world_position.x,
                    lighting.camera_position.y - world_position.y,
                    lighting.camera_position.z - world_position.z});
                const bool geometric_back_facing =
                    Dot(normal, view_direction) <= 0.0;
                if (lighting.backface_culling && geometric_back_facing) {
                    continue;
                }
                const bool use_back_texture =
                    texture_face == TextureFace::Back ||
                    (texture_face == TextureFace::Automatic &&
                     geometric_back_facing);
                Point3 shading_normal = normal;
                if (texture_face == TextureFace::Automatic &&
                    geometric_back_facing) {
                    shading_normal = {
                        -shading_normal.x,
                        -shading_normal.y,
                        -shading_normal.z};
                }
                if (render_view == kRenderViewDepth) {
                    depth_buffer[depth_index] =
                        static_cast<float>(inverse_depth);
                    continue;
                }
                if (render_view == kRenderViewUv) {
                    depth_buffer[depth_index] =
                        static_cast<float>(inverse_depth);
                    output_row[x] = MakeOpaqueViewPixel<Pixel>(
                        u,
                        v,
                        1.0 - u);
                    continue;
                }
                if (render_view == kRenderViewNormalsViewSpace) {
                    const Point3 view_normal =
                        DirectionToCameraSpace(shading_normal, camera);
                    depth_buffer[depth_index] =
                        static_cast<float>(inverse_depth);
                    output_row[x] = MakeOpaqueViewPixel<Pixel>(
                        view_normal.x * 0.5 + 0.5,
                        view_normal.y * 0.5 + 0.5,
                        0.5 - view_normal.z * 0.5);
                    continue;
                }
                Pixel sampled = SampleTexture<Pixel>(
                    surface,
                    use_back_texture ? back_input : front_input,
                    u,
                    v,
                    lighting.texture_filter);
                if (sampled.alpha == 0) {
                    continue;
                }
                sampled = ApplyLighting(
                    sampled,
                    surface,
                    shading_normal,
                    world_position,
                    lighting,
                    shadow_scene);
                depth_buffer[depth_index] = static_cast<float>(inverse_depth);
                output_row[x] = sampled;
            }
        }
    }
}

bool ClipLineToWorld(PF_LayerDef& output, Point2& start, Point2& end) {
    if (output.width <= 0 || output.height <= 0 ||
        !std::isfinite(start.x) || !std::isfinite(start.y) ||
        !std::isfinite(end.x) || !std::isfinite(end.y)) {
        return false;
    }

    const double delta_x = end.x - start.x;
    const double delta_y = end.y - start.y;
    if (!std::isfinite(delta_x) || !std::isfinite(delta_y)) {
        return false;
    }

    double minimum_t = 0.0;
    double maximum_t = 1.0;
    const auto clip = [&](double p, double q) {
        if (p == 0.0) {
            return q >= 0.0;
        }
        const double ratio = q / p;
        if (p < 0.0) {
            if (ratio > maximum_t) {
                return false;
            }
            minimum_t = std::max(minimum_t, ratio);
        } else {
            if (ratio < minimum_t) {
                return false;
            }
            maximum_t = std::min(maximum_t, ratio);
        }
        return true;
    };

    const double maximum_x = static_cast<double>(output.width - 1);
    const double maximum_y = static_cast<double>(output.height - 1);
    if (!clip(-delta_x, start.x) ||
        !clip(delta_x, maximum_x - start.x) ||
        !clip(-delta_y, start.y) ||
        !clip(delta_y, maximum_y - start.y)) {
        return false;
    }

    const Point2 original_start = start;
    start = {
        original_start.x + minimum_t * delta_x,
        original_start.y + minimum_t * delta_y};
    end = {
        original_start.x + maximum_t * delta_x,
        original_start.y + maximum_t * delta_y};
    return std::isfinite(start.x) && std::isfinite(start.y) &&
           std::isfinite(end.x) && std::isfinite(end.y);
}

template <typename Pixel>
void DrawLine(PF_LayerDef& output, Point2 start, Point2 end) {
    if (!ClipLineToWorld(output, start, end)) {
        return;
    }
    int x0 = static_cast<int>(std::lround(start.x));
    int y0 = static_cast<int>(std::lround(start.y));
    const int x1 = static_cast<int>(std::lround(end.x));
    const int y1 = static_cast<int>(std::lround(end.y));
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;

    Pixel line_pixel{};
    const auto white = static_cast<decltype(line_pixel.alpha)>(
        PixelChannelWhite<Pixel>());
    line_pixel.alpha = white;
    line_pixel.red = white;
    line_pixel.green = white;
    line_pixel.blue = white;

    while (true) {
        if (x0 >= 0 && x0 < output.width && y0 >= 0 && y0 < output.height) {
            auto* row = reinterpret_cast<Pixel*>(
                reinterpret_cast<A_u_char*>(output.data) +
                static_cast<std::ptrdiff_t>(y0) *
                    static_cast<std::ptrdiff_t>(output.rowbytes));
            row[x0] = line_pixel;
        }
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int doubled = 2 * error;
        if (doubled >= dy) {
            error += dy;
            x0 += sx;
        }
        if (doubled <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

}  // namespace

SurfaceEvaluationState BuildSurfaceEvaluationState(
    const SurfaceData& surface,
    const CameraState& camera,
    double render_scale_x,
    double render_scale_y,
    double render_scale_z) {
    SurfaceEvaluationState state;
    state.lattice = surface.lattice;
    double minimum_x = std::numeric_limits<double>::infinity();
    double minimum_y = std::numeric_limits<double>::infinity();
    double minimum_z = std::numeric_limits<double>::infinity();
    double maximum_x = -std::numeric_limits<double>::infinity();
    double maximum_y = -std::numeric_limits<double>::infinity();
    double maximum_z = -std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < state.lattice.point_count; ++index) {
        StoredPoint3& point = state.lattice.points[index];
        point.x = static_cast<float>(point.x * render_scale_x);
        point.y = static_cast<float>(point.y * render_scale_y);
        point.z = static_cast<float>(point.z * render_scale_z);
        minimum_x = std::min(minimum_x, static_cast<double>(point.x));
        minimum_y = std::min(minimum_y, static_cast<double>(point.y));
        minimum_z = std::min(minimum_z, static_cast<double>(point.z));
        maximum_x = std::max(maximum_x, static_cast<double>(point.x));
        maximum_y = std::max(maximum_y, static_cast<double>(point.y));
        maximum_z = std::max(maximum_z, static_cast<double>(point.z));
    }
    state.coordinate_transform = BuildSurfaceCoordinateTransform(
        surface,
        {camera.input_center_x, camera.input_center_y, 0.0},
        {render_scale_x, render_scale_y, render_scale_z});
    if (surface.root_transform_enabled != 0) {
        state.root_transform_enabled =
            BuildPreSceneRootTransform(
                surface.root_world_transform,
                camera.scene_transform,
                state.root_pre_scene_transform);
    }
    const SurfaceCoordinateTransform& transform = state.coordinate_transform;
    // Fixed reference cage centre = AE input/comp centre in render space.
    // camera.input_center is already downsampled; multiplying it by the render
    // scale again shifts the mesh off camera at Half/Quarter resolution. Do not use
    // Surface Position (that made point - Position + Position cancel Position
    // motion) or the live point-cloud mean (that sucked points into the pivot
    // after a large lattice write). Mapping places the fixed centre onto the
    // surface pivot so Position still translates the mesh.
    const Point3 lattice_center{
        camera.input_center_x,
        camera.input_center_y,
        0.0};
    if (std::isfinite(lattice_center.x) &&
        std::isfinite(lattice_center.y) &&
        std::isfinite(lattice_center.z) &&
        std::isfinite(transform.pivot.x) &&
        std::isfinite(transform.pivot.y) &&
        std::isfinite(transform.pivot.z)) {
        for (std::size_t index = 0; index < state.lattice.point_count;
             ++index) {
            StoredPoint3& point = state.lattice.points[index];
            const Point3 recentered = RecenterCagePoint(
                {point.x, point.y, point.z},
                lattice_center,
                transform.pivot);
            point.x = static_cast<float>(recentered.x);
            point.y = static_cast<float>(recentered.y);
            point.z = static_cast<float>(recentered.z);
        }
    }
    state.rotation_x = transform.rotation_radians.x;
    state.rotation_y = transform.rotation_radians.y;
    state.rotation_z = transform.rotation_radians.z;
    // The deform center, side-wall outward test, and scale-handle anchor all
    // want the center of the SCALED cage. Scaling pivots at the rotation
    // origin, so the cage center moves when origin != center; map it through
    // the same scaling. Equals transform.pivot for center origin or 100% scale.
    const Point3 scaled_pivot =
        ScaleSurfaceCagePoint(transform.pivot, transform);
    state.pivot_x = scaled_pivot.x;
    state.pivot_y = scaled_pivot.y;
    state.pivot_z = scaled_pivot.z;
    state.scale_x = transform.scale.x;
    state.scale_y = transform.scale.y;
    state.scale_z = transform.scale.z;
    state.rotation_origin_x = transform.rotation_origin.x;
    state.rotation_origin_y = transform.rotation_origin.y;
    state.rotation_origin_z = transform.rotation_origin.z;
    state.deform_extent_x = std::max(1.0e-6, maximum_x - minimum_x);
    state.deform_extent_y = std::max(1.0e-6, maximum_y - minimum_y);
    state.half_thickness = 0.0;
    // Roll is a cage-local evaluation layer applied after lattice sampling and
    // before surface scale/rotate. Origin is measured on the already-centered
    // evaluation lattice so gizmo and render share one frame.
    state.roll.angle_degrees = surface.roll_angle;
    state.roll.tilt_degrees = surface.roll_tilt;
    state.roll.radius = std::max(
        1.0e-6,
        static_cast<double>(surface.roll_radius) *
            std::max(1.0e-6, render_scale_x));
    state.roll.expand_per_turn =
        static_cast<double>(surface.roll_expand) *
        std::max(1.0e-6, render_scale_x);
    state.roll.origin_x = RollOriginXForLattice(
        state.lattice,
        surface.roll_tilt);
    return state;
}

Point3 EvaluateTransformedPoint(
    const SurfaceData& surface,
    const SurfaceEvaluationState& state,
    double u,
    double v) {
    Point3 point = EvaluateLattice(state.lattice, u, v);
    point = ApplySurfaceRoll(point, state.roll);
    point = ScaleSurfaceCagePoint(point, state.coordinate_transform);
    point = RotateSurfaceWorldPoint(point, state.coordinate_transform);
    if (state.root_transform_enabled) {
        point = ApplyAffine3D(state.root_pre_scene_transform, point);
    }
    return point;
}

namespace {

Point3 EvaluateSurfaceNormal(
    const SurfaceData& surface,
    const SurfaceEvaluationState& state,
    double u,
    double v) {
    constexpr double kDerivativeStep = 1.0e-4;
    const double u0 = std::max(0.0, u - kDerivativeStep);
    const double u1 = std::min(1.0, u + kDerivativeStep);
    const double v0 = std::max(0.0, v - kDerivativeStep);
    const double v1 = std::min(1.0, v + kDerivativeStep);
    const Point3 point_u0 = EvaluateTransformedPoint(surface, state, u0, v);
    const Point3 point_u1 = EvaluateTransformedPoint(surface, state, u1, v);
    const Point3 point_v0 = EvaluateTransformedPoint(surface, state, u, v0);
    const Point3 point_v1 = EvaluateTransformedPoint(surface, state, u, v1);
    return Normalize(Cross(
        {point_u1.x - point_u0.x,
         point_u1.y - point_u0.y,
         point_u1.z - point_u0.z},
        {point_v1.x - point_v0.x,
         point_v1.y - point_v0.y,
         point_v1.z - point_v0.z}));
}

void AddShadowTriangle(
    ShadowScene& scene,
    const Point3& a,
    const Point3& b,
    const Point3& c) {
    if (!IsFinitePoint3(a) ||
        !IsFinitePoint3(b) ||
        !IsFinitePoint3(c)) {
        return;
    }
    const Point3 edge_ab{b.x - a.x, b.y - a.y, b.z - a.z};
    const Point3 edge_ac{c.x - a.x, c.y - a.y, c.z - a.z};
    const Point3 area = Cross(edge_ab, edge_ac);
    if (Dot(area, area) <= 1.0e-12) {
        return;
    }
    ShadowTriangle triangle;
    triangle.a = a;
    triangle.b = b;
    triangle.c = c;
    triangle.centroid = {
        (a.x + b.x + c.x) / 3.0,
        (a.y + b.y + c.y) / 3.0,
        (a.z + b.z + c.z) / 3.0};
    triangle.bounds.Expand(a);
    triangle.bounds.Expand(b);
    triangle.bounds.Expand(c);
    scene.triangles.push_back(triangle);
}

std::uint32_t BuildShadowBvhNode(
    ShadowScene& scene,
    std::uint32_t first,
    std::uint32_t count) {
    const std::uint32_t node_index =
        static_cast<std::uint32_t>(scene.nodes.size());
    scene.nodes.emplace_back();

    ShadowBounds bounds;
    ShadowBounds centroid_bounds;
    for (std::uint32_t offset = 0; offset < count; ++offset) {
        const ShadowTriangle& triangle =
            scene.triangles[first + offset];
        bounds.Expand(triangle.bounds);
        centroid_bounds.Expand(triangle.centroid);
    }
    scene.nodes[node_index].bounds = bounds;
    if (count <= 6) {
        scene.nodes[node_index].first = first;
        scene.nodes[node_index].count = count;
        return node_index;
    }

    const Point3 centroid_extent{
        centroid_bounds.maximum.x - centroid_bounds.minimum.x,
        centroid_bounds.maximum.y - centroid_bounds.minimum.y,
        centroid_bounds.maximum.z - centroid_bounds.minimum.z};
    int axis = 0;
    if (centroid_extent.y > centroid_extent.x) {
        axis = 1;
    }
    if ((axis == 0 ? centroid_extent.x : centroid_extent.y) <
        centroid_extent.z) {
        axis = 2;
    }
    const auto coordinate = [axis](const ShadowTriangle& triangle) {
        return axis == 0
                   ? triangle.centroid.x
                   : (axis == 1
                          ? triangle.centroid.y
                          : triangle.centroid.z);
    };
    const std::uint32_t left_count = count / 2;
    auto begin = scene.triangles.begin() + first;
    auto middle = begin + left_count;
    auto end = begin + count;
    std::nth_element(
        begin,
        middle,
        end,
        [&](const ShadowTriangle& left,
            const ShadowTriangle& right) {
            return coordinate(left) < coordinate(right);
        });

    const std::uint32_t left =
        BuildShadowBvhNode(scene, first, left_count);
    const std::uint32_t right = BuildShadowBvhNode(
        scene,
        first + left_count,
        count - left_count);
    scene.nodes[node_index].left = left;
    scene.nodes[node_index].right = right;
    return node_index;
}

ShadowScene BuildShadowScene(
    const SceneData& scene_data,
    const CameraState& camera,
    double scale_x,
    double scale_y,
    double scale_z) {
    ShadowScene shadow_scene;
    constexpr int kMaximumShadowMeshQuality = 2;
    for (std::uint32_t surface_index = 0;
         surface_index < scene_data.surface_count;
         ++surface_index) {
        const SurfaceData& surface =
            scene_data.surfaces[surface_index];
        if (surface.enabled == 0) {
            continue;
        }
        const SurfaceEvaluationState evaluation =
            BuildSurfaceEvaluationState(
                surface,
                camera,
                scale_x,
                scale_y,
                scale_z);
        const int quality = std::clamp<int>(
            surface.mesh_quality,
            kMinimumMeshQuality,
            kMaximumShadowMeshQuality);
        const int divisions_x =
            std::max<int>(1, surface.lattice.divisions_x * quality);
        const int divisions_y =
            std::max<int>(1, surface.lattice.divisions_y * quality);
        const int stride = divisions_x + 1;
        std::vector<Point3> vertices(
            static_cast<std::size_t>(divisions_x + 1) *
            static_cast<std::size_t>(divisions_y + 1));
        for (int row = 0; row <= divisions_y; ++row) {
            const double v =
                static_cast<double>(row) / divisions_y;
            for (int column = 0; column <= divisions_x; ++column) {
                const double u =
                    static_cast<double>(column) / divisions_x;
                const Point3 point = EvaluateTransformedPoint(
                    surface,
                    evaluation,
                    u,
                    v);
                vertices[static_cast<std::size_t>(
                    row * stride + column)] =
                    ApplyScenePointTransform(
                        point,
                        camera.scene_transform);
            }
        }
        for (int row = 0; row < divisions_y; ++row) {
            for (int column = 0; column < divisions_x; ++column) {
                const Point3& top_left =
                    vertices[static_cast<std::size_t>(
                        row * stride + column)];
                const Point3& top_right =
                    vertices[static_cast<std::size_t>(
                        row * stride + column + 1)];
                const Point3& bottom_left =
                    vertices[static_cast<std::size_t>(
                        (row + 1) * stride + column)];
                const Point3& bottom_right =
                    vertices[static_cast<std::size_t>(
                        (row + 1) * stride + column + 1)];
                AddShadowTriangle(
                    shadow_scene,
                    top_left,
                    top_right,
                    bottom_right);
                AddShadowTriangle(
                    shadow_scene,
                    top_left,
                    bottom_right,
                    bottom_left);
            }
        }
    }
    if (!shadow_scene.triangles.empty()) {
        shadow_scene.nodes.reserve(
            shadow_scene.triangles.size() * 2);
        BuildShadowBvhNode(
            shadow_scene,
            0,
            static_cast<std::uint32_t>(
                shadow_scene.triangles.size()));
    }
    return shadow_scene;
}

template <typename Pixel>
void RasterizeSurface(
    const SurfaceData& surface,
    const PF_LayerDef& front_input,
    const PF_LayerDef& back_input,
    PF_LayerDef& output,
    std::vector<float>& depth_buffer,
    int legacy_tessellation,
    const CameraState& camera,
    const LightingState& lighting,
    const ShadowScene* shadow_scene,
    double scale_x,
    double scale_y,
    double scale_z,
    bool wireframe,
    A_long render_view) {
    const SurfaceEvaluationState evaluation = BuildSurfaceEvaluationState(
        surface,
        camera,
        scale_x,
        scale_y,
        scale_z);
    (void)legacy_tessellation;
    const int divisions_x =
        static_cast<int>(surface.lattice.divisions_x) *
        std::clamp<int>(
            surface.mesh_quality,
            kMinimumMeshQuality,
            kMaximumMeshQuality);
    const int divisions_y =
        static_cast<int>(surface.lattice.divisions_y) *
        std::clamp<int>(
            surface.mesh_quality,
            kMinimumMeshQuality,
            kMaximumMeshQuality);
    const int stride = divisions_x + 1;
    const bool has_thickness = evaluation.half_thickness > 1.0e-6;
    std::vector<Vertex> front_vertices(
        static_cast<std::size_t>(divisions_x + 1) *
        static_cast<std::size_t>(divisions_y + 1));
    std::vector<Vertex> back_vertices(front_vertices.size());

    for (int row = 0; row <= divisions_y; ++row) {
        const double v = static_cast<double>(row) / divisions_y;
        for (int column = 0; column <= divisions_x; ++column) {
            const double u = static_cast<double>(column) / divisions_x;
            const Point3 point = EvaluateTransformedPoint(surface, evaluation, u, v);
            const Point3 normal = EvaluateSurfaceNormal(surface, evaluation, u, v);
            Point3 front_point = point;
            Point3 back_point = point;
            if (has_thickness) {
                front_point.x -= normal.x * evaluation.half_thickness;
                front_point.y -= normal.y * evaluation.half_thickness;
                front_point.z -= normal.z * evaluation.half_thickness;
                back_point.x += normal.x * evaluation.half_thickness;
                back_point.y += normal.y * evaluation.half_thickness;
                back_point.z += normal.z * evaluation.half_thickness;
            }
            const size_t vertex_index = static_cast<size_t>(row * stride + column);
            front_vertices[vertex_index] = ProjectVertex(
                front_point,
                {-normal.x, -normal.y, -normal.z},
                u,
                v,
                camera);
            if (has_thickness) {
                back_vertices[vertex_index] = ProjectVertex(
                    back_point,
                    normal,
                    u,
                    v,
                    camera);
            }
        }
    }

    const auto raster_quad = [&](const Vertex& top_left,
                                 const Vertex& top_right,
                                 const Vertex& bottom_right,
                                 const Vertex& bottom_left,
                                 TextureFace texture_face) {
        RasterizeTriangle<Pixel>(
            top_left,
            top_right,
            bottom_right,
            surface,
            front_input,
            back_input,
            output,
            depth_buffer,
            camera.perspective,
            camera,
            lighting,
            shadow_scene,
            render_view,
            texture_face);
        RasterizeTriangle<Pixel>(
            top_left,
            bottom_right,
            bottom_left,
            surface,
            front_input,
            back_input,
            output,
            depth_buffer,
            camera.perspective,
            camera,
            lighting,
            shadow_scene,
            render_view,
            texture_face);
    };

    const auto raster_side_quad = [&](Vertex top_left,
                                      Vertex top_right,
                                      Vertex bottom_right,
                                      Vertex bottom_left) {
        const Point3 edge_a{
            top_right.world_position.x - top_left.world_position.x,
            top_right.world_position.y - top_left.world_position.y,
            top_right.world_position.z - top_left.world_position.z};
        const Point3 edge_b{
            bottom_left.world_position.x - top_left.world_position.x,
            bottom_left.world_position.y - top_left.world_position.y,
            bottom_left.world_position.z - top_left.world_position.z};
        Point3 face_normal = Normalize(Cross(edge_a, edge_b));
        const Point3 face_center{
            (top_left.world_position.x + top_right.world_position.x +
             bottom_right.world_position.x + bottom_left.world_position.x) * 0.25,
            (top_left.world_position.y + top_right.world_position.y +
             bottom_right.world_position.y + bottom_left.world_position.y) * 0.25,
            (top_left.world_position.z + top_right.world_position.z +
             bottom_right.world_position.z + bottom_left.world_position.z) * 0.25};
        const Point3 outward{
            face_center.x - evaluation.pivot_x,
            face_center.y - evaluation.pivot_y,
            face_center.z - evaluation.pivot_z};
        if (Dot(face_normal, outward) < 0.0) {
            face_normal = {-face_normal.x, -face_normal.y, -face_normal.z};
        }
        top_left.normal = face_normal;
        top_right.normal = face_normal;
        bottom_right.normal = face_normal;
        bottom_left.normal = face_normal;
        raster_quad(
            top_left,
            top_right,
            bottom_right,
            bottom_left,
            TextureFace::Front);
    };

    const auto raster_grid = [&](const std::vector<Vertex>& vertices,
                                 TextureFace texture_face) {
        for (int row = 0; row < divisions_y; ++row) {
            for (int column = 0; column < divisions_x; ++column) {
                const Vertex& top_left =
                    vertices[static_cast<size_t>(row * stride + column)];
                const Vertex& top_right =
                    vertices[static_cast<size_t>(row * stride + column + 1)];
                const Vertex& bottom_left =
                    vertices[static_cast<size_t>((row + 1) * stride + column)];
                const Vertex& bottom_right =
                    vertices[static_cast<size_t>((row + 1) * stride + column + 1)];
                raster_quad(
                    top_left,
                    top_right,
                    bottom_right,
                    bottom_left,
                    texture_face);
            }
        }
    };

    raster_grid(
        front_vertices,
        has_thickness ? TextureFace::Front : TextureFace::Automatic);
    if (has_thickness) {
        raster_grid(back_vertices, TextureFace::Back);

        for (int column = 0; column < divisions_x; ++column) {
            const size_t top_left = static_cast<size_t>(column);
            const size_t top_right = static_cast<size_t>(column + 1);
            raster_side_quad(
                front_vertices[top_left],
                front_vertices[top_right],
                back_vertices[top_right],
                back_vertices[top_left]);

            const size_t bottom_left =
                static_cast<size_t>(divisions_y * stride + column);
            const size_t bottom_right = bottom_left + 1;
            raster_side_quad(
                front_vertices[bottom_left],
                front_vertices[bottom_right],
                back_vertices[bottom_right],
                back_vertices[bottom_left]);
        }
        for (int row = 0; row < divisions_y; ++row) {
            const size_t left_top = static_cast<size_t>(row * stride);
            const size_t left_bottom = static_cast<size_t>((row + 1) * stride);
            raster_side_quad(
                front_vertices[left_top],
                front_vertices[left_bottom],
                back_vertices[left_bottom],
                back_vertices[left_top]);

            const size_t right_top =
                static_cast<size_t>(row * stride + divisions_x);
            const size_t right_bottom =
                static_cast<size_t>((row + 1) * stride + divisions_x);
            raster_side_quad(
                front_vertices[right_top],
                front_vertices[right_bottom],
                back_vertices[right_bottom],
                back_vertices[right_top]);
        }
    }

    if (wireframe && render_view == kRenderViewFinish) {
        const auto draw_grid = [&](const std::vector<Vertex>& vertices) {
            for (int row = 0; row <= divisions_y; ++row) {
                for (int column = 0; column < divisions_x; ++column) {
                    const Vertex& a =
                        vertices[static_cast<size_t>(row * stride + column)];
                    const Vertex& b =
                        vertices[static_cast<size_t>(row * stride + column + 1)];
                    if (a.visible && b.visible) {
                        DrawLine<Pixel>(output, {a.x, a.y}, {b.x, b.y});
                    }
                }
            }
            for (int column = 0; column <= divisions_x; ++column) {
                for (int row = 0; row < divisions_y; ++row) {
                    const Vertex& a =
                        vertices[static_cast<size_t>(row * stride + column)];
                    const Vertex& b =
                        vertices[static_cast<size_t>((row + 1) * stride + column)];
                    if (a.visible && b.visible) {
                        DrawLine<Pixel>(output, {a.x, a.y}, {b.x, b.y});
                    }
                }
            }
        };

        draw_grid(front_vertices);
        if (has_thickness) {
            draw_grid(back_vertices);
            for (int column = 0; column <= divisions_x; ++column) {
                const size_t top = static_cast<size_t>(column);
                const size_t bottom =
                    static_cast<size_t>(divisions_y * stride + column);
                if (front_vertices[top].visible && back_vertices[top].visible) {
                    DrawLine<Pixel>(
                        output,
                        {front_vertices[top].x, front_vertices[top].y},
                        {back_vertices[top].x, back_vertices[top].y});
                }
                if (front_vertices[bottom].visible && back_vertices[bottom].visible) {
                    DrawLine<Pixel>(
                        output,
                        {front_vertices[bottom].x, front_vertices[bottom].y},
                        {back_vertices[bottom].x, back_vertices[bottom].y});
                }
            }
            for (int row = 1; row < divisions_y; ++row) {
                const size_t left = static_cast<size_t>(row * stride);
                const size_t right =
                    static_cast<size_t>(row * stride + divisions_x);
                if (front_vertices[left].visible && back_vertices[left].visible) {
                    DrawLine<Pixel>(
                        output,
                        {front_vertices[left].x, front_vertices[left].y},
                        {back_vertices[left].x, back_vertices[left].y});
                }
                if (front_vertices[right].visible && back_vertices[right].visible) {
                    DrawLine<Pixel>(
                        output,
                        {front_vertices[right].x, front_vertices[right].y},
                        {back_vertices[right].x, back_vertices[right].y});
                }
            }
        }
    }
}

}  // namespace

CameraState BuildDefaultAfterEffectsCameraState(
    double center_x,
    double center_y,
    double output_offset_x,
    double output_offset_y,
    double camera_distance,
    double scale_z) {
    CameraState camera;
    camera.center_x = center_x;
    camera.center_y = center_y;
    camera.output_offset_x = output_offset_x;
    camera.output_offset_y = output_offset_y;
    camera.focal_distance = camera_distance * scale_z;
    camera.perspective = true;
    camera.position = {
        center_x,
        center_y,
        -camera.focal_distance};
    return camera;
}

SceneCoordinateTransform BuildSceneCoordinateTransform(
    PF_ParamDef* params[],
    double center_x,
    double center_y,
    bool initialize_from_input,
    double scale_x,
    double scale_y,
    double scale_z) {
    constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;
    constexpr double kMinimumScale = 1.0e-6;
    const auto safe_scale = [](double value) {
        if (!std::isfinite(value)) {
            return 1.0;
        }
        if (std::abs(value) >= kMinimumScale) {
            return value;
        }
        return value < 0.0 ? -kMinimumScale : kMinimumScale;
    };
    const PF_Point3DDef& position = params[kParamScenePosition]->u.point3d_d;
    SceneCoordinateTransform transform;
    transform.pivot = {center_x, center_y, 0.0};
    transform.position = {
        initialize_from_input
            ? center_x
            : position.x_value * scale_x,
        initialize_from_input
            ? center_y
            : position.y_value * scale_y,
        initialize_from_input
            ? 0.0
            : position.z_value * scale_z};
    transform.rotation_radians = {
        FIX_2_FLOAT(params[kParamSceneRotationX]->u.ad.value) *
            kDegreesToRadians,
        FIX_2_FLOAT(params[kParamSceneRotationY]->u.ad.value) *
            kDegreesToRadians,
        FIX_2_FLOAT(params[kParamSceneRotationZ]->u.ad.value) *
            kDegreesToRadians};
    transform.scale = {
        safe_scale(params[kParamSceneScaleX]->u.fs_d.value / 100.0),
        safe_scale(params[kParamSceneScaleY]->u.fs_d.value / 100.0),
        safe_scale(params[kParamSceneScaleZ]->u.fs_d.value / 100.0)};
    return transform;
}

namespace {
bool ResolveCompTime(PF_InData* in_data, A_Time& comp_time) {
    if (!in_data || !in_data->effect_ref || in_data->time_scale == 0) {
        return false;
    }
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    return suites.PFInterfaceSuite1()->AEGP_ConvertEffectToCompTime(
               in_data->effect_ref,
               in_data->current_time,
               in_data->time_scale,
               &comp_time) == A_Err_NONE;
}

Point3 MatrixRow(const A_Matrix4& matrix, int row) {
    return {
        matrix.mat[row][0],
        matrix.mat[row][1],
        matrix.mat[row][2]};
}

Point3 ScaledMatrixPosition(
    const A_Matrix4& matrix,
    double scale_x,
    double scale_y,
    double scale_z) {
    return {
        matrix.mat[3][0] * scale_x,
        matrix.mat[3][1] * scale_y,
        matrix.mat[3][2] * scale_z};
}

bool ResolveAfterEffectsView(
    PF_InData* in_data,
    double center_x,
    double center_y,
    double output_offset_x,
    double output_offset_y,
    double scale_x,
    double scale_y,
    double scale_z,
    CameraState& camera) {
    A_Time comp_time{};
    if (!ResolveCompTime(in_data, comp_time)) {
        return false;
    }
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    A_Matrix4 camera_to_world{};
    A_FpLong distance_to_image_plane = 0.0;
    A_short image_plane_width = 0;
    A_short image_plane_height = 0;
    if (suites.PFInterfaceSuite1()->AEGP_GetEffectCameraMatrix(
            in_data->effect_ref,
            &comp_time,
            &camera_to_world,
            &distance_to_image_plane,
            &image_plane_width,
            &image_plane_height) != A_Err_NONE ||
        !std::isfinite(distance_to_image_plane) ||
        distance_to_image_plane <= 0.0 ||
        image_plane_width <= 0 ||
        image_plane_height <= 0) {
        return false;
    }

    camera = {};
    camera.position = ScaledMatrixPosition(
        camera_to_world, scale_x, scale_y, scale_z);
    camera.right = Normalize(MatrixRow(camera_to_world, 0));
    camera.down = Normalize(MatrixRow(camera_to_world, 1));
    camera.forward = Normalize(MatrixRow(camera_to_world, 2));
    camera.focal_distance = distance_to_image_plane * scale_z;
    camera.center_x = center_x;
    camera.center_y = center_y;
    camera.output_offset_x = output_offset_x;
    camera.output_offset_y = output_offset_y;
    camera.perspective = true;
    camera.use_basis = true;

    return true;
}

bool ResolveAfterEffectsDefaultCameraDistance(
    PF_InData* in_data,
    double& camera_distance) {
    if (!in_data || !in_data->effect_ref) {
        return false;
    }
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    AEGP_LayerH effect_layer = nullptr;
    AEGP_CompH comp = nullptr;
    A_FpLong distance = 0.0;
    if (suites.PFInterfaceSuite1()->AEGP_GetEffectLayer(
            in_data->effect_ref,
            &effect_layer) != A_Err_NONE ||
        !effect_layer ||
        suites.LayerSuite5()->AEGP_GetLayerParentComp(
            effect_layer,
            &comp) != A_Err_NONE ||
        !comp ||
        suites.CameraSuite2()->AEGP_GetDefaultCameraDistanceToImagePlane(
            comp,
            &distance) != A_Err_NONE ||
        !std::isfinite(distance) ||
        distance <= 0.0) {
        return false;
    }
    camera_distance = distance;
    return true;
}

bool ReadLayerStream(
    AEGP_SuiteHandler& suites,
    AEGP_LayerH layer,
    AEGP_LayerStream stream,
    const A_Time& comp_time,
    AEGP_StreamVal& value) {
    value = {};
    return suites.StreamSuite2()->AEGP_GetLayerStreamValue(
               layer,
               stream,
               AEGP_LTimeMode_CompTime,
               &comp_time,
               FALSE,
               &value,
               nullptr) == A_Err_NONE;
}

bool ResolveAfterEffectsLights(
    PF_InData* in_data,
    double scale_x,
    double scale_y,
    double scale_z,
    LightingState& lighting) {
    A_Time comp_time{};
    if (!ResolveCompTime(in_data, comp_time)) {
        return false;
    }
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    AEGP_LayerH effect_layer = nullptr;
    AEGP_CompH comp = nullptr;
    if (suites.PFInterfaceSuite1()->AEGP_GetEffectLayer(
            in_data->effect_ref,
            &effect_layer) != A_Err_NONE ||
        !effect_layer ||
        suites.LayerSuite5()->AEGP_GetLayerParentComp(
            effect_layer,
            &comp) != A_Err_NONE ||
        !comp) {
        return false;
    }

    A_long layer_count = 0;
    if (suites.LayerSuite5()->AEGP_GetCompNumLayers(
            comp,
            &layer_count) != A_Err_NONE) {
        return false;
    }

    lighting.light_count = 0;
    lighting.ambient = {};
    bool found_light = false;
    for (A_long index = 0; index < layer_count; ++index) {
        AEGP_LayerH layer = nullptr;
        if (suites.LayerSuite5()->AEGP_GetCompLayerByIndex(
                comp,
                index,
                &layer) != A_Err_NONE ||
            !layer) {
            continue;
        }
        AEGP_ObjectType object_type = AEGP_ObjectType_NONE;
        A_Boolean active = FALSE;
        if (suites.LayerSuite5()->AEGP_GetLayerObjectType(
                layer,
                &object_type) != A_Err_NONE ||
            object_type != AEGP_ObjectType_LIGHT ||
            suites.LayerSuite5()->AEGP_IsVideoActive(
                layer,
                AEGP_LTimeMode_CompTime,
                &comp_time,
                &active) != A_Err_NONE ||
            !active) {
            continue;
        }

        AEGP_LightType light_type = AEGP_LightType_NONE;
        AEGP_StreamVal color_value{};
        AEGP_StreamVal intensity_value{};
        if (suites.LightSuite2()->AEGP_GetLightType(
                layer,
                &light_type) != A_Err_NONE ||
            !ReadLayerStream(
                suites,
                layer,
                AEGP_LayerStream_COLOR,
                comp_time,
                color_value) ||
            !ReadLayerStream(
                suites,
                layer,
                AEGP_LayerStream_INTENSITY,
                comp_time,
                intensity_value)) {
            continue;
        }
        found_light = true;
        const Point3 color{
            std::max(0.0, color_value.color.redF),
            std::max(0.0, color_value.color.greenF),
            std::max(0.0, color_value.color.blueF)};
        const double intensity = std::max(0.0, intensity_value.one_d / 100.0);
        if (light_type == AEGP_LightType_AMBIENT ||
            light_type == AEGP_LightType_ENVIRONMENT) {
            lighting.ambient.x += color.x * intensity;
            lighting.ambient.y += color.y * intensity;
            lighting.ambient.z += color.z * intensity;
            continue;
        }
        if (lighting.light_count >= lighting.lights.size()) {
            continue;
        }

        A_Matrix4 layer_to_world{};
        if (suites.LayerSuite5()->AEGP_GetLayerToWorldXform(
                layer,
                &comp_time,
                &layer_to_world) != A_Err_NONE) {
            continue;
        }
        RenderLight& light = lighting.lights[lighting.light_count++];
        light.position = ScaledMatrixPosition(
            layer_to_world, scale_x, scale_y, scale_z);
        light.forward = Normalize(MatrixRow(layer_to_world, 2));
        light.direction = {
            -light.forward.x,
            -light.forward.y,
            -light.forward.z};
        light.color = color;
        light.intensity = intensity;
        AEGP_StreamVal casts_shadows{};
        AEGP_StreamVal shadow_darkness{};
        AEGP_StreamVal shadow_diffusion{};
        light.casts_shadows =
            ReadLayerStream(
                suites,
                layer,
                AEGP_LayerStream_CASTS_SHADOWS,
                comp_time,
                casts_shadows) &&
            casts_shadows.one_d != 0.0;
        if (ReadLayerStream(
                suites,
                layer,
                AEGP_LayerStream_SHADOW_DARKNESS,
                comp_time,
                shadow_darkness)) {
            light.shadow_darkness = std::clamp(
                shadow_darkness.one_d / 100.0,
                0.0,
                1.0);
        }
        if (ReadLayerStream(
                suites,
                layer,
                AEGP_LayerStream_SHADOW_DIFFUSION,
                comp_time,
                shadow_diffusion)) {
            light.shadow_diffusion =
                std::max(0.0, shadow_diffusion.one_d);
        }
        if (light_type == AEGP_LightType_POINT) {
            light.type = RenderLightType::Point;
        } else if (light_type == AEGP_LightType_SPOT) {
            light.type = RenderLightType::Spot;
            AEGP_StreamVal cone_angle{};
            AEGP_StreamVal cone_feather{};
            if (ReadLayerStream(
                    suites,
                    layer,
                    AEGP_LayerStream_CONE_ANGLE,
                    comp_time,
                    cone_angle)) {
                light.cone_angle = cone_angle.one_d;
            }
            if (ReadLayerStream(
                    suites,
                    layer,
                    AEGP_LayerStream_CONE_FEATHER,
                    comp_time,
                    cone_feather)) {
                light.cone_feather = cone_feather.one_d;
            }
        } else {
            light.type = RenderLightType::Directional;
        }
    }
    if (!found_light) {
        lighting.ambient = {1.0, 1.0, 1.0};
    }
    return true;
}

struct PointControllerMarker {
    std::uint64_t host_layer_id{};
    std::uint64_t surface_id{};
    std::uint16_t row{};
    std::uint16_t column{};
};

enum class RootControllerKind {
    Scene,
    Surface
};

struct RootControllerMarker {
    RootControllerKind kind{RootControllerKind::Scene};
    std::uint64_t version{};
    std::uint64_t host_layer_id{};
    std::uint64_t surface_id{};
    Point3 bind_world{};
    Affine3D bind_transform{};
};

bool ParseUnsignedField(
    std::string_view marker,
    std::string_view key,
    std::uint64_t& value) {
    const std::string token = "|" + std::string(key) + "=";
    const std::size_t start = marker.find(token);
    if (start == std::string_view::npos) {
        return false;
    }
    std::size_t cursor = start + token.size();
    if (cursor >= marker.size() ||
        marker[cursor] < '0' || marker[cursor] > '9') {
        return false;
    }
    std::uint64_t parsed = 0;
    while (cursor < marker.size() &&
           marker[cursor] >= '0' && marker[cursor] <= '9') {
        const std::uint64_t digit =
            static_cast<std::uint64_t>(marker[cursor] - '0');
        if (parsed >
            (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
            return false;
        }
        parsed = parsed * 10U + digit;
        ++cursor;
    }
    if (cursor < marker.size() && marker[cursor] != '|') {
        return false;
    }
    value = parsed;
    return true;
}

bool ParseSignedField(
    std::string_view marker,
    std::string_view key,
    std::int64_t& value) {
    const std::string token = "|" + std::string(key) + "=";
    const std::size_t start = marker.find(token);
    if (start == std::string_view::npos) {
        return false;
    }
    std::size_t cursor = start + token.size();
    bool negative = false;
    if (cursor < marker.size() && marker[cursor] == '-') {
        negative = true;
        ++cursor;
    }
    if (cursor >= marker.size() ||
        marker[cursor] < '0' || marker[cursor] > '9') {
        return false;
    }
    std::uint64_t parsed = 0;
    while (cursor < marker.size() &&
           marker[cursor] >= '0' && marker[cursor] <= '9') {
        const std::uint64_t digit =
            static_cast<std::uint64_t>(marker[cursor] - '0');
        if (parsed >
            (static_cast<std::uint64_t>(
                 std::numeric_limits<std::int64_t>::max()) -
             digit) / 10U) {
            return false;
        }
        parsed = parsed * 10U + digit;
        ++cursor;
    }
    if (cursor < marker.size() && marker[cursor] != '|') {
        return false;
    }
    value = negative
                ? -static_cast<std::int64_t>(parsed)
                : static_cast<std::int64_t>(parsed);
    return true;
}

bool ParsePointControllerMarker(
    std::string_view marker,
    PointControllerMarker& controller) {
    constexpr std::string_view kPrefix = "SurfaceLabV1|point";
    if (marker.substr(0, kPrefix.size()) != kPrefix) {
        return false;
    }
    std::uint64_t row = 0;
    std::uint64_t column = 0;
    std::array<std::uint64_t, 4> id_chunks{};
    if (!ParseUnsignedField(
            marker, "host", controller.host_layer_id) ||
        !ParseUnsignedField(marker, "id0", id_chunks[0]) ||
        !ParseUnsignedField(marker, "id1", id_chunks[1]) ||
        !ParseUnsignedField(marker, "id2", id_chunks[2]) ||
        !ParseUnsignedField(marker, "id3", id_chunks[3]) ||
        !ParseUnsignedField(marker, "row", row) ||
        !ParseUnsignedField(marker, "col", column) ||
        controller.host_layer_id == 0 ||
        id_chunks[0] > 0xffffU ||
        id_chunks[1] > 0xffffU ||
        id_chunks[2] > 0xffffU ||
        id_chunks[3] > 0xffffU ||
        row > kMaximumLatticeDivisions ||
        column > kMaximumLatticeDivisions) {
        return false;
    }
    controller.surface_id =
        (id_chunks[0] << 48U) |
        (id_chunks[1] << 32U) |
        (id_chunks[2] << 16U) |
        id_chunks[3];
    if (controller.surface_id == 0) {
        return false;
    }
    controller.row = static_cast<std::uint16_t>(row);
    controller.column = static_cast<std::uint16_t>(column);
    return true;
}

bool ParseRootControllerMarker(
    std::string_view marker,
    RootControllerMarker& controller) {
    constexpr std::string_view kScenePrefix =
        "SurfaceLabV1|scene-root";
    constexpr std::string_view kSurfacePrefix =
        "SurfaceLabV1|surface-root";
    if (marker.substr(0, kScenePrefix.size()) == kScenePrefix) {
        controller.kind = RootControllerKind::Scene;
    } else if (
        marker.substr(0, kSurfacePrefix.size()) == kSurfacePrefix) {
        controller.kind = RootControllerKind::Surface;
    } else {
        return false;
    }
    std::uint64_t root_version = 0;
    std::int64_t bind_x = 0;
    std::int64_t bind_y = 0;
    std::int64_t bind_z = 0;
    if (!ParseUnsignedField(marker, "rootv", root_version) ||
        (root_version != 2 &&
         root_version != 3 &&
         root_version != 4) ||
        !ParseUnsignedField(
            marker, "host", controller.host_layer_id) ||
        !ParseSignedField(marker, "bindx", bind_x) ||
        !ParseSignedField(marker, "bindy", bind_y) ||
        !ParseSignedField(marker, "bindz", bind_z) ||
        controller.host_layer_id == 0) {
        return false;
    }
    controller.version = root_version;
    controller.bind_world = {
        static_cast<double>(bind_x) / 1000.0,
        static_cast<double>(bind_y) / 1000.0,
        static_cast<double>(bind_z) / 1000.0};
    controller.bind_transform.tx = controller.bind_world.x;
    controller.bind_transform.ty = controller.bind_world.y;
    controller.bind_transform.tz = controller.bind_world.z;
    if (root_version >= 3) {
        constexpr std::array<std::string_view, 9> kBasisFields{
            "bxx", "bxy", "bxz",
            "byx", "byy", "byz",
            "bzx", "bzy", "bzz"};
        std::array<std::int64_t, 9> basis{};
        for (std::size_t index = 0; index < basis.size(); ++index) {
            if (!ParseSignedField(
                    marker,
                    kBasisFields[index],
                    basis[index])) {
                return false;
            }
        }
        controller.bind_transform.xx =
            static_cast<double>(basis[0]) / 1000000.0;
        controller.bind_transform.xy =
            static_cast<double>(basis[1]) / 1000000.0;
        controller.bind_transform.xz =
            static_cast<double>(basis[2]) / 1000000.0;
        controller.bind_transform.yx =
            static_cast<double>(basis[3]) / 1000000.0;
        controller.bind_transform.yy =
            static_cast<double>(basis[4]) / 1000000.0;
        controller.bind_transform.yz =
            static_cast<double>(basis[5]) / 1000000.0;
        controller.bind_transform.zx =
            static_cast<double>(basis[6]) / 1000000.0;
        controller.bind_transform.zy =
            static_cast<double>(basis[7]) / 1000000.0;
        controller.bind_transform.zz =
            static_cast<double>(basis[8]) / 1000000.0;
        Affine3D inverse{};
        if (!TryInvertAffine3D(
                controller.bind_transform,
                inverse)) {
            return false;
        }
    }
    if (controller.kind == RootControllerKind::Scene) {
        return true;
    }
    std::array<std::uint64_t, 4> id_chunks{};
    if (!ParseUnsignedField(marker, "id0", id_chunks[0]) ||
        !ParseUnsignedField(marker, "id1", id_chunks[1]) ||
        !ParseUnsignedField(marker, "id2", id_chunks[2]) ||
        !ParseUnsignedField(marker, "id3", id_chunks[3]) ||
        id_chunks[0] > 0xffffU ||
        id_chunks[1] > 0xffffU ||
        id_chunks[2] > 0xffffU ||
        id_chunks[3] > 0xffffU) {
        return false;
    }
    controller.surface_id =
        (id_chunks[0] << 48U) |
        (id_chunks[1] << 32U) |
        (id_chunks[2] << 16U) |
        id_chunks[3];
    return controller.surface_id != 0;
}

bool ReadMarkerComment(
    AEGP_SuiteHandler& suites,
    AEGP_PluginID plugin_id,
    AEGP_ConstMarkerValP marker,
    std::string& comment) {
    AEGP_MemHandle unicode_handle = nullptr;
    if (!marker ||
        suites.MarkerSuite3()->AEGP_GetMarkerString(
            plugin_id,
            marker,
            AEGP_MarkerString_COMMENT,
            &unicode_handle) != A_Err_NONE ||
        !unicode_handle) {
        return false;
    }
    void* locked = nullptr;
    const A_Err lock_error =
        suites.MemorySuite1()->AEGP_LockMemHandle(
            unicode_handle,
            &locked);
    bool valid = lock_error == A_Err_NONE && locked;
    comment.clear();
    if (valid) {
        const auto* unicode = static_cast<const A_u_short*>(locked);
        constexpr std::size_t kMaximumMarkerLength = 512;
        for (std::size_t index = 0;
             index < kMaximumMarkerLength && unicode[index] != 0;
             ++index) {
            if (unicode[index] > 0x7fU) {
                valid = false;
                break;
            }
            comment.push_back(static_cast<char>(unicode[index]));
        }
    }
    if (locked) {
        suites.MemorySuite1()->AEGP_UnlockMemHandle(unicode_handle);
    }
    suites.MemorySuite1()->AEGP_FreeMemHandle(unicode_handle);
    return valid && !comment.empty();
}

bool ReadPointControllerMarker(
    AEGP_SuiteHandler& suites,
    AEGP_PluginID plugin_id,
    AEGP_LayerH layer,
    PointControllerMarker& controller) {
    AEGP_StreamRefH marker_stream = nullptr;
    if (suites.StreamSuite6()->AEGP_GetNewLayerStream(
            plugin_id,
            layer,
            AEGP_LayerStream_MARKER,
            &marker_stream) != A_Err_NONE ||
        !marker_stream) {
        return false;
    }
    A_long keyframe_count = 0;
    bool found = false;
    if (suites.KeyframeSuite5()->AEGP_GetStreamNumKFs(
            marker_stream,
            &keyframe_count) == A_Err_NONE) {
        for (A_long index = 0;
             index < keyframe_count && !found;
             ++index) {
            AEGP_StreamValue2 value{};
            if (suites.KeyframeSuite5()->AEGP_GetNewKeyframeValue(
                    plugin_id,
                    marker_stream,
                    index,
                    &value) != A_Err_NONE) {
                continue;
            }
            std::string comment;
            found =
                ReadMarkerComment(
                    suites,
                    plugin_id,
                    value.val.markerP,
                    comment) &&
                ParsePointControllerMarker(comment, controller);
            suites.StreamSuite6()->AEGP_DisposeStreamValue(&value);
        }
    }
    suites.StreamSuite6()->AEGP_DisposeStream(marker_stream);
    return found;
}

bool ReadRootControllerMarker(
    AEGP_SuiteHandler& suites,
    AEGP_PluginID plugin_id,
    AEGP_LayerH layer,
    RootControllerMarker& controller) {
    AEGP_StreamRefH marker_stream = nullptr;
    if (suites.StreamSuite6()->AEGP_GetNewLayerStream(
            plugin_id,
            layer,
            AEGP_LayerStream_MARKER,
            &marker_stream) != A_Err_NONE ||
        !marker_stream) {
        return false;
    }
    A_long keyframe_count = 0;
    bool found = false;
    if (suites.KeyframeSuite5()->AEGP_GetStreamNumKFs(
            marker_stream,
            &keyframe_count) == A_Err_NONE) {
        for (A_long index = 0;
             index < keyframe_count && !found;
             ++index) {
            AEGP_StreamValue2 value{};
            if (suites.KeyframeSuite5()->AEGP_GetNewKeyframeValue(
                    plugin_id,
                    marker_stream,
                    index,
                    &value) != A_Err_NONE) {
                continue;
            }
            std::string comment;
            found =
                ReadMarkerComment(
                    suites,
                    plugin_id,
                    value.val.markerP,
                    comment) &&
                ParseRootControllerMarker(comment, controller);
            suites.StreamSuite6()->AEGP_DisposeStreamValue(&value);
        }
    }
    suites.StreamSuite6()->AEGP_DisposeStream(marker_stream);
    return found;
}

bool BuildRootDeltaTransform(
    const A_Matrix4& layer_to_world,
    const RootControllerMarker& marker,
    Affine3D& delta_transform) {
    const Affine3D current_transform{
        layer_to_world.mat[0][0],
        layer_to_world.mat[0][1],
        layer_to_world.mat[0][2],
        layer_to_world.mat[1][0],
        layer_to_world.mat[1][1],
        layer_to_world.mat[1][2],
        layer_to_world.mat[2][0],
        layer_to_world.mat[2][1],
        layer_to_world.mat[2][2],
        layer_to_world.mat[3][0],
        layer_to_world.mat[3][1],
        layer_to_world.mat[3][2]};
    if (marker.version >= 3) {
        return BuildAffineDeltaTransform(
            marker.bind_transform,
            current_transform,
            delta_transform);
    }
    delta_transform = current_transform;
    const Point3 transformed_bind =
        ApplyAffine3D(delta_transform, marker.bind_world);
    delta_transform.tx +=
        layer_to_world.mat[3][0] - transformed_bind.x;
    delta_transform.ty +=
        layer_to_world.mat[3][1] - transformed_bind.y;
    delta_transform.tz +=
        layer_to_world.mat[3][2] - transformed_bind.z;
    return true;
}

bool IsDescendantOf(
    AEGP_SuiteHandler& suites,
    AEGP_LayerH layer,
    AEGP_LayerH ancestor) {
    if (!layer || !ancestor) {
        return false;
    }
    AEGP_LayerH current = layer;
    constexpr int kMaximumParentDepth = 64;
    for (int depth = 0; depth < kMaximumParentDepth; ++depth) {
        AEGP_LayerH parent = nullptr;
        if (suites.LayerSuite5()->AEGP_GetLayerParent(
                current,
                &parent) != A_Err_NONE ||
            !parent) {
            return false;
        }
        if (parent == ancestor) {
            return true;
        }
        current = parent;
    }
    return false;
}

Point3 TransformLayerAnchorToWorld(
    const A_Matrix4& transform,
    const AEGP_StreamVal& anchor) {
    return {
        anchor.three_d.x * transform.mat[0][0] +
            anchor.three_d.y * transform.mat[1][0] +
            anchor.three_d.z * transform.mat[2][0] +
            transform.mat[3][0],
        anchor.three_d.x * transform.mat[0][1] +
            anchor.three_d.y * transform.mat[1][1] +
            anchor.three_d.z * transform.mat[2][1] +
            transform.mat[3][1],
        anchor.three_d.x * transform.mat[0][2] +
            anchor.three_d.y * transform.mat[1][2] +
            anchor.three_d.z * transform.mat[2][2] +
            transform.mat[3][2]};
}

bool TryResolveControllerLatticePoint(
    Point3 full_world,
    const SurfaceData& surface,
    const CameraState& camera,
    double scale_x,
    double scale_y,
    double scale_z,
    Point3& lattice_point) {
    constexpr double kMinimumScale = 1.0e-10;
    if (std::abs(scale_x) <= kMinimumScale ||
        std::abs(scale_y) <= kMinimumScale ||
        std::abs(scale_z) <= kMinimumScale) {
        return false;
    }
    Point3 scene_local{};
    if (!TryInverseScenePointTransform(
            {full_world.x * scale_x,
             full_world.y * scale_y,
             full_world.z * scale_z},
            camera.scene_transform,
            scene_local)) {
        return false;
    }
    const SurfaceEvaluationState evaluation =
        BuildSurfaceEvaluationState(
            surface,
            camera,
            scale_x,
            scale_y,
            scale_z);
    const SurfaceCoordinateTransform& transform =
        evaluation.coordinate_transform;
    if (std::abs(transform.scale.x) <= kMinimumScale ||
        std::abs(transform.scale.y) <= kMinimumScale ||
        std::abs(transform.scale.z) <= kMinimumScale) {
        return false;
    }
    Point3 relative{
        scene_local.x - transform.rotation_origin.x,
        scene_local.y - transform.rotation_origin.y,
        scene_local.z - transform.rotation_origin.z};
    relative = InverseRotateVector(
        relative,
        transform.rotation_radians.x,
        transform.rotation_radians.y,
        transform.rotation_radians.z);
    const Point3 cage_point{
        transform.rotation_origin.x +
            relative.x / transform.scale.x,
        transform.rotation_origin.y +
            relative.y / transform.scale.y,
        transform.rotation_origin.z +
            relative.z / transform.scale.z};
    const StoredPoint3& original_zero = surface.lattice.points[0];
    const StoredPoint3& evaluated_zero = evaluation.lattice.points[0];
    const Point3 recenter_offset{
        static_cast<double>(evaluated_zero.x) -
            static_cast<double>(original_zero.x) * scale_x,
        static_cast<double>(evaluated_zero.y) -
            static_cast<double>(original_zero.y) * scale_y,
        static_cast<double>(evaluated_zero.z) -
            static_cast<double>(original_zero.z) * scale_z};
    lattice_point = {
        (cage_point.x - recenter_offset.x) / scale_x,
        (cage_point.y - recenter_offset.y) / scale_y,
        (cage_point.z - recenter_offset.z) / scale_z};
    return IsFinitePoint3(lattice_point);
}

void IncludeProjectedVertex(Bounds2D& bounds, const Vertex& vertex) {
    if (!vertex.visible || !std::isfinite(vertex.x) || !std::isfinite(vertex.y)) {
        return;
    }
    bounds.minimum_x = std::min(bounds.minimum_x, vertex.x);
    bounds.minimum_y = std::min(bounds.minimum_y, vertex.y);
    bounds.maximum_x = std::max(bounds.maximum_x, vertex.x);
    bounds.maximum_y = std::max(bounds.maximum_y, vertex.y);
}

void AccumulateSurfaceBounds(
    Bounds2D& bounds,
    const SurfaceData& surface,
    int legacy_tessellation,
    const CameraState& camera,
    double scale_x,
    double scale_y,
    double scale_z) {
    const SurfaceEvaluationState evaluation = BuildSurfaceEvaluationState(
        surface,
        camera,
        scale_x,
        scale_y,
        scale_z);
    const int divisions_x = static_cast<int>(ResolveDivisions(
        surface.divisions_x,
        static_cast<std::uint32_t>(legacy_tessellation)));
    const int divisions_y = static_cast<int>(ResolveDivisions(
        surface.divisions_y,
        static_cast<std::uint32_t>(legacy_tessellation)));
    const bool has_thickness = evaluation.half_thickness > 1.0e-6;

    for (int row = 0; row <= divisions_y; ++row) {
        const double v = static_cast<double>(row) / divisions_y;
        for (int column = 0; column <= divisions_x; ++column) {
            const double u = static_cast<double>(column) / divisions_x;
            const Point3 point = EvaluateTransformedPoint(surface, evaluation, u, v);
            const Point3 normal = EvaluateSurfaceNormal(surface, evaluation, u, v);
            Point3 front_point = point;
            front_point.x -= normal.x * evaluation.half_thickness;
            front_point.y -= normal.y * evaluation.half_thickness;
            front_point.z -= normal.z * evaluation.half_thickness;
            IncludeProjectedVertex(
                bounds,
                ProjectVertex(
                    front_point,
                    {-normal.x, -normal.y, -normal.z},
                    u,
                    v,
                    camera));
            if (has_thickness) {
                Point3 back_point = point;
                back_point.x += normal.x * evaluation.half_thickness;
                back_point.y += normal.y * evaluation.half_thickness;
                back_point.z += normal.z * evaluation.half_thickness;
                IncludeProjectedVertex(
                    bounds,
                    ProjectVertex(back_point, normal, u, v, camera));
            }
        }
    }
}

void LimitExpandedAxis(
    double desired_minimum,
    double desired_maximum,
    A_long source_size,
    A_long maximum_size,
    A_long& output_minimum,
    A_long& output_maximum) {
    constexpr double kSafeCoordinateLimit = 100000000.0;
    desired_minimum = std::clamp(
        desired_minimum,
        -kSafeCoordinateLimit,
        kSafeCoordinateLimit);
    desired_maximum = std::clamp(
        desired_maximum,
        -kSafeCoordinateLimit,
        kSafeCoordinateLimit);
    A_long minimum = static_cast<A_long>(std::floor(desired_minimum));
    A_long maximum = static_cast<A_long>(std::ceil(desired_maximum));
    minimum = std::min<A_long>(minimum, 0);
    maximum = std::max<A_long>(maximum, source_size);
    if (maximum - minimum <= maximum_size) {
        output_minimum = minimum;
        output_maximum = maximum;
        return;
    }

    const double left_expansion = std::max(0.0, -desired_minimum);
    const double right_expansion = std::max(0.0, desired_maximum - source_size);
    const A_long available = std::max<A_long>(0, maximum_size - source_size);
    const double total_expansion = left_expansion + right_expansion;
    A_long allocated_left = available / 2;
    if (total_expansion > 1.0e-6) {
        allocated_left = static_cast<A_long>(std::lround(
            static_cast<double>(available) * left_expansion / total_expansion));
    }
    allocated_left = std::clamp<A_long>(allocated_left, 0, available);
    output_minimum = -allocated_left;
    output_maximum = source_size + (available - allocated_left);
}

struct OutputBounds {
    A_long minimum_x{};
    A_long minimum_y{};
    A_long maximum_x{};
    A_long maximum_y{};
};

OutputBounds ComputeOutputBounds(
    PF_ParamDef*[],
    A_long input_width,
    A_long input_height,
    const SceneData&,
    const CameraState&,
    double,
    double,
    double) {
    return {0, 0, input_width, input_height};
}

}  // namespace

NullPointOverrideState ResolveNullPointOverrides(
    PF_InData* in_data,
    SceneData& scene,
    const CameraState& camera,
    double scale_x,
    double scale_y,
    double scale_z) {
    NullPointOverrideState result;
    if (!in_data || !in_data->effect_ref || !in_data->global_data) {
        return result;
    }
    const auto* global =
        reinterpret_cast<const GlobalData*>(in_data->global_data);
    A_Time comp_time{};
    if (!ResolveCompTime(in_data, comp_time)) {
        return result;
    }
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    AEGP_LayerH effect_layer = nullptr;
    AEGP_CompH comp = nullptr;
    AEGP_LayerIDVal effect_layer_id = 0;
    if (suites.PFInterfaceSuite1()->AEGP_GetEffectLayer(
            in_data->effect_ref,
            &effect_layer) != A_Err_NONE ||
        !effect_layer ||
        suites.LayerSuite5()->AEGP_GetLayerParentComp(
            effect_layer,
            &comp) != A_Err_NONE ||
        !comp ||
        suites.LayerSuite5()->AEGP_GetLayerID(
            effect_layer,
            &effect_layer_id) != A_Err_NONE) {
        return result;
    }
    A_long layer_count = 0;
    if (suites.LayerSuite5()->AEGP_GetCompNumLayers(
            comp,
            &layer_count) != A_Err_NONE) {
        return result;
    }

    struct ResolvedRootState {
        AEGP_LayerH layer{};
        Affine3D full_transform{};
        Affine3D inverse_full_transform{};
        bool valid{};
    };
    ResolvedRootState scene_root;
    std::array<ResolvedRootState, kMaximumSurfaces> surface_roots{};
    const auto find_surface_index =
        [&](std::uint64_t surface_id) {
            for (std::uint32_t index = 0;
                 index < scene.surface_count;
                 ++index) {
                if (scene.surfaces[index].lattice.surface_id ==
                    surface_id) {
                    return index;
                }
            }
            return kMaximumSurfaces;
        };

    // Resolve the roots first. A Surface Root's layer-to-world matrix already
    // contains its Scene Root parent, so it becomes the complete rigid
    // transform for that surface. Surfaces without their own root inherit the
    // Scene Root transform directly.
    for (A_long layer_index = 0;
         layer_index < layer_count;
         ++layer_index) {
        AEGP_LayerH layer = nullptr;
        if (suites.LayerSuite5()->AEGP_GetCompLayerByIndex(
                comp,
                layer_index,
                &layer) != A_Err_NONE ||
            !layer ||
            layer == effect_layer) {
            continue;
        }
        RootControllerMarker marker{};
        if (!ReadRootControllerMarker(
                suites,
                global->plugin_id,
                layer,
                marker) ||
            marker.host_layer_id !=
                static_cast<std::uint64_t>(effect_layer_id)) {
            continue;
        }
        ResolvedRootState* destination = nullptr;
        if (marker.kind == RootControllerKind::Scene) {
            destination = &scene_root;
        } else {
            const std::uint32_t surface_index =
                find_surface_index(marker.surface_id);
            if (surface_index < scene.surface_count) {
                destination = &surface_roots[surface_index];
            }
        }
        if (!destination || destination->valid) {
            continue;
        }
        A_Matrix4 layer_to_world{};
        if (suites.LayerSuite5()->AEGP_GetLayerToWorldXform(
                layer,
                &comp_time,
                &layer_to_world) != A_Err_NONE) {
            continue;
        }
        Affine3D transform{};
        if (!BuildRootDeltaTransform(
                layer_to_world,
                marker,
                transform)) {
            continue;
        }
        Affine3D inverse{};
        if (!TryInvertAffine3D(transform, inverse)) {
            continue;
        }
        destination->layer = layer;
        destination->full_transform = transform;
        destination->inverse_full_transform = inverse;
        destination->valid = true;
    }

    std::array<ResolvedRootState*, kMaximumSurfaces> effective_roots{};
    for (std::uint32_t index = 0;
         index < scene.surface_count;
         ++index) {
        ResolvedRootState* root =
            surface_roots[index].valid
                ? &surface_roots[index]
                : (scene_root.valid ? &scene_root : nullptr);
        effective_roots[index] = root;
        SurfaceData& surface = scene.surfaces[index];
        surface.root_transform_enabled = root ? 1U : 0U;
        surface.root_world_transform =
            root ? ScaleAffine3DCoordinateSystem(
                       root->full_transform,
                       scale_x,
                       scale_y,
                       scale_z)
                 : Affine3D{};
    }

    for (A_long layer_index = 0;
         layer_index < layer_count;
         ++layer_index) {
        AEGP_LayerH layer = nullptr;
        if (suites.LayerSuite5()->AEGP_GetCompLayerByIndex(
                comp,
                layer_index,
                &layer) != A_Err_NONE ||
            !layer ||
            layer == effect_layer) {
            continue;
        }
        PointControllerMarker marker{};
        if (!ReadPointControllerMarker(
                suites,
                global->plugin_id,
                layer,
                marker) ||
            marker.host_layer_id !=
                static_cast<std::uint64_t>(effect_layer_id)) {
            continue;
        }
        const std::uint32_t surface_index =
            find_surface_index(marker.surface_id);
        if (surface_index >= scene.surface_count) {
            continue;
        }
        SurfaceData& surface = scene.surfaces[surface_index];
        if (marker.row > surface.lattice.divisions_y ||
            marker.column > surface.lattice.divisions_x) {
            continue;
        }
        const std::size_t point_index = LatticePointIndex(
            surface.lattice.divisions_x,
            marker.row,
            marker.column);
        if (result.IsControlled(surface_index, point_index)) {
            continue;
        }
        A_Matrix4 layer_to_world{};
        AEGP_StreamVal anchor{};
        if (suites.LayerSuite5()->AEGP_GetLayerToWorldXform(
                layer,
                &comp_time,
                &layer_to_world) != A_Err_NONE ||
            !ReadLayerStream(
                suites,
                layer,
                AEGP_LayerStream_ANCHORPOINT,
                comp_time,
                anchor)) {
            continue;
        }
        Point3 world =
            TransformLayerAnchorToWorld(layer_to_world, anchor);
        const ResolvedRootState* root =
            effective_roots[surface_index];
        if (root &&
            IsDescendantOf(suites, layer, root->layer)) {
            world = ApplyAffine3D(
                root->inverse_full_transform,
                world);
        }
        Point3 lattice_point{};
        if (!TryResolveControllerLatticePoint(
                world,
                surface,
                camera,
                scale_x,
                scale_y,
                scale_z,
                lattice_point)) {
            continue;
        }
        StoredPoint3& stored = surface.lattice.points[point_index];
        stored.x = static_cast<float>(lattice_point.x);
        stored.y = static_cast<float>(lattice_point.y);
        stored.z = static_cast<float>(lattice_point.z);
        result.controlled[surface_index][point_index] = true;
        ++result.count;
    }
    return result;
}

CameraState BuildResolvedCameraState(
    PF_InData* in_data,
    PF_ParamDef* params[],
    double center_x,
    double center_y,
    double output_offset_x,
    double output_offset_y,
    double scale_x,
    double scale_y,
    double scale_z) {
    double default_camera_distance = 1.0;
    ResolveAfterEffectsDefaultCameraDistance(
        in_data,
        default_camera_distance);
    CameraState camera = BuildDefaultAfterEffectsCameraState(
        center_x,
        center_y,
        output_offset_x,
        output_offset_y,
        default_camera_distance,
        scale_z);
    ResolveAfterEffectsView(
        in_data,
        center_x,
        center_y,
        output_offset_x,
        output_offset_y,
        scale_x,
        scale_y,
        scale_z,
        camera);
    // The full-comp 2D host is a render window, not a scene object. AE applies
    // its layer transform after the effect, so folding the host transform back
    // into the projection shifts the comp-world surface a second time.
    camera.input_center_x = center_x;
    camera.input_center_y = center_y;
    bool initialize_from_input = false;
    for (std::uint32_t surface = 0; surface < kSurfaceCount; ++surface) {
        const PF_Handle handle =
            params[SurfaceLatticeParam(surface)]->u.arb_d.value;
        if (!handle) {
            continue;
        }
        const auto* lattice =
            static_cast<const LatticeData*>(PF_LOCK_HANDLE(handle));
        if (lattice) {
            initialize_from_input =
                NeedsInputSizedInitialization(*lattice);
            PF_UNLOCK_HANDLE(handle);
        }
        if (initialize_from_input) {
            break;
        }
    }
    camera.scene_transform = BuildSceneCoordinateTransform(
        params,
        center_x,
        center_y,
        initialize_from_input,
        scale_x,
        scale_y,
        scale_z);
    return camera;
}

PF_Err FrameSetup(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_ParamDef* params[]) {
    const PF_LayerDef& input = params[kParamInput]->u.ld;
    if (input.width <= 0 || input.height <= 0) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }

    const double scale_x = static_cast<double>(in_data->downsample_x.num) /
                           std::max<A_u_long>(1U, in_data->downsample_x.den);
    const double scale_y = static_cast<double>(in_data->downsample_y.num) /
                           std::max<A_u_long>(1U, in_data->downsample_y.den);
    const double scale_z = (scale_x + scale_y) * 0.5;
    const SceneData scene =
        ResolveSceneForFrame(in_data, params, input.width, input.height);
    const CameraState camera = BuildResolvedCameraState(
        in_data,
        params,
        static_cast<double>(input.width) * 0.5,
        static_cast<double>(input.height) * 0.5,
        0.0,
        0.0,
        scale_x,
        scale_y,
        scale_z);
    const OutputBounds bounds = ComputeOutputBounds(
        params,
        input.width,
        input.height,
        scene,
        camera,
        scale_x,
        scale_y,
        scale_z);

    out_data->width =
        std::max<A_long>(1, bounds.maximum_x - bounds.minimum_x);
    out_data->height =
        std::max<A_long>(1, bounds.maximum_y - bounds.minimum_y);
    out_data->origin.h = -bounds.minimum_x;
    out_data->origin.v = -bounds.minimum_y;
    return PF_Err_NONE;
}

namespace {

class CheckedOutLayerParam {
  public:
    explicit CheckedOutLayerParam(PF_InData* in_data) : in_data_(in_data) {}

    CheckedOutLayerParam(const CheckedOutLayerParam&) = delete;
    CheckedOutLayerParam& operator=(const CheckedOutLayerParam&) = delete;

    ~CheckedOutLayerParam() noexcept {
        if (active_) {
            PF_CHECKIN_PARAM(in_data_, &param_);
        }
    }

    PF_Err Checkout(PF_ParamIndex parameter) {
        if (active_) {
            return PF_Err_BAD_CALLBACK_PARAM;
        }
        const PF_Err error = PF_CHECKOUT_PARAM(
            in_data_,
            parameter,
            in_data_->current_time,
            in_data_->time_step,
            in_data_->time_scale,
            &param_);
        active_ = error == PF_Err_NONE;
        return error;
    }

    PF_Err Checkin() noexcept {
        if (!active_) {
            return PF_Err_NONE;
        }
        active_ = false;
        return PF_CHECKIN_PARAM(in_data_, &param_);
    }

    const PF_LayerDef& Layer() const {
        return param_.u.ld;
    }

  private:
    PF_InData* in_data_{};
    PF_ParamDef param_{};
    bool active_{};
};

bool IsUsableTextureWorld(const PF_LayerDef& world) {
    return world.data &&
           world.width > 0 &&
           world.height > 0 &&
           world.rowbytes != 0;
}

struct RenderFrameSnapshot {
    SceneData scene{};
    CameraState camera{};
    LightingState lighting{};
    int legacy_tessellation{1};
    double scale_x{1.0};
    double scale_y{1.0};
    double scale_z{1.0};
    bool wireframe{};
    A_long render_view{kRenderViewFinish};
    std::array<bool, kMaximumSurfaces> source_slots{};
    std::array<bool, kMaximumSurfaces> back_source_slots{};
    ShadowScene shadow_scene{};
    bool shadows_enabled{};
};

RenderFrameSnapshot BuildRenderFrameSnapshot(
    PF_InData* in_data,
    PF_ParamDef* params[],
    A_long input_width,
    A_long input_height,
    double output_offset_x,
    double output_offset_y) {
    RenderFrameSnapshot snapshot;
    snapshot.scale_x =
        static_cast<double>(in_data->downsample_x.num) /
        std::max<A_u_long>(1U, in_data->downsample_x.den);
    snapshot.scale_y =
        static_cast<double>(in_data->downsample_y.num) /
        std::max<A_u_long>(1U, in_data->downsample_y.den);
    snapshot.scale_z = (snapshot.scale_x + snapshot.scale_y) * 0.5;
    snapshot.wireframe = false;
    snapshot.render_view = std::clamp<A_long>(
        params[kParamRenderView]->u.pd.value,
        kRenderViewFinish,
        kRenderViewNormalsViewSpace);
    snapshot.camera = BuildResolvedCameraState(
        in_data,
        params,
        static_cast<double>(input_width) * 0.5,
        static_cast<double>(input_height) * 0.5,
        output_offset_x,
        output_offset_y,
        snapshot.scale_x,
        snapshot.scale_y,
        snapshot.scale_z);

    LightingState& lighting = snapshot.lighting;
    lighting.enabled = true;
    lighting.backface_culling = false;
    lighting.texture_filter = kTextureFilterBilinear;
    lighting.light_count = 0;
    lighting.ambient = {1.0, 1.0, 1.0};
    ResolveAfterEffectsLights(
        in_data,
        snapshot.scale_x,
        snapshot.scale_y,
        snapshot.scale_z,
        lighting);
    lighting.camera_position = snapshot.camera.position;
    snapshot.legacy_tessellation = 1;
    const double full_width = static_cast<double>(input_width) /
                              std::max(1.0e-6, snapshot.scale_x);
    const double full_height = static_cast<double>(input_height) /
                               std::max(1.0e-6, snapshot.scale_y);
    snapshot.scene = ResolveSceneForFrame(
        in_data,
        params,
        static_cast<A_long>(std::lround(full_width)),
        static_cast<A_long>(std::lround(full_height)));
    ResolveNullPointOverrides(
        in_data,
        snapshot.scene,
        snapshot.camera,
        snapshot.scale_x,
        snapshot.scale_y,
        snapshot.scale_z);
    snapshot.shadows_enabled =
        snapshot.render_view == kRenderViewFinish &&
        std::any_of(
            snapshot.lighting.lights.begin(),
            snapshot.lighting.lights.begin() +
                snapshot.lighting.light_count,
            [](const RenderLight& light) {
                return light.casts_shadows &&
                       light.shadow_darkness > 0.0;
            });
    if (snapshot.shadows_enabled) {
        snapshot.shadow_scene = BuildShadowScene(
            snapshot.scene,
            snapshot.camera,
            snapshot.scale_x,
            snapshot.scale_y,
            snapshot.scale_z);
    }
    if (snapshot.render_view == kRenderViewFinish) {
        for (std::uint32_t index = 0;
             index < snapshot.scene.surface_count;
             ++index) {
            const SurfaceData& surface = snapshot.scene.surfaces[index];
            if (surface.enabled == 0) {
                continue;
            }
            snapshot.source_slots[surface.source_slot] = true;
            if (surface.back_source_slot != 0) {
                snapshot.back_source_slots[index] = true;
            }
        }
    }
    return snapshot;
}

template <typename Pixel>
PF_Err RenderSurface(PF_InData* in_data, PF_ParamDef* params[], PF_LayerDef* output) {
    const PF_LayerDef& input = params[kParamInput]->u.ld;
    if (!IsUsableTextureWorld(input) ||
        !output->data ||
        output->width <= 0 ||
        output->height <= 0 ||
        output->rowbytes == 0) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }

    const RenderFrameSnapshot snapshot = BuildRenderFrameSnapshot(
        in_data,
        params,
        input.width,
        input.height,
        static_cast<double>(in_data->output_origin_x),
        static_cast<double>(in_data->output_origin_y));

    ClearWorld<Pixel>(*output);
    std::vector<float> depth_buffer(
        static_cast<size_t>(output->width) * static_cast<size_t>(output->height),
        -std::numeric_limits<float>::infinity());

    for (std::uint32_t index = 0;
         index < snapshot.scene.surface_count;
         ++index) {
        const SurfaceData& surface = snapshot.scene.surfaces[index];
        if (surface.enabled == 0) {
            continue;
        }

        CheckedOutLayerParam front_checkout(in_data);
        const PF_Err front_checkout_error = front_checkout.Checkout(
            SurfaceSourceParam(surface.source_slot));
        if (front_checkout_error != PF_Err_NONE) {
            return front_checkout_error;
        }

        const bool separate_back_checkout =
            surface.back_source_slot != 0;
        CheckedOutLayerParam back_checkout(in_data);
        if (separate_back_checkout) {
            const PF_Err back_checkout_error = back_checkout.Checkout(
                SurfaceBackSourceParam(index));
            if (back_checkout_error != PF_Err_NONE) {
                return back_checkout_error;
            }
        }

        const PF_LayerDef& front_texture =
            IsUsableTextureWorld(front_checkout.Layer())
                ? front_checkout.Layer()
                : input;
        const PF_LayerDef& back_texture = separate_back_checkout
                                              ? (IsUsableTextureWorld(
                                                     back_checkout.Layer())
                                                     ? back_checkout.Layer()
                                                     : input)
                                              : front_texture;
        RasterizeSurface<Pixel>(
            surface,
            front_texture,
            back_texture,
            *output,
            depth_buffer,
            snapshot.legacy_tessellation,
            snapshot.camera,
            snapshot.lighting,
            snapshot.shadows_enabled
                ? &snapshot.shadow_scene
                : nullptr,
            snapshot.scale_x,
            snapshot.scale_y,
            snapshot.scale_z,
            snapshot.wireframe,
            snapshot.render_view);

        if (separate_back_checkout) {
            const PF_Err back_checkin_error = back_checkout.Checkin();
            if (back_checkin_error != PF_Err_NONE) {
                return back_checkin_error;
            }
        }
        const PF_Err front_checkin_error = front_checkout.Checkin();
        if (front_checkin_error != PF_Err_NONE) {
            return front_checkin_error;
        }
    }
    if (snapshot.render_view == kRenderViewDepth) {
        FinalizeDepthView<Pixel>(*output, depth_buffer);
    }

    return PF_Err_NONE;
}

}  // namespace

PF_Err Render(PF_InData* in_data, PF_ParamDef* params[], PF_LayerDef* output) {
    if (PF_WORLD_IS_DEEP(output)) {
        return RenderSurface<PF_Pixel16>(in_data, params, output);
    }
    return RenderSurface<PF_Pixel8>(in_data, params, output);
}

namespace {

constexpr A_long kSmartCheckoutStride =
    1 + 2 * static_cast<A_long>(kMaximumSurfaces);

A_long SmartInputCheckoutId(std::size_t sample) {
    return static_cast<A_long>(sample) * kSmartCheckoutStride;
}

A_long SmartSourceCheckoutId(
    std::size_t sample,
    std::uint32_t source) {
    return SmartInputCheckoutId(sample) + 1 +
           static_cast<A_long>(source);
}

A_long SmartBackSourceCheckoutId(
    std::size_t sample,
    std::uint32_t surface) {
    return SmartInputCheckoutId(sample) + 1 +
           static_cast<A_long>(kMaximumSurfaces) +
           static_cast<A_long>(surface);
}

struct SmartParameterSet {
    explicit SmartParameterSet(PF_InData* in_data)
        : in_data(in_data),
          definitions(static_cast<std::size_t>(kParamCount)),
          pointers(static_cast<std::size_t>(kParamCount)),
          checked(static_cast<std::size_t>(kParamCount), false) {
        for (std::size_t index = 0; index < pointers.size(); ++index) {
            pointers[index] = &definitions[index];
        }
    }

    SmartParameterSet(const SmartParameterSet&) = delete;
    SmartParameterSet& operator=(const SmartParameterSet&) = delete;

    // Every checkout must be balanced with a checkin, including on the error
    // returns out of SmartPreRender; otherwise each PreRender leaks its
    // checked-out params (and the arbitrary SceneData copy with them).
    ~SmartParameterSet() noexcept {
        for (std::size_t index = 0; index < checked.size(); ++index) {
            if (checked[index]) {
                PF_CHECKIN_PARAM(in_data, &definitions[index]);
            }
        }
    }

    PF_InData* in_data{};
    std::vector<PF_ParamDef> definitions;
    std::vector<PF_ParamDef*> pointers;
    std::vector<bool> checked;
};

PF_Err CheckoutSmartParameter(
    PF_InData* in_data,
    SmartParameterSet& parameters,
    PF_ParamIndex index) {
    const std::size_t offset = static_cast<std::size_t>(index);
    if (offset >= parameters.definitions.size()) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }
    if (parameters.checked[offset]) {
        return PF_Err_NONE;
    }
    const PF_Err error = PF_CHECKOUT_PARAM(
        in_data,
        index,
        in_data->current_time,
        in_data->time_step,
        in_data->time_scale,
        &parameters.definitions[offset]);
    if (error == PF_Err_NONE) {
        parameters.checked[offset] = true;
    }
    return error;
}

PF_Err CheckoutSmartRenderParameters(
    PF_InData* in_data,
    SmartParameterSet& parameters) {
    constexpr std::array<PF_ParamIndex, 8> kFrameParameters = {
        kParamScenePosition,
        kParamSceneRotationX,
        kParamSceneRotationY,
        kParamSceneRotationZ,
        kParamSceneScaleX,
        kParamSceneScaleY,
        kParamSceneScaleZ,
        kParamRenderView};
    for (PF_ParamIndex index : kFrameParameters) {
        const PF_Err error =
            CheckoutSmartParameter(in_data, parameters, index);
        if (error != PF_Err_NONE) {
            return error;
        }
    }
    for (std::uint32_t surface = 0;
         surface < kSurfaceCount;
        ++surface) {
        for (PF_ParamIndex offset = kSurfaceSourceOffset;
             offset <= kSurfaceMetalnessOffset;
             ++offset) {
            const PF_Err error = CheckoutSmartParameter(
                in_data,
                parameters,
                SurfaceParam(
                    surface,
                    static_cast<SurfaceParamOffset>(offset)));
            if (error != PF_Err_NONE) {
                return error;
            }
        }
    }
    return PF_Err_NONE;
}

struct SmartRenderSnapshot {
    std::vector<RenderFrameSnapshot> samples;
};

void DeleteSmartRenderSnapshot(void* data) {
    delete static_cast<SmartRenderSnapshot*>(data);
}

PF_LRect FullTextureRequestRect() {
    return {0, 0, PF_MAX_WORLD_WIDTH, PF_MAX_WORLD_HEIGHT};
}

PF_LRect IntersectRects(const PF_LRect& first, const PF_LRect& second) {
    PF_LRect result{
        std::max(first.left, second.left),
        std::max(first.top, second.top),
        std::min(first.right, second.right),
        std::min(first.bottom, second.bottom)};
    if (result.left >= result.right || result.top >= result.bottom) {
        return {};
    }
    return result;
}

class CheckedOutSmartLayerPixels {
  public:
    CheckedOutSmartLayerPixels() = default;
    CheckedOutSmartLayerPixels(const CheckedOutSmartLayerPixels&) = delete;
    CheckedOutSmartLayerPixels& operator=(
        const CheckedOutSmartLayerPixels&) = delete;

    ~CheckedOutSmartLayerPixels() noexcept {
        if (checked_out_ && callbacks_) {
            callbacks_->checkin_layer_pixels(effect_ref_, checkout_id_);
        }
    }

    PF_Err Checkout(
        PF_InData* in_data,
        PF_SmartRenderExtra* extra,
        A_long checkout_id) {
        effect_ref_ = in_data->effect_ref;
        callbacks_ = extra->cb;
        checkout_id_ = checkout_id;
        const PF_Err error = callbacks_->checkout_layer_pixels(
            effect_ref_,
            checkout_id_,
            &world_);
        checked_out_ = error == PF_Err_NONE;
        return error;
    }

    const PF_LayerDef* World() const {
        return world_;
    }

  private:
    PF_ProgPtr effect_ref_{};
    PF_SmartRenderCallbacks* callbacks_{};
    A_long checkout_id_{};
    PF_EffectWorld* world_{};
    bool checked_out_{};
};

template <typename Pixel>
PF_Err RenderSmartFrame(
    const RenderFrameSnapshot& snapshot,
    const PF_LayerDef& input,
    const std::array<const PF_LayerDef*, kMaximumSurfaces>& source_worlds,
    const std::array<const PF_LayerDef*, kMaximumSurfaces>&
        back_source_worlds,
    PF_LayerDef& output) {
    if (!IsUsableTextureWorld(input) ||
        !output.data ||
        output.width <= 0 ||
        output.height <= 0 ||
        output.rowbytes == 0) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }

    CameraState camera = snapshot.camera;
    camera.output_offset_x = -static_cast<double>(output.origin_x);
    camera.output_offset_y = -static_cast<double>(output.origin_y);
    LightingState lighting = snapshot.lighting;
    lighting.camera_position = camera.position;

    ClearWorld<Pixel>(output);
    std::vector<float> depth_buffer(
        static_cast<std::size_t>(output.width) *
            static_cast<std::size_t>(output.height),
        -std::numeric_limits<float>::infinity());
    for (std::uint32_t index = 0;
         index < snapshot.scene.surface_count;
         ++index) {
        const SurfaceData& surface = snapshot.scene.surfaces[index];
        if (surface.enabled == 0) {
            continue;
        }
        const PF_LayerDef* front_world = source_worlds[surface.source_slot];
        const PF_LayerDef* back_world =
            surface.back_source_slot != 0
                ? back_source_worlds[index]
                : front_world;
        const PF_LayerDef& front_texture =
            front_world && IsUsableTextureWorld(*front_world)
                ? *front_world
                : input;
        const PF_LayerDef& back_texture =
            back_world && IsUsableTextureWorld(*back_world)
                ? *back_world
                : input;
        RasterizeSurface<Pixel>(
            surface,
            front_texture,
            back_texture,
            output,
            depth_buffer,
            snapshot.legacy_tessellation,
            camera,
            lighting,
            snapshot.shadows_enabled
                ? &snapshot.shadow_scene
                : nullptr,
            snapshot.scale_x,
            snapshot.scale_y,
            snapshot.scale_z,
            snapshot.wireframe,
            snapshot.render_view);
    }
    if (snapshot.render_view == kRenderViewDepth) {
        FinalizeDepthView<Pixel>(output, depth_buffer);
    }
    return PF_Err_NONE;
}

template <typename Pixel>
PF_Err RenderMotionSamples(
    const SmartRenderSnapshot& render_snapshot,
    const std::vector<const PF_LayerDef*>& inputs,
    const std::vector<
        std::array<const PF_LayerDef*, kMaximumSurfaces>>&
        source_worlds,
    const std::vector<
        std::array<const PF_LayerDef*, kMaximumSurfaces>>&
        back_source_worlds,
    PF_LayerDef& output) {
    const std::size_t sample_count = render_snapshot.samples.size();
    if (sample_count == 0 ||
        inputs.size() != sample_count ||
        source_worlds.size() != sample_count ||
        back_source_worlds.size() != sample_count) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }
    if (sample_count == 1) {
        return RenderSmartFrame<Pixel>(
            render_snapshot.samples[0],
            *inputs[0],
            source_worlds[0],
            back_source_worlds[0],
            output);
    }

    const std::size_t pixel_count =
        static_cast<std::size_t>(output.width) *
        static_cast<std::size_t>(output.height);
    std::vector<Pixel> sample_pixels(pixel_count);
    std::vector<float> accumulation(pixel_count * 4U, 0.0F);
    PF_LayerDef sample_world = output;
    sample_world.data =
        reinterpret_cast<PF_PixelPtr>(sample_pixels.data());
    sample_world.rowbytes =
        static_cast<A_long>(
            static_cast<std::size_t>(output.width) * sizeof(Pixel));

    for (std::size_t sample = 0;
         sample < sample_count;
         ++sample) {
        const PF_Err error = RenderSmartFrame<Pixel>(
            render_snapshot.samples[sample],
            *inputs[sample],
            source_worlds[sample],
            back_source_worlds[sample],
            sample_world);
        if (error != PF_Err_NONE) {
            return error;
        }
        for (std::size_t index = 0; index < pixel_count; ++index) {
            const Pixel& pixel = sample_pixels[index];
            accumulation[index * 4U] +=
                static_cast<float>(pixel.alpha);
            accumulation[index * 4U + 1U] +=
                static_cast<float>(pixel.red);
            accumulation[index * 4U + 2U] +=
                static_cast<float>(pixel.green);
            accumulation[index * 4U + 3U] +=
                static_cast<float>(pixel.blue);
        }
    }

    const float inverse_samples =
        1.0F / static_cast<float>(sample_count);
    for (A_long y = 0; y < output.height; ++y) {
        auto* row = reinterpret_cast<Pixel*>(
            reinterpret_cast<A_u_char*>(output.data) +
            static_cast<std::ptrdiff_t>(y) *
                static_cast<std::ptrdiff_t>(output.rowbytes));
        for (A_long x = 0; x < output.width; ++x) {
            const std::size_t index =
                static_cast<std::size_t>(y) *
                    static_cast<std::size_t>(output.width) +
                static_cast<std::size_t>(x);
            const auto channel = [&](std::size_t offset) {
                const float value =
                    accumulation[index * 4U + offset] *
                    inverse_samples;
                if constexpr (std::is_same_v<Pixel, PF_PixelFloat>) {
                    return value;
                } else {
                    return static_cast<decltype(Pixel{}.alpha)>(
                        std::lround(value));
                }
            };
            row[x].alpha = channel(0);
            row[x].red = channel(1);
            row[x].green = channel(2);
            row[x].blue = channel(3);
        }
    }
    return PF_Err_NONE;
}

}  // namespace

namespace {

std::vector<A_long> ResolveMotionSampleTimes(PF_InData* in_data) {
    if (!in_data || !in_data->effect_ref || in_data->time_step <= 0) {
        return {in_data ? in_data->current_time : 0};
    }
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    AEGP_LayerH effect_layer = nullptr;
    AEGP_CompH comp = nullptr;
    AEGP_LayerFlags layer_flags = AEGP_LayerFlag_NONE;
    AEGP_CompFlags comp_flags = 0;
    if (suites.PFInterfaceSuite1()->AEGP_GetEffectLayer(
            in_data->effect_ref,
            &effect_layer) != A_Err_NONE ||
        !effect_layer ||
        suites.LayerSuite5()->AEGP_GetLayerParentComp(
            effect_layer,
            &comp) != A_Err_NONE ||
        !comp ||
        suites.LayerSuite5()->AEGP_GetLayerFlags(
            effect_layer,
            &layer_flags) != A_Err_NONE ||
        suites.CompSuite12()->AEGP_GetCompFlags(
            comp,
            &comp_flags) != A_Err_NONE ||
        (layer_flags & AEGP_LayerFlag_MOTION_BLUR) == 0 ||
        (comp_flags & AEGP_CompFlag_ENABLE_MOTION_BLUR) == 0) {
        return {in_data->current_time};
    }

    A_long suggested_samples = 8;
    if (suites.CompSuite12()
            ->AEGP_GetCompSuggestedMotionBlurSamples(
                comp,
                &suggested_samples) != A_Err_NONE) {
        suggested_samples = 8;
    }
    const std::vector<std::int64_t> wide_times =
        BuildSubframeSampleTimes(
            in_data->current_time,
            in_data->time_step,
            FIX_2_FLOAT(in_data->shutter_angle),
            FIX_2_FLOAT(in_data->shutter_phase),
            static_cast<std::uint32_t>(
                std::clamp<A_long>(suggested_samples, 2, 32)));
    std::vector<A_long> times;
    times.reserve(wide_times.size());
    for (std::int64_t time : wide_times) {
        times.push_back(static_cast<A_long>(std::clamp<std::int64_t>(
            time,
            std::numeric_limits<A_long>::min(),
            std::numeric_limits<A_long>::max())));
    }
    return times;
}

// AE's frame cache keys on stream parameters and checked-out layer frames.
// Everything this effect reads through AEGP suites -- the comp camera, the
// comp lights, and future external controller state -- is invisible to that
// key. Without mixing that state into the render GUID,
// editing only the camera re-serves a stale frame rendered from the old
// pose while the 3D Nulls track the live view: the render appears to
// rotate the wrong way around a mirrored hinge, and the mismatch depends
// on edit order, which is what made it so hard to reproduce consistently.
// The digest is a flat, padding-free double array so identical state always
// produces identical bytes (a struct memcpy would mix indeterminate
// padding and defeat caching entirely).
void AppendPointDigest(std::vector<double>& digest, const Point3& point) {
    digest.push_back(point.x);
    digest.push_back(point.y);
    digest.push_back(point.z);
}

std::vector<double> BuildExternalStateDigest(
    const CameraState& camera,
    const LightingState& lighting,
    const SceneData& scene) {
    std::vector<double> digest;
    digest.reserve(
        48 +
        lighting.light_count * 16 +
        scene.surface_count * 4 +
        kMaximumLatticePoints * 3);
    AppendPointDigest(digest, camera.scene_transform.pivot);
    AppendPointDigest(digest, camera.scene_transform.position);
    AppendPointDigest(digest, camera.scene_transform.scale);
    AppendPointDigest(digest, camera.scene_transform.rotation_radians);
    AppendPointDigest(digest, camera.position);
    AppendPointDigest(digest, camera.right);
    AppendPointDigest(digest, camera.down);
    AppendPointDigest(digest, camera.forward);
    digest.push_back(camera.rotation_x);
    digest.push_back(camera.rotation_y);
    digest.push_back(camera.rotation_z);
    digest.push_back(camera.focal_distance);
    digest.push_back(camera.center_x);
    digest.push_back(camera.center_y);
    digest.push_back(camera.output_offset_x);
    digest.push_back(camera.output_offset_y);
    digest.push_back(camera.comp_to_output.xx);
    digest.push_back(camera.comp_to_output.xy);
    digest.push_back(camera.comp_to_output.yx);
    digest.push_back(camera.comp_to_output.yy);
    digest.push_back(camera.comp_to_output.tx);
    digest.push_back(camera.comp_to_output.ty);
    digest.push_back(camera.perspective ? 1.0 : 0.0);
    digest.push_back(camera.use_basis ? 1.0 : 0.0);
    digest.push_back(camera.use_comp_to_output ? 1.0 : 0.0);
    digest.push_back(lighting.enabled ? 1.0 : 0.0);
    digest.push_back(static_cast<double>(lighting.light_count));
    AppendPointDigest(digest, lighting.ambient);
    for (std::size_t index = 0; index < lighting.light_count; ++index) {
        const RenderLight& light = lighting.lights[index];
        digest.push_back(static_cast<double>(
            static_cast<int>(light.type)));
        AppendPointDigest(digest, light.position);
        AppendPointDigest(digest, light.direction);
        AppendPointDigest(digest, light.forward);
        AppendPointDigest(digest, light.color);
        digest.push_back(light.intensity);
        digest.push_back(light.cone_angle);
        digest.push_back(light.cone_feather);
        digest.push_back(light.casts_shadows ? 1.0 : 0.0);
        digest.push_back(light.shadow_darkness);
        digest.push_back(light.shadow_diffusion);
    }
    digest.push_back(static_cast<double>(scene.surface_count));
    for (std::uint32_t surface_index = 0;
         surface_index < scene.surface_count;
         ++surface_index) {
        const LatticeData& lattice =
            scene.surfaces[surface_index].lattice;
        digest.push_back(static_cast<double>(lattice.surface_id));
        digest.push_back(static_cast<double>(lattice.divisions_x));
        digest.push_back(static_cast<double>(lattice.divisions_y));
        digest.push_back(static_cast<double>(lattice.point_count));
        const SurfaceData& surface = scene.surfaces[surface_index];
        digest.push_back(static_cast<double>(surface.source_slot));
        digest.push_back(static_cast<double>(surface.back_source_slot));
        digest.push_back(static_cast<double>(surface.image_size_mode));
        digest.push_back(static_cast<double>(surface.image_border_mode));
        digest.push_back(surface.opacity);
        digest.push_back(surface.diffuse);
        digest.push_back(surface.specular);
        digest.push_back(surface.metalness);
        digest.push_back(surface.shininess);
        digest.push_back(
            surface.root_transform_enabled != 0 ? 1.0 : 0.0);
        const Affine3D& root = surface.root_world_transform;
        digest.insert(
            digest.end(),
            {
                root.xx, root.xy, root.xz,
                root.yx, root.yy, root.yz,
                root.zx, root.zy, root.zz,
                root.tx, root.ty, root.tz});
        for (std::size_t point_index = 0;
             point_index < lattice.point_count;
             ++point_index) {
            const StoredPoint3& point = lattice.points[point_index];
            digest.push_back(point.x);
            digest.push_back(point.y);
            digest.push_back(point.z);
        }
    }
    return digest;
}

}  // namespace

PF_Err SmartPreRender(
    PF_InData* in_data,
    PF_OutData*,
    PF_PreRenderExtra* extra) {
    if (!in_data || !extra || !extra->input || !extra->output || !extra->cb) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }

    PF_RenderRequest texture_request = extra->input->output_request;
    texture_request.rect = FullTextureRequestRect();
    const std::vector<A_long> sample_times =
        ResolveMotionSampleTimes(in_data);
    auto snapshot = std::make_unique<SmartRenderSnapshot>();
    snapshot->samples.reserve(sample_times.size());
    std::vector<double> external_state;
    external_state.push_back(
        static_cast<double>(sample_times.size()));
    external_state.push_back(FIX_2_FLOAT(in_data->shutter_angle));
    external_state.push_back(FIX_2_FLOAT(in_data->shutter_phase));

    PF_LRect maximum_rect{};
    bool has_bounds = false;
    for (std::size_t sample = 0;
         sample < sample_times.size();
         ++sample) {
        PF_InData sample_in = *in_data;
        sample_in.current_time = sample_times[sample];
        PF_CheckoutResult input_result{};
        PF_Err error = extra->cb->checkout_layer(
            in_data->effect_ref,
            kParamInput,
            SmartInputCheckoutId(sample),
            &texture_request,
            sample_in.current_time,
            sample_in.time_step,
            sample_in.time_scale,
            &input_result);
        if (error != PF_Err_NONE) {
            return error;
        }
        const A_long input_width = std::max<A_long>(
            1,
            input_result.max_result_rect.right -
                input_result.max_result_rect.left);
        const A_long input_height = std::max<A_long>(
            1,
            input_result.max_result_rect.bottom -
                input_result.max_result_rect.top);
        SmartParameterSet parameters(&sample_in);
        error = CheckoutSmartRenderParameters(
            &sample_in,
            parameters);
        if (error != PF_Err_NONE) {
            return error;
        }
        RenderFrameSnapshot frame = BuildRenderFrameSnapshot(
            &sample_in,
            parameters.pointers.data(),
            input_width,
            input_height,
            0.0,
            0.0);
        external_state.push_back(
            static_cast<double>(sample_in.current_time));
        const std::vector<double> frame_state =
            BuildExternalStateDigest(
                frame.camera,
                frame.lighting,
                frame.scene);
        external_state.insert(
            external_state.end(),
            frame_state.begin(),
            frame_state.end());

        for (std::uint32_t slot = 0;
             slot <
                 static_cast<std::uint32_t>(
                     frame.source_slots.size());
             ++slot) {
            if (!frame.source_slots[slot]) {
                continue;
            }
            PF_CheckoutResult source_result{};
            error = extra->cb->checkout_layer(
                in_data->effect_ref,
                SurfaceSourceParam(slot),
                SmartSourceCheckoutId(sample, slot),
                &texture_request,
                sample_in.current_time,
                sample_in.time_step,
                sample_in.time_scale,
                &source_result);
            if (error != PF_Err_NONE) {
                return error;
            }
        }
        for (std::uint32_t surface = 0;
             surface <
                 static_cast<std::uint32_t>(
                     frame.back_source_slots.size());
             ++surface) {
            if (!frame.back_source_slots[surface]) {
                continue;
            }
            PF_CheckoutResult source_result{};
            error = extra->cb->checkout_layer(
                in_data->effect_ref,
                SurfaceBackSourceParam(surface),
                SmartBackSourceCheckoutId(sample, surface),
                &texture_request,
                sample_in.current_time,
                sample_in.time_step,
                sample_in.time_scale,
                &source_result);
            if (error != PF_Err_NONE) {
                return error;
            }
        }

        const OutputBounds bounds = ComputeOutputBounds(
            parameters.pointers.data(),
            input_width,
            input_height,
            frame.scene,
            frame.camera,
            frame.scale_x,
            frame.scale_y,
            frame.scale_z);
        const PF_LRect frame_rect{
            bounds.minimum_x,
            bounds.minimum_y,
            bounds.maximum_x,
            bounds.maximum_y};
        if (!has_bounds) {
            maximum_rect = frame_rect;
            has_bounds = true;
        } else {
            maximum_rect.left =
                std::min(maximum_rect.left, frame_rect.left);
            maximum_rect.top =
                std::min(maximum_rect.top, frame_rect.top);
            maximum_rect.right =
                std::max(maximum_rect.right, frame_rect.right);
            maximum_rect.bottom =
                std::max(maximum_rect.bottom, frame_rect.bottom);
        }
        snapshot->samples.push_back(std::move(frame));
    }

    PF_Err error = extra->cb->GuidMixInPtr(
        in_data->effect_ref,
        static_cast<A_u_long>(external_state.size() * sizeof(double)),
        external_state.data());
    if (error != PF_Err_NONE) {
        return error;
    }
    extra->output->max_result_rect = maximum_rect;
    extra->output->result_rect = IntersectRects(
        maximum_rect,
        extra->input->output_request.rect);
    extra->output->pre_render_data = snapshot.release();
    extra->output->delete_pre_render_data_func = DeleteSmartRenderSnapshot;
    return PF_Err_NONE;
}

PF_Err SmartRender(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_SmartRenderExtra* extra) {
    if (!in_data || !out_data || !extra || !extra->input ||
        !extra->cb || !extra->input->pre_render_data) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }
    const auto& snapshot = *static_cast<const SmartRenderSnapshot*>(
        extra->input->pre_render_data);
    if (snapshot.samples.empty()) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }

    std::vector<std::unique_ptr<CheckedOutSmartLayerPixels>>
        input_checkouts;
    std::vector<
        std::array<
            std::unique_ptr<CheckedOutSmartLayerPixels>,
            kMaximumSurfaces>>
        source_checkouts(snapshot.samples.size());
    std::vector<
        std::array<
            std::unique_ptr<CheckedOutSmartLayerPixels>,
            kMaximumSurfaces>>
        back_source_checkouts(snapshot.samples.size());
    std::vector<const PF_LayerDef*> input_worlds;
    std::vector<
        std::array<const PF_LayerDef*, kMaximumSurfaces>>
        source_worlds(snapshot.samples.size());
    std::vector<
        std::array<const PF_LayerDef*, kMaximumSurfaces>>
        back_source_worlds(snapshot.samples.size());
    input_checkouts.reserve(snapshot.samples.size());
    input_worlds.reserve(snapshot.samples.size());
    for (std::size_t sample = 0;
         sample < snapshot.samples.size();
         ++sample) {
        input_checkouts.push_back(
            std::make_unique<CheckedOutSmartLayerPixels>());
        PF_Err error = input_checkouts.back()->Checkout(
            in_data,
            extra,
            SmartInputCheckoutId(sample));
        if (error != PF_Err_NONE ||
            !input_checkouts.back()->World()) {
            return error != PF_Err_NONE
                       ? error
                       : PF_Err_BAD_CALLBACK_PARAM;
        }
        input_worlds.push_back(input_checkouts.back()->World());
        for (std::uint32_t slot = 0;
             slot <
                 static_cast<std::uint32_t>(
                     snapshot.samples[sample]
                         .source_slots.size());
             ++slot) {
            if (!snapshot.samples[sample].source_slots[slot]) {
                continue;
            }
            source_checkouts[sample][slot] =
                std::make_unique<CheckedOutSmartLayerPixels>();
            error = source_checkouts[sample][slot]->Checkout(
                in_data,
                extra,
                SmartSourceCheckoutId(sample, slot));
            if (error != PF_Err_NONE) {
                return error;
            }
            source_worlds[sample][slot] =
                source_checkouts[sample][slot]->World();
        }
        for (std::uint32_t surface = 0;
             surface <
                 static_cast<std::uint32_t>(
                     snapshot.samples[sample]
                         .back_source_slots.size());
             ++surface) {
            if (!snapshot.samples[sample]
                     .back_source_slots[surface]) {
                continue;
            }
            back_source_checkouts[sample][surface] =
                std::make_unique<CheckedOutSmartLayerPixels>();
            error =
                back_source_checkouts[sample][surface]->Checkout(
                    in_data,
                    extra,
                    SmartBackSourceCheckoutId(sample, surface));
            if (error != PF_Err_NONE) {
                return error;
            }
            back_source_worlds[sample][surface] =
                back_source_checkouts[sample][surface]->World();
        }
    }

    PF_EffectWorld* output = nullptr;
    PF_Err error =
        extra->cb->checkout_output(in_data->effect_ref, &output);
    if (error != PF_Err_NONE || !output) {
        return error != PF_Err_NONE ? error : PF_Err_BAD_CALLBACK_PARAM;
    }

    AEFX_SuiteScoper<PF_WorldSuite2> world_suite(
        in_data,
        kPFWorldSuite,
        kPFWorldSuiteVersion2,
        out_data);
    PF_PixelFormat format = PF_PixelFormat_INVALID;
    error = world_suite->PF_GetPixelFormat(output, &format);
    if (error != PF_Err_NONE) {
        return error;
    }
    switch (format) {
        case PF_PixelFormat_ARGB128:
            return RenderMotionSamples<PF_PixelFloat>(
                snapshot,
                input_worlds,
                source_worlds,
                back_source_worlds,
                *output);
        case PF_PixelFormat_ARGB64:
            return RenderMotionSamples<PF_Pixel16>(
                snapshot,
                input_worlds,
                source_worlds,
                back_source_worlds,
                *output);
        case PF_PixelFormat_ARGB32:
            return RenderMotionSamples<PF_Pixel8>(
                snapshot,
                input_worlds,
                source_worlds,
                back_source_worlds,
                *output);
        default:
            return PF_Err_BAD_CALLBACK_PARAM;
    }
}
