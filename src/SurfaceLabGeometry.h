#pragma once

// Host-independent geometry and deformation math for SurfaceLab.
//
// AE-free: everything here operates purely on the Point/scene model. The small,
// hot vector primitives are inline; EvaluatePatch (bicubic surface point) and
// the deformation pass are defined in SurfaceLabGeometry.cpp so they can be
// unit-tested off-host alongside the model.

#include "SurfaceLabModel.h"

#include <array>
#include <cmath>

inline Point3 Cross(Point3 a, Point3 b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x};
}

inline Point3 Normalize(Point3 vector) {
    const double length = std::sqrt(
        vector.x * vector.x + vector.y * vector.y + vector.z * vector.z);
    if (length <= 1.0e-10) {
        return {0.0, 0.0, 1.0};
    }
    return {vector.x / length, vector.y / length, vector.z / length};
}

inline double Dot(Point3 a, Point3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

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

    const double sin_x = std::sin(rotation_x);
    const double cos_x = std::cos(rotation_x);
    const double y_x = point.y * cos_x - point.z * sin_x;
    const double z_x = point.y * sin_x + point.z * cos_x;
    point.y = y_x;
    point.z = z_x;

    const double sin_y = std::sin(rotation_y);
    const double cos_y = std::cos(rotation_y);
    const double x_y = point.x * cos_y + point.z * sin_y;
    const double z_y = -point.x * sin_y + point.z * cos_y;
    point.x = x_y;
    point.z = z_y;

    const double sin_z = std::sin(rotation_z);
    const double cos_z = std::cos(rotation_z);
    const double x_z = point.x * cos_z - point.y * sin_z;
    const double y_z = point.x * sin_z + point.y * cos_z;
    point.x = x_z + center_x;
    point.y = y_z + center_y;
    point.z += center_z;
    return point;
}

inline Point3 InverseRotateVector(
    Point3 vector,
    double rotation_x,
    double rotation_y,
    double rotation_z) {
    const double sin_z = std::sin(rotation_z);
    const double cos_z = std::cos(rotation_z);
    const double x_z = vector.x * cos_z + vector.y * sin_z;
    const double y_z = -vector.x * sin_z + vector.y * cos_z;
    vector.x = x_z;
    vector.y = y_z;

    const double sin_y = std::sin(rotation_y);
    const double cos_y = std::cos(rotation_y);
    const double x_y = vector.x * cos_y - vector.z * sin_y;
    const double z_y = vector.x * sin_y + vector.z * cos_y;
    vector.x = x_y;
    vector.z = z_y;

    const double sin_x = std::sin(rotation_x);
    const double cos_x = std::cos(rotation_x);
    const double y_x = vector.y * cos_x + vector.z * sin_x;
    const double z_x = -vector.y * sin_x + vector.z * cos_x;
    vector.y = y_x;
    vector.z = z_x;
    return vector;
}

Point3 EvaluatePatch(
    const std::array<Point3, 16>& points,
    double u,
    double v);

void ApplySurfaceDeform(
    Point3& point,
    const SurfaceData& surface,
    double u,
    double v,
    double pivot_x,
    double pivot_y,
    double pivot_z,
    double extent_x,
    double extent_y);
