#pragma once

#include "SurfaceLabModel.h"

#include <cmath>

inline Point3 Cross(Point3 a, Point3 b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x};
}

inline double Dot(Point3 a, Point3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Point3 Normalize(Point3 vector) {
    const double length = std::sqrt(Dot(vector, vector));
    if (length <= 1.0e-10) {
        return {0.0, 0.0, 1.0};
    }
    return {
        vector.x / length,
        vector.y / length,
        vector.z / length};
}

// Host-independent intersection helpers used by the CPU shadow accelerator.
bool RayIntersectsTriangle(
    Point3 origin,
    Point3 direction,
    Point3 a,
    Point3 b,
    Point3 c,
    double minimum_distance,
    double maximum_distance,
    double* hit_distance = nullptr);

bool RayIntersectsBounds(
    Point3 origin,
    Point3 direction,
    Point3 minimum,
    Point3 maximum,
    double minimum_distance,
    double maximum_distance);

struct Affine2D {
    double xx{1.0};
    double xy{};
    double yx{};
    double yy{1.0};
    double tx{};
    double ty{};
};

Point2 ApplyAffine2D(const Affine2D& transform, Point2 point);

bool TryInvertAffine2D(
    const Affine2D& transform,
    Affine2D& inverse);

Point3 ApplyAffine3D(
    const Affine3D& transform,
    Point3 point);

bool TryInvertAffine3D(
    const Affine3D& transform,
    Affine3D& inverse);

// Returns the world-space delta that maps geometry from its bind transform to
// the current transform. An unchanged oriented Root therefore produces the
// identity transform instead of applying its bind orientation twice.
bool BuildAffineDeltaTransform(
    const Affine3D& bind_transform,
    const Affine3D& current_transform,
    Affine3D& delta_transform);

Affine3D ScaleAffine3DCoordinateSystem(
    const Affine3D& transform,
    double scale_x,
    double scale_y,
    double scale_z);

inline Point3 RotatePoint(
    Point3 point,
    double center_x,
    double center_y,
    double center_z,
    double rotation_x,
    double rotation_y,
    double rotation_z) {
    point.x -= center_x;
    point.y -= center_y;
    point.z -= center_z;
    const double sx = std::sin(rotation_x);
    const double cx = std::cos(rotation_x);
    const double sy = std::sin(rotation_y);
    const double cy = std::cos(rotation_y);
    const double sz = std::sin(rotation_z);
    const double cz = std::cos(rotation_z);
    const double x_y = point.x;
    const double y_x = point.y * cx - point.z * sx;
    const double z_x = point.y * sx + point.z * cx;
    const double x_z = x_y * cy + z_x * sy;
    const double z_y = -x_y * sy + z_x * cy;
    const double final_x = x_z * cz - y_x * sz;
    const double final_y = x_z * sz + y_x * cz;
    return {
        final_x + center_x,
        final_y + center_y,
        z_y + center_z};
}

inline Point3 InverseRotateVector(
    Point3 vector,
    double rotation_x,
    double rotation_y,
    double rotation_z) {
    const double sz = std::sin(rotation_z);
    const double cz = std::cos(rotation_z);
    const double xz = vector.x * cz + vector.y * sz;
    const double yz = -vector.x * sz + vector.y * cz;
    vector.x = xz;
    vector.y = yz;
    const double sy = std::sin(rotation_y);
    const double cy = std::cos(rotation_y);
    const double xy = vector.x * cy - vector.z * sy;
    const double zy = vector.x * sy + vector.z * cy;
    vector.x = xy;
    vector.z = zy;
    const double sx = std::sin(rotation_x);
    const double cx = std::cos(rotation_x);
    const double yx = vector.y * cx + vector.z * sx;
    const double zx = -vector.y * sx + vector.z * cx;
    vector.y = yx;
    vector.z = zx;
    return vector;
}

struct SurfaceCoordinateTransform {
    Point3 pivot{};
    Point3 rotation_origin{};
    Point3 scale{1.0, 1.0, 1.0};
    Point3 rotation_radians{};
};

struct SceneCoordinateTransform {
    Point3 pivot{};
    Point3 position{};
    Point3 scale{1.0, 1.0, 1.0};
    Point3 rotation_radians{};
};

Point3 ApplyScenePointTransform(
    Point3 point,
    const SceneCoordinateTransform& transform);

Point3 RecenterCagePoint(
    Point3 point,
    Point3 fixed_reference_center,
    Point3 target_center);

Point3 ApplySceneVectorTransform(
    Point3 vector,
    const SceneCoordinateTransform& transform);

Point3 ApplySceneNormalTransform(
    Point3 normal,
    const SceneCoordinateTransform& transform);

bool TryInverseScenePointTransform(
    Point3 point,
    const SceneCoordinateTransform& transform,
    Point3& untransformed);

bool BuildPreSceneRootTransform(
    const Affine3D& root_world_transform,
    const SceneCoordinateTransform& scene_transform,
    Affine3D& pre_scene_transform);

SurfaceCoordinateTransform BuildSurfaceCoordinateTransform(
    const SurfaceData& surface,
    Point3 legacy_pivot,
    Point3 render_scale = {1.0, 1.0, 1.0});

Point3 ScaleSurfaceCagePoint(
    Point3 cage_point,
    const SurfaceCoordinateTransform& transform);

Point3 RotateSurfaceWorldPoint(
    Point3 scaled_point,
    const SurfaceCoordinateTransform& transform);

Point3 EvaluateLattice(
    const LatticeData& lattice,
    double u,
    double v);

Point3 EvaluateLatticeNormal(
    const LatticeData& lattice,
    double u,
    double v);

// Procedural paper-roll deform in cage-local space. angle_degrees == 0 is an
// identity. Positive angle rolls the +X side (after tilt) onto +Z. radius is
// the base cylinder radius; expand_per_turn grows the radius each full turn
// for spiral packing. origin_x is the roll-start edge in the tilted frame.
struct SurfaceRollParams {
    double angle_degrees{};
    double tilt_degrees{};
    double radius{200.0};
    double expand_per_turn{};
    double origin_x{};
};

bool LatticeCageBounds(
    const LatticeData& lattice,
    Point3& minimum,
    Point3& maximum);

// origin_x for a given tilt: minimum of tilted-frame X over lattice corners.
double RollOriginXForLattice(
    const LatticeData& lattice,
    double tilt_degrees);

Point3 ApplySurfaceRoll(
    Point3 cage_point,
    const SurfaceRollParams& roll);
