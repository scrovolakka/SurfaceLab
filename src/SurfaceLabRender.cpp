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

}  // namespace

Vertex ProjectVertex(
    const Point3& point,
    Point3 normal,
    double u,
    double v,
    const CameraState& camera) {
    Point3 camera_point{
        point.x - camera.position.x,
        point.y - camera.position.y,
        point.z - camera.position.z};
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
    vertex.world_position = point;
    vertex.normal = Normalize(normal);
    vertex.visible = camera_depth > 1.0;
    if (vertex.visible) {
        vertex.inverse_depth = 1.0 / camera_depth;
        if (camera.perspective) {
            const double projection_scale = camera.focal_distance * vertex.inverse_depth;
            vertex.x = camera.center_x + camera_point.x * projection_scale +
                       camera.output_offset_x;
            vertex.y = camera.center_y + camera_point.y * projection_scale +
                       camera.output_offset_y;
        } else {
            vertex.x = camera.center_x + camera_point.x + camera.output_offset_x;
            vertex.y = camera.center_y + camera_point.y + camera.output_offset_y;
        }
    }
    return vertex;
}

namespace {
double Edge(const Vertex& a, const Vertex& b, double x, double y) {
    return (x - a.x) * (b.y - a.y) - (y - a.y) * (b.x - a.x);
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
Pixel ApplyOpacity(Pixel pixel, float opacity_percent) {
    const double multiplier = std::clamp(
        static_cast<double>(opacity_percent) / 100.0,
        0.0,
        1.0);
    pixel.alpha = static_cast<decltype(pixel.alpha)>(
        std::lround(static_cast<double>(pixel.alpha) * multiplier));
    pixel.red = static_cast<decltype(pixel.red)>(
        std::lround(static_cast<double>(pixel.red) * multiplier));
    pixel.green = static_cast<decltype(pixel.green)>(
        std::lround(static_cast<double>(pixel.green) * multiplier));
    pixel.blue = static_cast<decltype(pixel.blue)>(
        std::lround(static_cast<double>(pixel.blue) * multiplier));
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
        const double maximum = std::is_same_v<Pixel, PF_Pixel16>
                                   ? static_cast<double>(PF_MAX_CHAN16)
                                   : static_cast<double>(PF_MAX_CHAN8);
        return std::lround(std::clamp(value, 0.0, maximum));
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
            reinterpret_cast<const A_u_char*>(input.data) + y * input.rowbytes);
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
        return std::lround(top * (1.0 - ty) + bottom * ty);
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
            reinterpret_cast<A_u_char*>(output.data) + y * output.rowbytes);
        std::fill(row, row + output.width, Pixel{});
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
    const LightingState& lighting,
    TextureFace texture_face) {
    if (!a.visible || !b.visible || !c.visible) {
        return;
    }
    const double area = Edge(a, b, c.x, c.y);
    if (std::abs(area) < 1.0e-8) {
        return;
    }

    const int min_x = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
    const int max_x = std::min(
        static_cast<int>(output.width - 1),
        static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
    const int min_y = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
    const int max_y = std::min(
        static_cast<int>(output.height - 1),
        static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));

    for (int y = min_y; y <= max_y; ++y) {
        auto* output_row = reinterpret_cast<Pixel*>(
            reinterpret_cast<A_u_char*>(output.data) + y * output.rowbytes);
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

template <typename Pixel>
void DrawLine(PF_LayerDef& output, Point2 start, Point2 end) {
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
    if constexpr (std::is_same_v<Pixel, PF_Pixel16>) {
        line_pixel.alpha = PF_MAX_CHAN16;
        line_pixel.red = PF_MAX_CHAN16;
        line_pixel.green = PF_MAX_CHAN16;
        line_pixel.blue = PF_MAX_CHAN16;
    } else {
        line_pixel.alpha = PF_MAX_CHAN8;
        line_pixel.red = PF_MAX_CHAN8;
        line_pixel.green = PF_MAX_CHAN8;
        line_pixel.blue = PF_MAX_CHAN8;
    }

    while (true) {
        if (x0 >= 0 && x0 < output.width && y0 >= 0 && y0 < output.height) {
            auto* row = reinterpret_cast<Pixel*>(
                reinterpret_cast<A_u_char*>(output.data) + y0 * output.rowbytes);
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
    constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;
    state.rotation_x = surface.rotation_x * kDegreesToRadians;
    state.rotation_y = surface.rotation_y * kDegreesToRadians;
    state.rotation_z = surface.rotation_z * kDegreesToRadians;
    state.pivot_x = surface.transform_mode != 0
                        ? static_cast<double>(surface.position_x) * render_scale_x
                        : camera.center_x;
    state.pivot_y = surface.transform_mode != 0
                        ? static_cast<double>(surface.position_y) * render_scale_y
                        : camera.center_y;
    state.pivot_z = surface.transform_mode != 0
                        ? static_cast<double>(surface.position_z) * render_scale_z
                        : 0.0;
    state.scale_x = surface.transform_mode != 0
                        ? static_cast<double>(surface.scale_x) / 100.0
                        : 1.0;
    state.scale_y = surface.transform_mode != 0
                        ? static_cast<double>(surface.scale_y) / 100.0
                        : 1.0;
    state.scale_z = surface.transform_mode != 0
                        ? static_cast<double>(surface.scale_z) / 100.0
                        : 1.0;
    double origin_x_percent = 50.0;
    double origin_y_percent = 50.0;
    switch (surface.rotation_origin_mode) {
        case kRotationOriginLeftEdge:
            origin_x_percent = 0.0;
            break;
        case kRotationOriginRightEdge:
            origin_x_percent = 100.0;
            break;
        case kRotationOriginTopEdge:
            origin_y_percent = 0.0;
            break;
        case kRotationOriginBottomEdge:
            origin_y_percent = 100.0;
            break;
        case kRotationOriginCustom:
            origin_x_percent = static_cast<double>(surface.rotation_origin_x);
            origin_y_percent = static_cast<double>(surface.rotation_origin_y);
            break;
        default:
            break;
    }
    state.rotation_origin_x =
        state.pivot_x + (origin_x_percent / 100.0 - 0.5) *
                            static_cast<double>(surface.size_x) *
                            render_scale_x * state.scale_x;
    state.rotation_origin_y =
        state.pivot_y + (origin_y_percent / 100.0 - 0.5) *
                            static_cast<double>(surface.size_y) *
                            render_scale_y * state.scale_y;
    state.rotation_origin_z = state.pivot_z;
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
    point.x = state.pivot_x + (point.x - state.pivot_x) * state.scale_x;
    point.y = state.pivot_y + (point.y - state.pivot_y) * state.scale_y;
    point.z = state.pivot_z + (point.z - state.pivot_z) * state.scale_z;
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
    return RotatePoint(
        point,
        state.rotation_origin_x,
        state.rotation_origin_y,
        state.rotation_origin_z,
        state.rotation_x,
        state.rotation_y,
        state.rotation_z);
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
    bool wireframe) {
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
            lighting,
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
            lighting,
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

    if (wireframe) {
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

}  // namespace

PF_Err FrameSetup(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_ParamDef* params[]) {
    const PF_LayerDef& input = params[kParamInput]->u.ld;
    if (input.width <= 0 || input.height <= 0) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }

    const A_long mode = std::clamp<A_long>(
        params[kParamOutputBoundsMode]->u.pd.value,
        kOutputBoundsSource,
        kOutputBoundsFixed);
    const double scale_x = static_cast<double>(in_data->downsample_x.num) /
                           std::max<A_u_long>(1U, in_data->downsample_x.den);
    const double scale_y = static_cast<double>(in_data->downsample_y.num) /
                           std::max<A_u_long>(1U, in_data->downsample_y.den);
    const double scale_z = (scale_x + scale_y) * 0.5;
    const A_long padding_x = static_cast<A_long>(std::lround(
        std::max<A_long>(0, params[kParamOutputPaddingX]->u.sd.value) * scale_x));
    const A_long padding_y = static_cast<A_long>(std::lround(
        std::max<A_long>(0, params[kParamOutputPaddingY]->u.sd.value) * scale_y));

    A_long minimum_x = 0;
    A_long minimum_y = 0;
    A_long maximum_x = input.width;
    A_long maximum_y = input.height;

    if (mode == kOutputBoundsFixed) {
        LimitExpandedAxis(
            -static_cast<double>(padding_x),
            static_cast<double>(input.width + padding_x),
            input.width,
            PF_MAX_WORLD_WIDTH,
            minimum_x,
            maximum_x);
        LimitExpandedAxis(
            -static_cast<double>(padding_y),
            static_cast<double>(input.height + padding_y),
            input.height,
            PF_MAX_WORLD_HEIGHT,
            minimum_y,
            maximum_y);
    } else if (mode == kOutputBoundsAuto) {
        const int legacy_tessellation = std::clamp(
            static_cast<int>(params[kParamTessellation]->u.sd.value), 2, 32);
        const SceneData scene =
            ResolveSceneForFrame(in_data, params, input.width, input.height);
        CameraState camera = BuildCameraState(
            params,
            static_cast<double>(input.width) * 0.5,
            static_cast<double>(input.height) * 0.5,
            0.0,
            0.0,
            scale_x,
            scale_y,
            scale_z);
        if (params[kParamCameraSource]->u.pd.value ==
            kCameraSourceAfterEffects) {
            ResolveAfterEffectsCamera(
                in_data,
                static_cast<double>(input.width) * 0.5,
                static_cast<double>(input.height) * 0.5,
                0.0,
                0.0,
                scale_x,
                scale_y,
                scale_z,
                camera);
        }
        Bounds2D bounds{
            0.0,
            0.0,
            static_cast<double>(input.width),
            static_cast<double>(input.height)};
        for (std::uint32_t index = 0; index < scene.surface_count; ++index) {
            if (scene.surfaces[index].enabled != 0) {
                AccumulateSurfaceBounds(
                    bounds,
                    scene.surfaces[index],
                    legacy_tessellation,
                    camera,
                    scale_x,
                    scale_y,
                    scale_z);
            }
        }
        LimitExpandedAxis(
            bounds.minimum_x - padding_x,
            bounds.maximum_x + padding_x,
            input.width,
            PF_MAX_WORLD_WIDTH,
            minimum_x,
            maximum_x);
        LimitExpandedAxis(
            bounds.minimum_y - padding_y,
            bounds.maximum_y + padding_y,
            input.height,
            PF_MAX_WORLD_HEIGHT,
            minimum_y,
            maximum_y);
    }

    out_data->width = std::max<A_long>(1, maximum_x - minimum_x);
    out_data->height = std::max<A_long>(1, maximum_y - minimum_y);
    out_data->origin.h = -minimum_x;
    out_data->origin.v = -minimum_y;
    return PF_Err_NONE;
}

namespace {
template <typename Pixel>
PF_Err RenderSurface(PF_InData* in_data, PF_ParamDef* params[], PF_LayerDef* output) {
    const PF_LayerDef& input = params[kParamInput]->u.ld;
    if (!input.data || !output->data || input.width <= 0 || input.height <= 0) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }

    const double scale_x = static_cast<double>(in_data->downsample_x.num) /
                           static_cast<double>(in_data->downsample_x.den);
    const double scale_y = static_cast<double>(in_data->downsample_y.num) /
                           static_cast<double>(in_data->downsample_y.den);
    const double scale_z = (scale_x + scale_y) * 0.5;
    const bool wireframe = params[kParamWireframe]->u.bd.value != FALSE;
    constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;
    CameraState camera = BuildCameraState(
        params,
        static_cast<double>(input.width) * 0.5,
        static_cast<double>(input.height) * 0.5,
        static_cast<double>(in_data->output_origin_x),
        static_cast<double>(in_data->output_origin_y),
        scale_x,
        scale_y,
        scale_z);
    if (params[kParamCameraSource]->u.pd.value ==
        kCameraSourceAfterEffects) {
        ResolveAfterEffectsCamera(
            in_data,
            static_cast<double>(input.width) * 0.5,
            static_cast<double>(input.height) * 0.5,
            static_cast<double>(in_data->output_origin_x),
            static_cast<double>(in_data->output_origin_y),
            scale_x,
            scale_y,
            scale_z,
            camera);
    }

    LightingState lighting;
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
    const double light_rotation_x =
        FIX_2_FLOAT(params[kParamLightRotationX]->u.ad.value) * kDegreesToRadians;
    const double light_rotation_y =
        FIX_2_FLOAT(params[kParamLightRotationY]->u.ad.value) * kDegreesToRadians;
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
            scale_x,
            scale_y,
            scale_z,
            lighting);
    }
    lighting.camera_position = camera.position;
    const int legacy_tessellation = std::clamp(
        static_cast<int>(params[kParamTessellation]->u.sd.value), 2, 32);

    const SceneData scene =
        ResolveSceneForFrame(in_data, params, input.width, input.height);

    ClearWorld<Pixel>(*output);
    std::vector<float> depth_buffer(
        static_cast<size_t>(output->width) * static_cast<size_t>(output->height),
        -std::numeric_limits<float>::infinity());

    for (std::uint32_t index = 0; index < scene.surface_count; ++index) {
        const SurfaceData& surface = scene.surfaces[index];
        if (surface.enabled == 0) {
            continue;
        }

        PF_ParamDef front_checkout{};
        const PF_Err front_checkout_error = PF_CHECKOUT_PARAM(
            in_data,
            kParamSurfaceSourceLayer1 + static_cast<PF_ParamIndex>(surface.source_slot),
            in_data->current_time,
            in_data->time_step,
            in_data->time_scale,
            &front_checkout);
        if (front_checkout_error != PF_Err_NONE) {
            return front_checkout_error;
        }

        const std::uint32_t back_slot = surface.back_source_slot == 0
                                            ? surface.source_slot
                                            : surface.back_source_slot - 1;
        const bool separate_back_checkout = back_slot != surface.source_slot;
        PF_ParamDef back_checkout{};
        if (separate_back_checkout) {
            const PF_Err back_checkout_error = PF_CHECKOUT_PARAM(
                in_data,
                kParamSurfaceSourceLayer1 + static_cast<PF_ParamIndex>(back_slot),
                in_data->current_time,
                in_data->time_step,
                in_data->time_scale,
                &back_checkout);
            if (back_checkout_error != PF_Err_NONE) {
                PF_CHECKIN_PARAM(in_data, &front_checkout);
                return back_checkout_error;
            }
        }

        const PF_LayerDef& front_texture = front_checkout.u.ld.data
                                               ? front_checkout.u.ld
                                               : input;
        const PF_LayerDef& back_texture = separate_back_checkout
                                              ? (back_checkout.u.ld.data
                                                     ? back_checkout.u.ld
                                                     : input)
                                              : front_texture;
        RasterizeSurface<Pixel>(
            surface,
            front_texture,
            back_texture,
            *output,
            depth_buffer,
            legacy_tessellation,
            camera,
            lighting,
            scale_x,
            scale_y,
            scale_z,
            wireframe);

        if (separate_back_checkout) {
            const PF_Err back_checkin_error =
                PF_CHECKIN_PARAM(in_data, &back_checkout);
            if (back_checkin_error != PF_Err_NONE) {
                PF_CHECKIN_PARAM(in_data, &front_checkout);
                return back_checkin_error;
            }
        }
        const PF_Err front_checkin_error =
            PF_CHECKIN_PARAM(in_data, &front_checkout);
        if (front_checkin_error != PF_Err_NONE) {
            return front_checkin_error;
        }
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
