#include "SurfaceLabGeometry.h"

#include <algorithm>
#include <cmath>

// Bicubic surface evaluation and deformation, extracted verbatim from
// SurfaceLab.cpp. AE-free; depends only on the scene model. EvaluatePatch and
// ApplySurfaceDeform are exported (declared in the header); the remaining
// helpers are used only here.

Point2 ApplyAffine2D(const Affine2D& transform, Point2 point) {
    return {
        transform.xx * point.x + transform.xy * point.y + transform.tx,
        transform.yx * point.x + transform.yy * point.y + transform.ty};
}

bool TryInvertAffine2D(
    const Affine2D& transform,
    Affine2D& inverse) {
    constexpr double kMinimumDeterminant = 1.0e-12;
    const double determinant =
        transform.xx * transform.yy - transform.xy * transform.yx;
    if (!std::isfinite(determinant) ||
        std::abs(determinant) <= kMinimumDeterminant) {
        return false;
    }
    const double inverse_determinant = 1.0 / determinant;
    inverse.xx = transform.yy * inverse_determinant;
    inverse.xy = -transform.xy * inverse_determinant;
    inverse.yx = -transform.yx * inverse_determinant;
    inverse.yy = transform.xx * inverse_determinant;
    inverse.tx =
        -(inverse.xx * transform.tx + inverse.xy * transform.ty);
    inverse.ty =
        -(inverse.yx * transform.tx + inverse.yy * transform.ty);
    return std::isfinite(inverse.xx) &&
           std::isfinite(inverse.xy) &&
           std::isfinite(inverse.yx) &&
           std::isfinite(inverse.yy) &&
           std::isfinite(inverse.tx) &&
           std::isfinite(inverse.ty);
}

Point3 ApplySceneVectorTransform(
    Point3 vector,
    const SceneCoordinateTransform& transform) {
    vector.x *= transform.scale.x;
    vector.y *= transform.scale.y;
    vector.z *= transform.scale.z;
    return RotatePoint(
        vector,
        0.0,
        0.0,
        0.0,
        transform.rotation_radians.x,
        transform.rotation_radians.y,
        transform.rotation_radians.z);
}

Point3 ApplyScenePointTransform(
    Point3 point,
    const SceneCoordinateTransform& transform) {
    const Point3 relative{
        point.x - transform.pivot.x,
        point.y - transform.pivot.y,
        point.z - transform.pivot.z};
    const Point3 transformed =
        ApplySceneVectorTransform(relative, transform);
    return {
        transform.position.x + transformed.x,
        transform.position.y + transformed.y,
        transform.position.z + transformed.z};
}

Point3 ApplySceneNormalTransform(
    Point3 normal,
    const SceneCoordinateTransform& transform) {
    constexpr double kMinimumScale = 1.0e-10;
    const auto safe_inverse = [](double value) {
        return std::abs(value) > kMinimumScale ? 1.0 / value : 0.0;
    };
    normal.x *= safe_inverse(transform.scale.x);
    normal.y *= safe_inverse(transform.scale.y);
    normal.z *= safe_inverse(transform.scale.z);
    return Normalize(RotatePoint(
        normal,
        0.0,
        0.0,
        0.0,
        transform.rotation_radians.x,
        transform.rotation_radians.y,
        transform.rotation_radians.z));
}

bool TryInverseScenePointTransform(
    Point3 point,
    const SceneCoordinateTransform& transform,
    Point3& untransformed) {
    constexpr double kMinimumScale = 1.0e-10;
    if (std::abs(transform.scale.x) <= kMinimumScale ||
        std::abs(transform.scale.y) <= kMinimumScale ||
        std::abs(transform.scale.z) <= kMinimumScale) {
        return false;
    }
    Point3 relative{
        point.x - transform.position.x,
        point.y - transform.position.y,
        point.z - transform.position.z};
    relative = InverseRotateVector(
        relative,
        transform.rotation_radians.x,
        transform.rotation_radians.y,
        transform.rotation_radians.z);
    untransformed = {
        transform.pivot.x + relative.x / transform.scale.x,
        transform.pivot.y + relative.y / transform.scale.y,
        transform.pivot.z + relative.z / transform.scale.z};
    return std::isfinite(untransformed.x) &&
           std::isfinite(untransformed.y) &&
           std::isfinite(untransformed.z);
}

SurfaceCoordinateTransform BuildSurfaceCoordinateTransform(
    const SurfaceData& surface,
    Point3 legacy_pivot,
    Point3 render_scale) {
    constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;
    SurfaceCoordinateTransform transform;
    if (surface.transform_mode != 0) {
        transform.pivot = {
            static_cast<double>(surface.position_x) * render_scale.x,
            static_cast<double>(surface.position_y) * render_scale.y,
            static_cast<double>(surface.position_z) * render_scale.z};
        transform.scale = {
            static_cast<double>(surface.scale_x) / 100.0,
            static_cast<double>(surface.scale_y) / 100.0,
            static_cast<double>(surface.scale_z) / 100.0};
    } else {
        transform.pivot = legacy_pivot;
    }
    transform.rotation_radians = {
        static_cast<double>(surface.rotation_x) * kDegreesToRadians,
        static_cast<double>(surface.rotation_y) * kDegreesToRadians,
        static_cast<double>(surface.rotation_z) * kDegreesToRadians};

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
    transform.rotation_origin = {
        transform.pivot.x +
            (origin_x_percent / 100.0 - 0.5) *
                static_cast<double>(surface.size_x) * render_scale.x *
                transform.scale.x,
        transform.pivot.y +
            (origin_y_percent / 100.0 - 0.5) *
                static_cast<double>(surface.size_y) * render_scale.y *
                transform.scale.y,
        transform.pivot.z};
    return transform;
}

Point3 SurfaceCageToLocal(
    Point3 cage_point,
    const SurfaceCoordinateTransform& transform) {
    return {
        cage_point.x - transform.pivot.x,
        cage_point.y - transform.pivot.y,
        cage_point.z - transform.pivot.z};
}

Point3 SurfaceLocalToCage(
    Point3 local_point,
    const SurfaceCoordinateTransform& transform) {
    return {
        local_point.x + transform.pivot.x,
        local_point.y + transform.pivot.y,
        local_point.z + transform.pivot.z};
}

Point3 ScaleSurfaceCagePoint(
    Point3 cage_point,
    const SurfaceCoordinateTransform& transform) {
    return {
        transform.pivot.x +
            (cage_point.x - transform.pivot.x) * transform.scale.x,
        transform.pivot.y +
            (cage_point.y - transform.pivot.y) * transform.scale.y,
        transform.pivot.z +
            (cage_point.z - transform.pivot.z) * transform.scale.z};
}

Point3 RotateSurfaceWorldPoint(
    Point3 scaled_point,
    const SurfaceCoordinateTransform& transform) {
    return RotatePoint(
        scaled_point,
        transform.rotation_origin.x,
        transform.rotation_origin.y,
        transform.rotation_origin.z,
        transform.rotation_radians.x,
        transform.rotation_radians.y,
        transform.rotation_radians.z);
}

Point3 SurfaceCageToWorld(
    Point3 cage_point,
    const SurfaceCoordinateTransform& transform) {
    return RotateSurfaceWorldPoint(
        ScaleSurfaceCagePoint(cage_point, transform),
        transform);
}

bool TrySurfaceWorldToCage(
    Point3 world_point,
    const SurfaceCoordinateTransform& transform,
    Point3& cage_point) {
    constexpr double kMinimumScale = 1.0e-10;
    if (std::abs(transform.scale.x) <= kMinimumScale ||
        std::abs(transform.scale.y) <= kMinimumScale ||
        std::abs(transform.scale.z) <= kMinimumScale) {
        return false;
    }
    Point3 relative{
        world_point.x - transform.rotation_origin.x,
        world_point.y - transform.rotation_origin.y,
        world_point.z - transform.rotation_origin.z};
    relative = InverseRotateVector(
        relative,
        transform.rotation_radians.x,
        transform.rotation_radians.y,
        transform.rotation_radians.z);
    const Point3 scaled_point{
        relative.x + transform.rotation_origin.x,
        relative.y + transform.rotation_origin.y,
        relative.z + transform.rotation_origin.z};
    cage_point = {
        transform.pivot.x +
            (scaled_point.x - transform.pivot.x) / transform.scale.x,
        transform.pivot.y +
            (scaled_point.y - transform.pivot.y) / transform.scale.y,
        transform.pivot.z +
            (scaled_point.z - transform.pivot.z) / transform.scale.z};
    return true;
}

double Bernstein(int index, double t) {
    const double s = 1.0 - t;
    switch (index) {
        case 0: return s * s * s;
        case 1: return 3.0 * t * s * s;
        case 2: return 3.0 * t * t * s;
        default: return t * t * t;
    }
}

Point3 EvaluatePatch(const std::array<Point3, 16>& points, double u, double v) {
    Point3 result;
    for (int row = 0; row < 4; ++row) {
        const double bv = Bernstein(row, v);
        for (int column = 0; column < 4; ++column) {
            const double weight = Bernstein(column, u) * bv;
            const Point3& point = points[static_cast<size_t>(row * 4 + column)];
            result.x += point.x * weight;
            result.y += point.y * weight;
            result.z += point.z * weight;
        }
    }
    return result;
}

void ApplyArcDeform(
    double& coordinate,
    double& depth,
    double neutral_coordinate,
    double center,
    double depth_center,
    double extent,
    double angle_radians) {
    if (std::abs(angle_radians) <= 1.0e-7 || extent <= 1.0e-7) {
        return;
    }
    const double radius = extent / angle_radians;
    const double phase = (neutral_coordinate - center) / radius;
    const double sine = std::sin(phase);
    const double cosine = std::cos(phase);
    const double base_coordinate = center + sine * radius;
    const double base_depth = (1.0 - cosine) * radius;
    const double local_coordinate = coordinate - neutral_coordinate;
    const double local_depth = depth - depth_center;
    coordinate = base_coordinate + cosine * local_coordinate - sine * local_depth;
    depth = depth_center + base_depth + sine * local_coordinate + cosine * local_depth;
}

void ApplyRollDeform(
    double& coordinate,
    double& depth,
    double neutral_coordinate,
    double minimum,
    double maximum,
    double depth_center,
    bool reverse,
    double roll_length_percent,
    double angle_radians) {
    const double extent = maximum - minimum;
    const double roll_extent = extent *
                               std::clamp(roll_length_percent / 100.0, 0.0, 1.0);
    if (roll_extent <= 1.0e-7 || std::abs(angle_radians) <= 1.0e-7) {
        return;
    }
    const double oriented =
        reverse ? maximum - neutral_coordinate : neutral_coordinate - minimum;
    const double start = extent - roll_extent;
    if (oriented <= start) {
        return;
    }
    const double distance = oriented - start;
    const double radius = roll_extent / angle_radians;
    const double phase = distance / radius;
    const double sine = std::sin(phase);
    const double cosine = std::cos(phase);
    const double rolled = start + std::sin(phase) * radius;
    const double base_coordinate =
        reverse ? maximum - rolled : minimum + rolled;
    const double base_depth = (1.0 - cosine) * radius;
    const double local_coordinate = coordinate - neutral_coordinate;
    const double local_depth = depth - depth_center;
    const double orientation = reverse ? -1.0 : 1.0;
    coordinate = base_coordinate + cosine * local_coordinate -
                 orientation * sine * local_depth;
    depth = depth_center + base_depth +
            orientation * sine * local_coordinate + cosine * local_depth;
}

Point3 CornerCurlDisplacement(
    const CornerCurlData& curl,
    std::uint32_t corner,
    double u,
    double v,
    double extent_x,
    double extent_y) {
    constexpr double kPi = 3.14159265358979323846;
    const double maximum_angle = std::clamp(
                                     std::abs(static_cast<double>(curl.amount)) /
                                         100.0,
                                     0.0,
                                     1.0) *
                                 kPi;
    const double minimum_extent = std::min(extent_x, extent_y);
    const double curl_length = minimum_extent *
                               std::clamp(
                                   static_cast<double>(curl.length) / 100.0,
                                   0.0,
                                   1.5);
    const double requested_radius = minimum_extent *
                                    std::clamp(
                                        static_cast<double>(curl.radius) / 100.0,
                                        0.001,
                                        1.0);
    if (maximum_angle <= 1.0e-7 ||
        curl_length <= 1.0e-7 ||
        requested_radius <= 1.0e-7) {
        return {};
    }

    constexpr double kDegreesToRadians = kPi / 180.0;
    const double direction = std::clamp(
                                 static_cast<double>(curl.direction),
                                 0.0,
                                 90.0) *
                             kDegreesToRadians;
    const double horizontal_sign =
        corner == kCornerTopRight || corner == kCornerBottomRight ? -1.0 : 1.0;
    const double vertical_sign =
        corner == kCornerBottomRight || corner == kCornerBottomLeft ? -1.0 : 1.0;
    const double direction_x = horizontal_sign * std::cos(direction);
    const double direction_y = vertical_sign * std::sin(direction);
    const double inward_x =
        (horizontal_sign > 0.0 ? u : 1.0 - u) * extent_x;
    const double inward_y =
        (vertical_sign > 0.0 ? v : 1.0 - v) * extent_y;
    const double inward_distance =
        inward_x * std::cos(direction) + inward_y * std::sin(direction);
    if (inward_distance < -1.0e-6 || inward_distance >= curl_length) {
        return {};
    }

    const double distance_from_fold = curl_length - inward_distance;
    const double bend_length = std::min(
        curl_length,
        requested_radius * maximum_angle);
    const double radius = bend_length / maximum_angle;
    double curled_distance = inward_distance;
    double depth = 0.0;
    if (distance_from_fold <= bend_length) {
        const double phase = distance_from_fold / radius;
        curled_distance = curl_length - std::sin(phase) * radius;
        depth = (1.0 - std::cos(phase)) * radius;
    } else {
        const double straight_length = distance_from_fold - bend_length;
        curled_distance =
            curl_length - std::sin(maximum_angle) * radius -
            std::cos(maximum_angle) * straight_length;
        depth =
            (1.0 - std::cos(maximum_angle)) * radius +
            std::sin(maximum_angle) * straight_length;
    }

    const double displacement = curled_distance - inward_distance;
    const double depth_sign = curl.amount < 0.0F ? -1.0 : 1.0;
    return {
        direction_x * displacement,
        direction_y * displacement,
        depth_sign * depth};
}

double EdgeTwistWeight(double distance_from_edge, double falloff_percent) {
    const double range = std::clamp(falloff_percent / 100.0, 0.01, 1.0);
    if (distance_from_edge >= range) {
        return 0.0;
    }
    const double value = 1.0 - std::clamp(distance_from_edge / range, 0.0, 1.0);
    return value * value * (3.0 - 2.0 * value);
}

void ApplyEdgeTwist(
    Point3& point,
    const SurfaceData& surface,
    double u,
    double v,
    double pivot_x,
    double pivot_y,
    double pivot_z) {
    constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;
    const EdgeTwistData& left = surface.edge_twists[kTwistEdgeLeft - 1];
    const EdgeTwistData& right = surface.edge_twists[kTwistEdgeRight - 1];
    const EdgeTwistData& top = surface.edge_twists[kTwistEdgeTop - 1];
    const EdgeTwistData& bottom = surface.edge_twists[kTwistEdgeBottom - 1];
    const double rotation_x =
        (static_cast<double>(left.angle) * EdgeTwistWeight(u, left.falloff) +
         static_cast<double>(right.angle) *
             EdgeTwistWeight(1.0 - u, right.falloff)) *
        kDegreesToRadians;
    const double rotation_y =
        (static_cast<double>(top.angle) * EdgeTwistWeight(v, top.falloff) +
         static_cast<double>(bottom.angle) *
             EdgeTwistWeight(1.0 - v, bottom.falloff)) *
        kDegreesToRadians;
    if (std::abs(rotation_x) <= 1.0e-7 && std::abs(rotation_y) <= 1.0e-7) {
        return;
    }

    point.x -= pivot_x;
    point.y -= pivot_y;
    point.z -= pivot_z;
    const double sin_x = std::sin(rotation_x);
    const double cos_x = std::cos(rotation_x);
    const double rotated_y = point.y * cos_x - point.z * sin_x;
    const double rotated_z_x = point.y * sin_x + point.z * cos_x;
    point.y = rotated_y;
    point.z = rotated_z_x;
    const double sin_y = std::sin(rotation_y);
    const double cos_y = std::cos(rotation_y);
    const double rotated_x = point.x * cos_y + point.z * sin_y;
    const double rotated_z_y = -point.x * sin_y + point.z * cos_y;
    point.x = rotated_x + pivot_x;
    point.y += pivot_y;
    point.z = rotated_z_y + pivot_z;
}

void ApplySurfaceDeform(
    Point3& point,
    const SurfaceData& surface,
    double u,
    double v,
    double pivot_x,
    double pivot_y,
    double pivot_z,
    double extent_x,
    double extent_y) {
    constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;
    const double minimum_x = pivot_x - extent_x * 0.5;
    const double maximum_x = pivot_x + extent_x * 0.5;
    const double minimum_y = pivot_y - extent_y * 0.5;
    const double maximum_y = pivot_y + extent_y * 0.5;
    const double neutral_x =
        minimum_x + std::clamp(u, 0.0, 1.0) * extent_x;
    const double neutral_y =
        minimum_y + std::clamp(v, 0.0, 1.0) * extent_y;
    const double roll_angle = static_cast<double>(surface.roll_angle) *
                              kDegreesToRadians;
    Point3 combined_corner_displacement{};
    for (std::uint32_t corner = kCornerTopLeft;
         corner <= kCornerBottomLeft;
         ++corner) {
        const Point3 displacement = CornerCurlDisplacement(
            surface.corner_curls[corner - 1],
            corner,
            u,
            v,
            extent_x,
            extent_y);
        combined_corner_displacement.x += displacement.x;
        combined_corner_displacement.y += displacement.y;
        combined_corner_displacement.z += displacement.z;
    }
    point.x += combined_corner_displacement.x;
    point.y += combined_corner_displacement.y;
    point.z += combined_corner_displacement.z;
    ApplyEdgeTwist(
        point,
        surface,
        u,
        v,
        pivot_x,
        pivot_y,
        pivot_z);
    if (surface.roll_edge == kRollEdgeRight ||
        surface.roll_edge == kRollEdgeLeft) {
        ApplyRollDeform(
            point.x,
            point.z,
            neutral_x,
            minimum_x,
            maximum_x,
            pivot_z,
            surface.roll_edge == kRollEdgeLeft,
            surface.roll_length,
            roll_angle);
    } else if (surface.roll_edge == kRollEdgeBottom ||
               surface.roll_edge == kRollEdgeTop) {
        ApplyRollDeform(
            point.y,
            point.z,
            neutral_y,
            minimum_y,
            maximum_y,
            pivot_z,
            surface.roll_edge == kRollEdgeTop,
            surface.roll_length,
            roll_angle);
    }
    ApplyArcDeform(
        point.x,
        point.z,
        neutral_x,
        pivot_x,
        pivot_z,
        extent_x,
        static_cast<double>(surface.bend_x) * kDegreesToRadians);
    ApplyArcDeform(
        point.y,
        point.z,
        neutral_y,
        pivot_y,
        pivot_z,
        extent_y,
        static_cast<double>(surface.bend_y) * kDegreesToRadians);
}
