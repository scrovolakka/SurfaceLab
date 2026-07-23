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
#include <type_traits>
#include <vector>

#include "SurfaceLabRender.h"

namespace {

std::array<Point3, 16> ToControlPoints(
    const SurfaceData& surface,
    double scale_x,
    double scale_y,
    double scale_z) {
    std::array<Point3, 16> result{};
    for (int index = 0; index < 16; ++index) {
        result[static_cast<std::size_t>(index)] = {
            static_cast<double>(surface.control_points[index].x) * scale_x,
            static_cast<double>(surface.control_points[index].y) * scale_y,
            static_cast<double>(surface.control_points[index].z) * scale_z};
    }
    return result;
}

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
    const LightingState& lighting) {
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
        double spot_factor = 1.0;
        if (light.type != RenderLightType::Directional) {
            light_direction = Normalize({
                light.position.x - world_position.x,
                light.position.y - world_position.y,
                light.position.z - world_position.z});
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
        const Point3 half_vector = Normalize({
            light_direction.x + view_direction.x,
            light_direction.y + view_direction.y,
            light_direction.z + view_direction.z});
        const double specular_term =
            std::pow(
                std::max(0.0, Dot(normal, half_vector)),
                std::max(1.0, static_cast<double>(surface.shininess))) *
            spot_factor;
        const double diffuse_strength = light.intensity * diffuse_term;
        const double specular_strength = light.intensity * specular_term;
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
    const double alpha = static_cast<double>(pixel.alpha);
    const auto shade = [&](auto channel, double diffuse, double specular) {
        const double value =
            static_cast<double>(channel) * diffuse * diffuse_coefficient +
            alpha * specular * specular_coefficient;
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
                    lighting);
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
    state.control_points =
        ToControlPoints(surface, render_scale_x, render_scale_y, render_scale_z);
    state.coordinate_transform = BuildSurfaceCoordinateTransform(
        surface,
        {camera.center_x, camera.center_y, 0.0},
        {render_scale_x, render_scale_y, render_scale_z});
    const SurfaceCoordinateTransform& transform = state.coordinate_transform;
    state.rotation_x = transform.rotation_radians.x;
    state.rotation_y = transform.rotation_radians.y;
    state.rotation_z = transform.rotation_radians.z;
    state.pivot_x = transform.pivot.x;
    state.pivot_y = transform.pivot.y;
    state.pivot_z = transform.pivot.z;
    state.scale_x = transform.scale.x;
    state.scale_y = transform.scale.y;
    state.scale_z = transform.scale.z;
    state.rotation_origin_x = transform.rotation_origin.x;
    state.rotation_origin_y = transform.rotation_origin.y;
    state.rotation_origin_z = transform.rotation_origin.z;
    state.deform_extent_x = std::max(
        1.0e-6,
        static_cast<double>(surface.size_x) * render_scale_x *
            std::abs(state.scale_x));
    state.deform_extent_y = std::max(
        1.0e-6,
        static_cast<double>(surface.size_y) * render_scale_y *
            std::abs(state.scale_y));
    state.half_thickness =
        std::max(0.0, static_cast<double>(surface.thickness)) *
        render_scale_z * std::abs(state.scale_z) * 0.5;
    return state;
}

Point3 EvaluateTransformedPoint(
    const SurfaceData& surface,
    const SurfaceEvaluationState& state,
    double u,
    double v) {
    Point3 point = EvaluatePatch(state.control_points, u, v);
    point = ScaleSurfaceCagePoint(point, state.coordinate_transform);
    ApplySurfaceDeform(
        point,
        surface,
        u,
        v,
        state.pivot_x,
        state.pivot_y,
        state.pivot_z,
        state.deform_extent_x,
        state.deform_extent_y);
    return RotateSurfaceWorldPoint(point, state.coordinate_transform);
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
    const int divisions_x = static_cast<int>(ResolveDivisions(
        surface.divisions_x,
        static_cast<std::uint32_t>(legacy_tessellation)));
    const int divisions_y = static_cast<int>(ResolveDivisions(
        surface.divisions_y,
        static_cast<std::uint32_t>(legacy_tessellation)));
    const int stride = divisions_x + 1;
    const bool has_thickness = evaluation.half_thickness > 1.0e-6;
    std::array<Vertex, 33 * 33> front_vertices{};
    std::array<Vertex, 33 * 33> back_vertices{};

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

    const auto raster_grid = [&](const std::array<Vertex, 33 * 33>& vertices,
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
        const auto draw_grid = [&](const std::array<Vertex, 33 * 33>& vertices) {
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

CameraState BuildCameraState(
    PF_ParamDef* params[],
    double center_x,
    double center_y,
    double output_offset_x,
    double output_offset_y,
    double scale_x,
    double scale_y,
    double scale_z) {
    constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;
    const double camera_distance = std::max(
        1.0,
        static_cast<double>(params[kParamCameraDistance]->u.sd.value) * scale_z);
    CameraState camera;
    camera.center_x = center_x;
    camera.center_y = center_y;
    camera.output_offset_x = output_offset_x;
    camera.output_offset_y = output_offset_y;
    camera.focal_distance = camera_distance;
    camera.perspective = params[kParamPerspective]->u.bd.value != FALSE;
    camera.position = {
        center_x + params[kParamCameraOffsetX]->u.fs_d.value * scale_x,
        center_y + params[kParamCameraOffsetY]->u.fs_d.value * scale_y,
        -camera_distance + params[kParamCameraOffsetZ]->u.fs_d.value * scale_z};
    camera.rotation_x =
        FIX_2_FLOAT(params[kParamCameraRotationX]->u.ad.value) * kDegreesToRadians;
    camera.rotation_y =
        FIX_2_FLOAT(params[kParamCameraRotationY]->u.ad.value) * kDegreesToRadians;
    camera.rotation_z =
        FIX_2_FLOAT(params[kParamCameraRotationZ]->u.ad.value) * kDegreesToRadians;
    return camera;
}

SceneCoordinateTransform BuildSceneCoordinateTransform(
    PF_ParamDef* params[],
    double center_x,
    double center_y,
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
        position.x_value * scale_x,
        position.y_value * scale_y,
        position.z_value * scale_z};
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

bool ResolveAfterEffectsCamera(
    PF_InData* in_data,
    double center_x,
    double center_y,
    double output_offset_x,
    double output_offset_y,
    double scale_x,
    double scale_y,
    double scale_z,
    bool use_comp_world,
    CameraState& camera) {
    A_Time comp_time{};
    if (!ResolveCompTime(in_data, comp_time)) {
        return false;
    }
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    AEGP_LayerH camera_layer = nullptr;
    if (suites.PFInterfaceSuite1()->AEGP_GetEffectCamera(
            in_data->effect_ref,
            &comp_time,
            &camera_layer) != A_Err_NONE ||
        !camera_layer) {
        return false;
    }

    A_Matrix4 layer_to_world{};
    if (suites.LayerSuite5()->AEGP_GetLayerToWorldXform(
            camera_layer,
            &comp_time,
            &layer_to_world) != A_Err_NONE) {
        return false;
    }
    AEGP_StreamVal zoom{};
    if (suites.StreamSuite2()->AEGP_GetLayerStreamValue(
            camera_layer,
            AEGP_LayerStream_ZOOM,
            AEGP_LTimeMode_CompTime,
            &comp_time,
            FALSE,
            &zoom,
            nullptr) != A_Err_NONE ||
        !std::isfinite(zoom.one_d) || zoom.one_d <= 0.0) {
        return false;
    }

    camera = {};
    camera.position = ScaledMatrixPosition(
        layer_to_world, scale_x, scale_y, scale_z);
    camera.right = Normalize(MatrixRow(layer_to_world, 0));
    camera.down = Normalize(MatrixRow(layer_to_world, 1));
    camera.forward = Normalize(MatrixRow(layer_to_world, 2));
    camera.focal_distance = zoom.one_d * scale_z;
    camera.center_x = center_x;
    camera.center_y = center_y;
    camera.output_offset_x = output_offset_x;
    camera.output_offset_y = output_offset_y;
    camera.perspective = true;
    camera.use_basis = true;

    // In Composition World mode SurfaceLab points and 3D controller Nulls
    // share AE comp-world coordinates. The effect output is still a 2D layer,
    // so cancel that host layer's affine transform before AE composites it.
    if (use_comp_world) {
        AEGP_LayerH effect_layer = nullptr;
        AEGP_LayerFlags layer_flags = AEGP_LayerFlag_NONE;
        AEGP_CompH comp = nullptr;
        AEGP_ItemH comp_item = nullptr;
        A_long comp_width = 0;
        A_long comp_height = 0;
        A_Matrix4 effect_to_world{};
        if (suites.PFInterfaceSuite1()->AEGP_GetEffectLayer(
                in_data->effect_ref,
                &effect_layer) == A_Err_NONE &&
            effect_layer &&
            suites.LayerSuite5()->AEGP_GetLayerFlags(
                effect_layer,
                &layer_flags) == A_Err_NONE &&
            (layer_flags & AEGP_LayerFlag_LAYER_IS_3D) == 0 &&
            suites.LayerSuite5()->AEGP_GetLayerParentComp(
                effect_layer,
                &comp) == A_Err_NONE &&
            comp &&
            suites.CompSuite4()->AEGP_GetItemFromComp(
                comp,
                &comp_item) == A_Err_NONE &&
            comp_item &&
            suites.ItemSuite6()->AEGP_GetItemDimensions(
                comp_item,
                &comp_width,
                &comp_height) == A_Err_NONE &&
            comp_width > 0 &&
            comp_height > 0 &&
            suites.LayerSuite5()->AEGP_GetLayerToWorldXform(
                effect_layer,
                &comp_time,
                &effect_to_world) == A_Err_NONE) {
            const double safe_scale_x = std::max(1.0e-12, scale_x);
            const double safe_scale_y = std::max(1.0e-12, scale_y);
            const Affine2D output_to_comp{
                effect_to_world.mat[0][0],
                effect_to_world.mat[1][0] * safe_scale_x / safe_scale_y,
                effect_to_world.mat[0][1] * safe_scale_y / safe_scale_x,
                effect_to_world.mat[1][1],
                effect_to_world.mat[3][0] * scale_x,
                effect_to_world.mat[3][1] * scale_y};
            Affine2D comp_to_output;
            if (TryInvertAffine2D(output_to_comp, comp_to_output)) {
                camera.center_x =
                    static_cast<double>(comp_width) * scale_x * 0.5;
                camera.center_y =
                    static_cast<double>(comp_height) * scale_y * 0.5;
                camera.comp_to_output = comp_to_output;
                camera.use_comp_to_output = true;
            }
        }
    }
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
    PF_ParamDef* params[],
    A_long input_width,
    A_long input_height,
    const SceneData& scene,
    const CameraState& camera,
    double scale_x,
    double scale_y,
    double scale_z) {
    const A_long mode = std::clamp<A_long>(
        params[kParamOutputBoundsMode]->u.pd.value,
        kOutputBoundsSource,
        kOutputBoundsFixed);
    const A_long padding_x = static_cast<A_long>(std::lround(
        std::max<A_long>(0, params[kParamOutputPaddingX]->u.sd.value) *
        scale_x));
    const A_long padding_y = static_cast<A_long>(std::lround(
        std::max<A_long>(0, params[kParamOutputPaddingY]->u.sd.value) *
        scale_y));

    OutputBounds bounds{0, 0, input_width, input_height};
    if (mode == kOutputBoundsFixed) {
        LimitExpandedAxis(
            -static_cast<double>(padding_x),
            static_cast<double>(input_width + padding_x),
            input_width,
            PF_MAX_WORLD_WIDTH,
            bounds.minimum_x,
            bounds.maximum_x);
        LimitExpandedAxis(
            -static_cast<double>(padding_y),
            static_cast<double>(input_height + padding_y),
            input_height,
            PF_MAX_WORLD_HEIGHT,
            bounds.minimum_y,
            bounds.maximum_y);
    } else if (mode == kOutputBoundsAuto) {
        const int legacy_tessellation = std::clamp(
            static_cast<int>(params[kParamTessellation]->u.sd.value),
            static_cast<int>(kMinimumDivisions),
            static_cast<int>(kMaximumDivisions));
        Bounds2D projected_bounds{
            0.0,
            0.0,
            static_cast<double>(input_width),
            static_cast<double>(input_height)};
        for (std::uint32_t index = 0; index < scene.surface_count; ++index) {
            if (scene.surfaces[index].enabled != 0) {
                AccumulateSurfaceBounds(
                    projected_bounds,
                    scene.surfaces[index],
                    legacy_tessellation,
                    camera,
                    scale_x,
                    scale_y,
                    scale_z);
            }
        }
        LimitExpandedAxis(
            projected_bounds.minimum_x - padding_x,
            projected_bounds.maximum_x + padding_x,
            input_width,
            PF_MAX_WORLD_WIDTH,
            bounds.minimum_x,
            bounds.maximum_x);
        LimitExpandedAxis(
            projected_bounds.minimum_y - padding_y,
            projected_bounds.maximum_y + padding_y,
            input_height,
            PF_MAX_WORLD_HEIGHT,
            bounds.minimum_y,
            bounds.maximum_y);
    }
    return bounds;
}

}  // namespace

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
    CameraState camera = BuildCameraState(
        params,
        center_x,
        center_y,
        output_offset_x,
        output_offset_y,
        scale_x,
        scale_y,
        scale_z);
    if (params[kParamCameraSource]->u.pd.value ==
        kCameraSourceAfterEffects) {
        const bool use_comp_world =
            params[kParamCoordinateSpace]->u.pd.value ==
            kCoordinateSpaceCompWorld;
        ResolveAfterEffectsCamera(
            in_data,
            center_x,
            center_y,
            output_offset_x,
            output_offset_y,
            scale_x,
            scale_y,
            scale_z,
            use_comp_world,
            camera);
    }
    camera.scene_transform = BuildSceneCoordinateTransform(
        params,
        center_x,
        center_y,
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
    int legacy_tessellation{static_cast<int>(kDefaultDivisions)};
    double scale_x{1.0};
    double scale_y{1.0};
    double scale_z{1.0};
    bool wireframe{};
    A_long render_view{kRenderViewFinish};
    std::array<bool, kMaximumSurfaces> source_slots{};
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
    snapshot.wireframe = params[kParamWireframe]->u.bd.value != FALSE;
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
    lighting.enabled = params[kParamLightingEnabled]->u.bd.value != FALSE;
    lighting.backface_culling =
        params[kParamBackfaceCulling]->u.bd.value != FALSE;
    lighting.texture_filter = std::clamp<A_long>(
        params[kParamTextureFilter]->u.pd.value,
        kTextureFilterNearest,
        kTextureFilterBilinear);
    const double internal_intensity = std::clamp(
        params[kParamLightIntensity]->u.fs_d.value / 100.0,
        0.0,
        4.0);
    const double internal_ambient = std::clamp(
        params[kParamAmbientLight]->u.fs_d.value / 100.0,
        0.0,
        1.0);
    constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;
    const double light_rotation_x =
        FIX_2_FLOAT(params[kParamLightRotationX]->u.ad.value) *
        kDegreesToRadians;
    const double light_rotation_y =
        FIX_2_FLOAT(params[kParamLightRotationY]->u.ad.value) *
        kDegreesToRadians;
    RenderLight& internal_light = lighting.lights[0];
    internal_light.type = RenderLightType::Directional;
    internal_light.direction = Normalize(RotatePoint(
        {0.0, 0.0, -1.0},
        0.0,
        0.0,
        0.0,
        light_rotation_x,
        light_rotation_y,
        0.0));
    internal_light.intensity = internal_intensity;
    lighting.light_count = 1;
    lighting.ambient = {
        internal_ambient,
        internal_ambient,
        internal_ambient};
    if (lighting.enabled &&
        params[kParamLightSource]->u.pd.value ==
            kLightSourceAfterEffects) {
        ResolveAfterEffectsLights(
            in_data,
            snapshot.scale_x,
            snapshot.scale_y,
            snapshot.scale_z,
            lighting);
    }
    lighting.camera_position = snapshot.camera.position;
    snapshot.legacy_tessellation = std::clamp(
        static_cast<int>(params[kParamTessellation]->u.sd.value),
        static_cast<int>(kMinimumDivisions),
        static_cast<int>(kMaximumDivisions));
    snapshot.scene =
        ResolveSceneForFrame(in_data, params, input_width, input_height);
    if (snapshot.render_view == kRenderViewFinish) {
        for (std::uint32_t index = 0;
             index < snapshot.scene.surface_count;
             ++index) {
            const SurfaceData& surface = snapshot.scene.surfaces[index];
            if (surface.enabled == 0) {
                continue;
            }
            snapshot.source_slots[surface.source_slot] = true;
            const std::uint32_t back_slot = surface.back_source_slot == 0
                                                ? surface.source_slot
                                                : surface.back_source_slot - 1;
            snapshot.source_slots[back_slot] = true;
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
            kParamSurfaceSourceLayer1 +
            static_cast<PF_ParamIndex>(surface.source_slot));
        if (front_checkout_error != PF_Err_NONE) {
            return front_checkout_error;
        }

        const std::uint32_t back_slot = surface.back_source_slot == 0
                                            ? surface.source_slot
                                            : surface.back_source_slot - 1;
        const bool separate_back_checkout = back_slot != surface.source_slot;
        CheckedOutLayerParam back_checkout(in_data);
        if (separate_back_checkout) {
            const PF_Err back_checkout_error = back_checkout.Checkout(
                kParamSurfaceSourceLayer1 +
                static_cast<PF_ParamIndex>(back_slot));
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

constexpr A_long kSmartInputCheckoutId = 0;
constexpr A_long kSmartSourceCheckoutBase = 1;

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
    constexpr std::array<PF_ParamIndex, 31> kFrameParameters = {
        kParamScenePosition,
        kParamSceneRotationX,
        kParamSceneRotationY,
        kParamSceneRotationZ,
        kParamSceneScaleX,
        kParamSceneScaleY,
        kParamSceneScaleZ,
        kParamTessellation,
        kParamWireframe,
        kParamPerspective,
        kParamCameraDistance,
        kParamSceneData,
        kParamCameraSource,
        kParamCoordinateSpace,
        kParamCameraOffsetX,
        kParamCameraOffsetY,
        kParamCameraOffsetZ,
        kParamCameraRotationX,
        kParamCameraRotationY,
        kParamCameraRotationZ,
        kParamLightSource,
        kParamLightingEnabled,
        kParamLightRotationX,
        kParamLightRotationY,
        kParamLightIntensity,
        kParamAmbientLight,
        kParamRenderView,
        kParamTextureFilter,
        kParamBackfaceCulling,
        kParamOutputBoundsMode,
        kParamOutputPaddingX};
    for (PF_ParamIndex index : kFrameParameters) {
        const PF_Err error =
            CheckoutSmartParameter(in_data, parameters, index);
        if (error != PF_Err_NONE) {
            return error;
        }
    }
    PF_Err error = CheckoutSmartParameter(
        in_data,
        parameters,
        kParamOutputPaddingY);
    if (error != PF_Err_NONE) {
        return error;
    }

    std::array<bool, kMaximumSurfaces> animation_banks{};
    bool active_scene = false;
    const PF_Handle scene_handle =
        parameters.definitions[static_cast<std::size_t>(kParamSceneData)]
            .u.arb_d.value;
    if (scene_handle) {
        const auto* scene =
            static_cast<const SceneData*>(PF_LOCK_HANDLE(scene_handle));
        if (scene) {
            active_scene = IsValidScene(*scene) && scene->active != 0;
            if (active_scene) {
                for (std::uint32_t index = 0;
                     index < scene->surface_count;
                     ++index) {
                    animation_banks[scene->surfaces[index].animation_bank] =
                        true;
                }
            }
            PF_UNLOCK_HANDLE(scene_handle);
        }
    }
    if (!active_scene) {
        animation_banks[0] = true;
    }

    for (std::uint32_t bank = 0;
         bank < static_cast<std::uint32_t>(animation_banks.size());
         ++bank) {
        if (!animation_banks[bank]) {
            continue;
        }
        for (std::size_t property = 0;
             property < static_cast<std::size_t>(
                            kSurfaceAnimationPropertyCount);
             ++property) {
            error = CheckoutSmartParameter(
                in_data,
                parameters,
                AnimationBankParam(bank, property));
            if (error != PF_Err_NONE) {
                return error;
            }
        }
    }
    return PF_Err_NONE;
}

void DeleteSmartRenderSnapshot(void* data) {
    delete static_cast<RenderFrameSnapshot*>(data);
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
        const std::uint32_t back_slot = surface.back_source_slot == 0
                                            ? surface.source_slot
                                            : surface.back_source_slot - 1;
        const PF_LayerDef* front_world = source_worlds[surface.source_slot];
        const PF_LayerDef* back_world = source_worlds[back_slot];
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
    PF_CheckoutResult input_result{};
    PF_Err error = extra->cb->checkout_layer(
        in_data->effect_ref,
        kParamInput,
        kSmartInputCheckoutId,
        &texture_request,
        in_data->current_time,
        in_data->time_step,
        in_data->time_scale,
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
    SmartParameterSet parameters(in_data);
    error = CheckoutSmartRenderParameters(in_data, parameters);
    if (error != PF_Err_NONE) {
        return error;
    }

    auto snapshot = std::make_unique<RenderFrameSnapshot>(
        BuildRenderFrameSnapshot(
            in_data,
            parameters.pointers.data(),
            input_width,
            input_height,
            0.0,
            0.0));
    for (std::uint32_t slot = 0;
         slot < static_cast<std::uint32_t>(snapshot->source_slots.size());
         ++slot) {
        if (!snapshot->source_slots[slot]) {
            continue;
        }
        PF_CheckoutResult source_result{};
        error = extra->cb->checkout_layer(
            in_data->effect_ref,
            kParamSurfaceSourceLayer1 + static_cast<PF_ParamIndex>(slot),
            kSmartSourceCheckoutBase + static_cast<A_long>(slot),
            &texture_request,
            in_data->current_time,
            in_data->time_step,
            in_data->time_scale,
            &source_result);
        if (error != PF_Err_NONE) {
            return error;
        }
    }

    const OutputBounds bounds = ComputeOutputBounds(
        parameters.pointers.data(),
        input_width,
        input_height,
        snapshot->scene,
        snapshot->camera,
        snapshot->scale_x,
        snapshot->scale_y,
        snapshot->scale_z);
    const PF_LRect maximum_rect{
        bounds.minimum_x,
        bounds.minimum_y,
        bounds.maximum_x,
        bounds.maximum_y};
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
    const auto& snapshot = *static_cast<const RenderFrameSnapshot*>(
        extra->input->pre_render_data);

    CheckedOutSmartLayerPixels input_checkout;
    PF_Err error = input_checkout.Checkout(
        in_data,
        extra,
        kSmartInputCheckoutId);
    if (error != PF_Err_NONE || !input_checkout.World()) {
        return error != PF_Err_NONE ? error : PF_Err_BAD_CALLBACK_PARAM;
    }

    std::array<CheckedOutSmartLayerPixels, kMaximumSurfaces>
        source_checkouts;
    std::array<const PF_LayerDef*, kMaximumSurfaces> source_worlds{};
    for (std::uint32_t slot = 0;
         slot < static_cast<std::uint32_t>(snapshot.source_slots.size());
         ++slot) {
        if (!snapshot.source_slots[slot]) {
            continue;
        }
        error = source_checkouts[slot].Checkout(
            in_data,
            extra,
            kSmartSourceCheckoutBase + static_cast<A_long>(slot));
        if (error != PF_Err_NONE) {
            return error;
        }
        source_worlds[slot] = source_checkouts[slot].World();
    }

    PF_EffectWorld* output = nullptr;
    error = extra->cb->checkout_output(in_data->effect_ref, &output);
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
            return RenderSmartFrame<PF_PixelFloat>(
                snapshot,
                *input_checkout.World(),
                source_worlds,
                *output);
        case PF_PixelFormat_ARGB64:
            return RenderSmartFrame<PF_Pixel16>(
                snapshot,
                *input_checkout.World(),
                source_worlds,
                *output);
        case PF_PixelFormat_ARGB32:
            return RenderSmartFrame<PF_Pixel8>(
                snapshot,
                *input_checkout.World(),
                source_worlds,
                *output);
        default:
            return PF_Err_BAD_CALLBACK_PARAM;
    }
}
