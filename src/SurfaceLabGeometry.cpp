#include "SurfaceLabGeometry.h"

#include <algorithm>
#include <cmath>
#include <limits>

Point2 ApplyAffine2D(const Affine2D& transform, Point2 point) {
    return {
        transform.xx * point.x +
            transform.xy * point.y + transform.tx,
        transform.yx * point.x +
            transform.yy * point.y + transform.ty};
}

bool TryInvertAffine2D(
    const Affine2D& transform,
    Affine2D& inverse) {
    const double determinant =
        transform.xx * transform.yy -
        transform.xy * transform.yx;
    if (!std::isfinite(determinant) ||
        std::abs(determinant) <= 1.0e-12) {
        return false;
    }
    inverse.xx = transform.yy / determinant;
    inverse.xy = -transform.xy / determinant;
    inverse.yx = -transform.yx / determinant;
    inverse.yy = transform.xx / determinant;
    inverse.tx =
        -(inverse.xx * transform.tx + inverse.xy * transform.ty);
    inverse.ty =
        -(inverse.yx * transform.tx + inverse.yy * transform.ty);
    return true;
}

Point3 ApplyAffine3D(
    const Affine3D& transform,
    Point3 point) {
    return {
        point.x * transform.xx +
            point.y * transform.yx +
            point.z * transform.zx +
            transform.tx,
        point.x * transform.xy +
            point.y * transform.yy +
            point.z * transform.zy +
            transform.ty,
        point.x * transform.xz +
            point.y * transform.yz +
            point.z * transform.zz +
            transform.tz};
}

bool TryInvertAffine3D(
    const Affine3D& transform,
    Affine3D& inverse) {
    const double determinant =
        transform.xx * (
            transform.yy * transform.zz -
            transform.yz * transform.zy) -
        transform.xy * (
            transform.yx * transform.zz -
            transform.yz * transform.zx) +
        transform.xz * (
            transform.yx * transform.zy -
            transform.yy * transform.zx);
    if (!std::isfinite(determinant) ||
        std::abs(determinant) <= 1.0e-12) {
        return false;
    }
    const double reciprocal = 1.0 / determinant;
    inverse.xx =
        (transform.yy * transform.zz -
         transform.yz * transform.zy) * reciprocal;
    inverse.xy =
        (transform.xz * transform.zy -
         transform.xy * transform.zz) * reciprocal;
    inverse.xz =
        (transform.xy * transform.yz -
         transform.xz * transform.yy) * reciprocal;
    inverse.yx =
        (transform.yz * transform.zx -
         transform.yx * transform.zz) * reciprocal;
    inverse.yy =
        (transform.xx * transform.zz -
         transform.xz * transform.zx) * reciprocal;
    inverse.yz =
        (transform.xz * transform.yx -
         transform.xx * transform.yz) * reciprocal;
    inverse.zx =
        (transform.yx * transform.zy -
         transform.yy * transform.zx) * reciprocal;
    inverse.zy =
        (transform.xy * transform.zx -
         transform.xx * transform.zy) * reciprocal;
    inverse.zz =
        (transform.xx * transform.yy -
         transform.xy * transform.yx) * reciprocal;
    inverse.tx = -(
        transform.tx * inverse.xx +
        transform.ty * inverse.yx +
        transform.tz * inverse.zx);
    inverse.ty = -(
        transform.tx * inverse.xy +
        transform.ty * inverse.yy +
        transform.tz * inverse.zy);
    inverse.tz = -(
        transform.tx * inverse.xz +
        transform.ty * inverse.yz +
        transform.tz * inverse.zz);
    const double values[] = {
        inverse.xx, inverse.xy, inverse.xz,
        inverse.yx, inverse.yy, inverse.yz,
        inverse.zx, inverse.zy, inverse.zz,
        inverse.tx, inverse.ty, inverse.tz};
    for (double value : values) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return true;
}

bool BuildAffineDeltaTransform(
    const Affine3D& bind_transform,
    const Affine3D& current_transform,
    Affine3D& delta_transform) {
    Affine3D inverse_bind{};
    if (!TryInvertAffine3D(bind_transform, inverse_bind)) {
        return false;
    }
    const auto map = [&](Point3 point) {
        return ApplyAffine3D(
            current_transform,
            ApplyAffine3D(inverse_bind, point));
    };
    const Point3 origin = map({0.0, 0.0, 0.0});
    const Point3 x_axis = map({1.0, 0.0, 0.0});
    const Point3 y_axis = map({0.0, 1.0, 0.0});
    const Point3 z_axis = map({0.0, 0.0, 1.0});
    delta_transform = {
        x_axis.x - origin.x,
        x_axis.y - origin.y,
        x_axis.z - origin.z,
        y_axis.x - origin.x,
        y_axis.y - origin.y,
        y_axis.z - origin.z,
        z_axis.x - origin.x,
        z_axis.y - origin.y,
        z_axis.z - origin.z,
        origin.x,
        origin.y,
        origin.z};
    return true;
}

Affine3D ScaleAffine3DCoordinateSystem(
    const Affine3D& transform,
    double scale_x,
    double scale_y,
    double scale_z) {
    constexpr double kMinimumScale = 1.0e-12;
    if (std::abs(scale_x) <= kMinimumScale ||
        std::abs(scale_y) <= kMinimumScale ||
        std::abs(scale_z) <= kMinimumScale) {
        return {};
    }
    Affine3D scaled = transform;
    scaled.xy = transform.xy * scale_y / scale_x;
    scaled.xz = transform.xz * scale_z / scale_x;
    scaled.yx = transform.yx * scale_x / scale_y;
    scaled.yz = transform.yz * scale_z / scale_y;
    scaled.zx = transform.zx * scale_x / scale_z;
    scaled.zy = transform.zy * scale_y / scale_z;
    scaled.tx = transform.tx * scale_x;
    scaled.ty = transform.ty * scale_y;
    scaled.tz = transform.tz * scale_z;
    return scaled;
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
    normal.x /= std::abs(transform.scale.x) > kMinimumScale
                    ? transform.scale.x : 1.0;
    normal.y /= std::abs(transform.scale.y) > kMinimumScale
                    ? transform.scale.y : 1.0;
    normal.z /= std::abs(transform.scale.z) > kMinimumScale
                    ? transform.scale.z : 1.0;
    return Normalize(RotatePoint(
        normal,
        0.0,
        0.0,
        0.0,
        transform.rotation_radians.x,
        transform.rotation_radians.y,
        transform.rotation_radians.z));
}

Point3 RecenterCagePoint(
    Point3 point,
    Point3 fixed_reference_center,
    Point3 target_center) {
    return {
        point.x - fixed_reference_center.x + target_center.x,
        point.y - fixed_reference_center.y + target_center.y,
        point.z - fixed_reference_center.z + target_center.z};
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

bool BuildPreSceneRootTransform(
    const Affine3D& root_world_transform,
    const SceneCoordinateTransform& scene_transform,
    Affine3D& pre_scene_transform) {
    const auto map_point = [&](Point3 point, Point3& mapped) {
        const Point3 scene_world =
            ApplyScenePointTransform(point, scene_transform);
        return TryInverseScenePointTransform(
            ApplyAffine3D(root_world_transform, scene_world),
            scene_transform,
            mapped);
    };
    Point3 origin{};
    Point3 x_axis{};
    Point3 y_axis{};
    Point3 z_axis{};
    if (!map_point({0.0, 0.0, 0.0}, origin) ||
        !map_point({1.0, 0.0, 0.0}, x_axis) ||
        !map_point({0.0, 1.0, 0.0}, y_axis) ||
        !map_point({0.0, 0.0, 1.0}, z_axis)) {
        return false;
    }
    pre_scene_transform = {
        x_axis.x - origin.x,
        x_axis.y - origin.y,
        x_axis.z - origin.z,
        y_axis.x - origin.x,
        y_axis.y - origin.y,
        y_axis.z - origin.z,
        z_axis.x - origin.x,
        z_axis.y - origin.y,
        z_axis.z - origin.z,
        origin.x,
        origin.y,
        origin.z};
    return true;
}

SurfaceCoordinateTransform BuildSurfaceCoordinateTransform(
    const SurfaceData& surface,
    Point3 legacy_pivot,
    Point3 render_scale) {
    constexpr double kDegreesToRadians =
        3.14159265358979323846 / 180.0;
    SurfaceCoordinateTransform transform;
    transform.pivot = surface.transform_mode != 0
                          ? Point3{
                                surface.position_x * render_scale.x,
                                surface.position_y * render_scale.y,
                                surface.position_z * render_scale.z}
                          : legacy_pivot;
    transform.rotation_origin = transform.pivot;
    transform.scale = {
        surface.scale_x / 100.0,
        surface.scale_y / 100.0,
        surface.scale_z / 100.0};
    transform.rotation_radians = {
        surface.rotation_x * kDegreesToRadians,
        surface.rotation_y * kDegreesToRadians,
        surface.rotation_z * kDegreesToRadians};
    return transform;
}

Point3 ScaleSurfaceCagePoint(
    Point3 point,
    const SurfaceCoordinateTransform& transform) {
    return {
        transform.rotation_origin.x +
            (point.x - transform.rotation_origin.x) *
                transform.scale.x,
        transform.rotation_origin.y +
            (point.y - transform.rotation_origin.y) *
                transform.scale.y,
        transform.rotation_origin.z +
            (point.z - transform.rotation_origin.z) *
                transform.scale.z};
}

Point3 RotateSurfaceWorldPoint(
    Point3 point,
    const SurfaceCoordinateTransform& transform) {
    return RotatePoint(
        point,
        transform.rotation_origin.x,
        transform.rotation_origin.y,
        transform.rotation_origin.z,
        transform.rotation_radians.x,
        transform.rotation_radians.y,
        transform.rotation_radians.z);
}

namespace {

Point3 Add(Point3 first, Point3 second) {
    return {
        first.x + second.x,
        first.y + second.y,
        first.z + second.z};
}

Point3 Multiply(Point3 point, double scalar) {
    return {
        point.x * scalar,
        point.y * scalar,
        point.z * scalar};
}

Point3 Reflected(Point3 endpoint, Point3 neighbor) {
    return {
        endpoint.x * 2.0 - neighbor.x,
        endpoint.y * 2.0 - neighbor.y,
        endpoint.z * 2.0 - neighbor.z};
}

Point3 CatmullRom(
    Point3 p0,
    Point3 p1,
    Point3 p2,
    Point3 p3,
    double t) {
    const double t2 = t * t;
    const double t3 = t2 * t;
    return Multiply(
        Add(
            Add(
                Multiply(p1, 2.0),
                Multiply(
                    {p2.x - p0.x, p2.y - p0.y, p2.z - p0.z},
                    t)),
            Add(
                Multiply(
                    {2.0 * p0.x - 5.0 * p1.x +
                         4.0 * p2.x - p3.x,
                     2.0 * p0.y - 5.0 * p1.y +
                         4.0 * p2.y - p3.y,
                     2.0 * p0.z - 5.0 * p1.z +
                         4.0 * p2.z - p3.z},
                    t2),
                Multiply(
                    {-p0.x + 3.0 * p1.x -
                         3.0 * p2.x + p3.x,
                     -p0.y + 3.0 * p1.y -
                         3.0 * p2.y + p3.y,
                     -p0.z + 3.0 * p1.z -
                         3.0 * p2.z + p3.z},
                    t3))),
        0.5);
}

template <typename Getter>
Point3 EvaluateAxis(
    std::uint16_t divisions,
    double coordinate,
    Getter getter) {
    const double clamped = std::clamp(coordinate, 0.0, 1.0);
    if (divisions == 1) {
        return Add(
            Multiply(getter(0), 1.0 - clamped),
            Multiply(getter(1), clamped));
    }
    const double scaled = clamped * divisions;
    const std::uint16_t segment = clamped >= 1.0
                                      ? divisions - 1
                                      : static_cast<std::uint16_t>(
                                            std::floor(scaled));
    const double t =
        clamped >= 1.0 ? 1.0 : scaled - segment;
    const Point3 p1 = getter(segment);
    const Point3 p2 = getter(segment + 1);
    const Point3 p0 = segment == 0
                          ? Reflected(p1, p2)
                          : getter(segment - 1);
    const Point3 p3 = segment + 2 > divisions
                          ? Reflected(p2, p1)
                          : getter(segment + 2);
    return CatmullRom(p0, p1, p2, p3, t);
}

}  // namespace

Point3 EvaluateLattice(
    const LatticeData& lattice,
    double u,
    double v) {
    if (!IsValidLattice(lattice)) {
        return {};
    }
    return EvaluateAxis(
        lattice.divisions_y,
        v,
        [&](std::uint16_t row) {
            return EvaluateAxis(
                lattice.divisions_x,
                u,
                [&](std::uint16_t column) {
                    const StoredPoint3& point =
                        lattice.points[LatticePointIndex(
                            lattice.divisions_x,
                            row,
                            column)];
                    return Point3{point.x, point.y, point.z};
                });
        });
}

Point3 EvaluateLatticeNormal(
    const LatticeData& lattice,
    double u,
    double v) {
    constexpr double kStep = 1.0e-4;
    const Point3 u0 =
        EvaluateLattice(lattice, std::max(0.0, u - kStep), v);
    const Point3 u1 =
        EvaluateLattice(lattice, std::min(1.0, u + kStep), v);
    const Point3 v0 =
        EvaluateLattice(lattice, u, std::max(0.0, v - kStep));
    const Point3 v1 =
        EvaluateLattice(lattice, u, std::min(1.0, v + kStep));
    return Normalize(Cross(
        {u1.x - u0.x, u1.y - u0.y, u1.z - u0.z},
        {v1.x - v0.x, v1.y - v0.y, v1.z - v0.z}));
}

bool LatticeCageBounds(
    const LatticeData& lattice,
    Point3& minimum,
    Point3& maximum) {
    if (!IsValidLattice(lattice) || lattice.point_count == 0) {
        return false;
    }
    minimum = {
        lattice.points[0].x,
        lattice.points[0].y,
        lattice.points[0].z};
    maximum = minimum;
    for (std::uint16_t index = 1; index < lattice.point_count; ++index) {
        const StoredPoint3& point = lattice.points[index];
        minimum.x = std::min(minimum.x, static_cast<double>(point.x));
        minimum.y = std::min(minimum.y, static_cast<double>(point.y));
        minimum.z = std::min(minimum.z, static_cast<double>(point.z));
        maximum.x = std::max(maximum.x, static_cast<double>(point.x));
        maximum.y = std::max(maximum.y, static_cast<double>(point.y));
        maximum.z = std::max(maximum.z, static_cast<double>(point.z));
    }
    return true;
}

double RollOriginXForLattice(
    const LatticeData& lattice,
    double tilt_degrees) {
    Point3 minimum{};
    Point3 maximum{};
    if (!LatticeCageBounds(lattice, minimum, maximum)) {
        return 0.0;
    }
    const double tilt = tilt_degrees * 0.017453292519943295;
    const double cosine = std::cos(-tilt);
    const double sine = std::sin(-tilt);
    double origin = std::numeric_limits<double>::infinity();
    const Point3 corners[4] = {
        {minimum.x, minimum.y, 0.0},
        {maximum.x, minimum.y, 0.0},
        {minimum.x, maximum.y, 0.0},
        {maximum.x, maximum.y, 0.0}};
    for (const Point3& corner : corners) {
        const double tilted_x = corner.x * cosine - corner.y * sine;
        origin = std::min(origin, tilted_x);
    }
    return std::isfinite(origin) ? origin : 0.0;
}

Point3 ApplySurfaceRoll(
    Point3 cage_point,
    const SurfaceRollParams& roll) {
    if (!std::isfinite(roll.angle_degrees) ||
        !std::isfinite(roll.tilt_degrees) ||
        !std::isfinite(roll.radius) ||
        !std::isfinite(roll.expand_per_turn) ||
        !std::isfinite(roll.origin_x) ||
        std::abs(roll.angle_degrees) <= 1.0e-8 ||
        roll.radius <= 1.0e-8) {
        return cage_point;
    }

    constexpr double kPi = 3.14159265358979323846;
    constexpr double kTwoPi = 2.0 * kPi;
    const double tilt = roll.tilt_degrees * (kPi / 180.0);
    const double cosine = std::cos(-tilt);
    const double sine = std::sin(-tilt);
    // Tilted frame: roll advances along +X, axis is +Y, wrap lifts +Z.
    const double local_x =
        cage_point.x * cosine - cage_point.y * sine;
    const double local_y =
        cage_point.x * sine + cage_point.y * cosine;
    const double local_z = cage_point.z;

    const double arc = local_x - roll.origin_x;
    const double angle_rad = roll.angle_degrees * (kPi / 180.0);
    const double abs_angle = std::abs(angle_rad);
    const double sign = angle_rad >= 0.0 ? 1.0 : -1.0;
    const double expand = std::max(0.0, roll.expand_per_turn);
    // Arc length of a spiral r(theta) = radius + expand * theta / 2pi:
    // L(Theta) = radius*Theta + expand*Theta^2 / (4pi)
    const double rolled_length =
        roll.radius * abs_angle +
        expand * abs_angle * abs_angle / (2.0 * kTwoPi);

    auto radius_at = [&](double abs_theta) {
        return roll.radius + expand * (abs_theta / kTwoPi);
    };

    double rolled_x = local_x;
    double rolled_z = local_z;
    if (arc <= 0.0 || rolled_length <= 1.0e-12) {
        rolled_x = local_x;
        rolled_z = local_z;
    } else {
        double abs_theta = 0.0;
        double remaining = 0.0;
        if (arc >= rolled_length) {
            abs_theta = abs_angle;
            remaining = arc - rolled_length;
        } else if (expand <= 1.0e-12) {
            abs_theta = arc / roll.radius;
        } else {
            // Solve a*theta^2 + b*theta - arc = 0 with a = expand/(4pi).
            const double a = expand / (2.0 * kTwoPi);
            const double discriminant =
                roll.radius * roll.radius + 4.0 * a * arc;
            abs_theta =
                (-roll.radius + std::sqrt(std::max(0.0, discriminant))) /
                (2.0 * a);
        }
        abs_theta = std::clamp(abs_theta, 0.0, abs_angle);
        const double theta = sign * abs_theta;
        const double radius = radius_at(abs_theta);
        const double sin_t = std::sin(theta);
        const double cos_t = std::cos(theta);
        // Flat remainder continues along the end tangent.
        rolled_x = roll.origin_x + radius * sin_t + remaining * cos_t;
        rolled_z = local_z + radius * (1.0 - cos_t) + remaining * sin_t;
    }

    // Un-tilt back to cage space.
    const double out_x = rolled_x * cosine + local_y * sine;
    const double out_y = -rolled_x * sine + local_y * cosine;
    return {out_x, out_y, rolled_z};
}
