#include "SurfaceLabGeometry.h"
#include "SurfaceLabModel.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace {

int g_checks{};
int g_failures{};

void Check(bool condition, const char* expression, int line) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("FAIL line %d: %s\n", line, expression);
    }
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

bool Near(double first, double second, double epsilon = 1.0e-5) {
    return std::abs(first - second) <= epsilon;
}

void TestInitialization() {
    LatticeData lattice{};
    InitializeLattice(lattice, 1, 16, 1600.0, 900.0, 42);
    CHECK(IsValidLattice(lattice));
    CHECK(lattice.surface_id == 42);
    CHECK(lattice.point_count == 34);
    CHECK(lattice.divisions_x == 1);
    CHECK(lattice.divisions_y == 16);
    const StoredPoint3& bottom_right =
        lattice.points[LatticePointIndex(1, 16, 1)];
    CHECK(Near(bottom_right.x, 1600.0));
    CHECK(Near(bottom_right.y, 900.0));

    SceneData scene{};
    InitializeScene(scene, 1920.0, 1080.0);
    CHECK(IsValidScene(scene));
    CHECK(scene.surface_count == 1);
    CHECK(Near(scene.surfaces[0].position_x, 960.0));
    CHECK(Near(scene.surfaces[0].position_y, 540.0));
}

void TestMaterialValidation() {
    SceneData scene{};
    InitializeScene(scene, 1920.0, 1080.0);
    SurfaceData& surface = scene.surfaces[0];
    surface.back_source_slot = 1;
    surface.image_size_mode = kImageSizeFit;
    surface.image_border_mode = kImageBorderTransparent;
    surface.specular = 75.0F;
    surface.metalness = 80.0F;
    surface.shininess = 32.0F;
    CHECK(IsValidScene(scene));

    surface.back_source_slot = kMaximumSurfaces + 1;
    CHECK(!IsValidScene(scene));
    surface.back_source_slot = 0;
    surface.image_size_mode = kImageSizeFit + 1;
    CHECK(!IsValidScene(scene));
    surface.image_size_mode = kImageSizeStretch;
    surface.specular = std::numeric_limits<float>::quiet_NaN();
    CHECK(!IsValidScene(scene));
    surface.specular = 75.0F;
    surface.metalness = 101.0F;
    CHECK(!IsValidScene(scene));
}

void TestShadowRayIntersections() {
    const Point3 triangle_a{-1.0, -1.0, 5.0};
    const Point3 triangle_b{1.0, -1.0, 5.0};
    const Point3 triangle_c{0.0, 1.0, 5.0};
    double distance{};
    CHECK(RayIntersectsTriangle(
        {0.0, 0.0, 0.0},
        {0.0, 0.0, 1.0},
        triangle_a,
        triangle_b,
        triangle_c,
        0.01,
        10.0,
        &distance));
    CHECK(Near(distance, 5.0));
    CHECK(!RayIntersectsTriangle(
        {2.0, 0.0, 0.0},
        {0.0, 0.0, 1.0},
        triangle_a,
        triangle_b,
        triangle_c,
        0.01,
        10.0));
    CHECK(!RayIntersectsTriangle(
        {0.0, 0.0, 0.0},
        {0.0, 0.0, 1.0},
        triangle_a,
        triangle_b,
        triangle_c,
        5.1,
        10.0));

    CHECK(RayIntersectsBounds(
        {0.0, 0.0, 0.0},
        {0.0, 0.0, 1.0},
        {-1.0, -1.0, 4.0},
        {1.0, 1.0, 6.0},
        0.01,
        10.0));
    CHECK(!RayIntersectsBounds(
        {2.0, 0.0, 0.0},
        {0.0, 0.0, 1.0},
        {-1.0, -1.0, 4.0},
        {1.0, 1.0, 6.0},
        0.01,
        10.0));
}

void TestDeferredInputSizedInitialization() {
    LatticeData deferred{};
    InitializeLattice(deferred, 3, 3, 0.0, 0.0, 10);
    deferred.reserved |= kLatticeFlagNeedsInputSize;
    CHECK(IsValidLattice(deferred));
    CHECK(NeedsInputSizedInitialization(deferred));

    LatticeData serialized{};
    const std::vector<std::uint8_t> bytes = FlattenLattice(deferred);
    CHECK(UnflattenLattice(bytes.data(), bytes.size(), serialized));
    CHECK(
        (serialized.reserved & kLatticeFlagNeedsInputSize) != 0);
    CHECK(NeedsInputSizedInitialization(serialized));

    InitializeLattice(serialized, 3, 3, 1920.0, 1080.0, 10);
    CHECK(
        (serialized.reserved & kLatticeFlagNeedsInputSize) == 0);
    CHECK(!NeedsInputSizedInitialization(serialized));

    LatticeData broken_v124{};
    InitializeLattice(broken_v124, 3, 3, 1.0, 1.0, 11);
    CHECK(NeedsInputSizedInitialization(broken_v124));

    broken_v124.points[LatticePointIndex(3, 1, 1)].z = 0.25F;
    CHECK(!NeedsInputSizedInitialization(broken_v124));

    LatticeData real{};
    InitializeLattice(real, 3, 3, 1920.0, 1080.0, 12);
    CHECK(!NeedsInputSizedInitialization(real));
}

void TestFixedCageCenterUsesRenderSpaceOnce() {
    // At half resolution the full 1920x1080 cage is 960x540 and its centre is
    // already (480,270). Moving Position by (+50,-20) must translate every
    // point by exactly that amount, not apply the 0.5 scale to the centre again.
    const Point3 top_left = RecenterCagePoint(
        {0.0, 0.0, 0.0},
        {480.0, 270.0, 0.0},
        {530.0, 250.0, 0.0});
    const Point3 bottom_right = RecenterCagePoint(
        {960.0, 540.0, 0.0},
        {480.0, 270.0, 0.0},
        {530.0, 250.0, 0.0});
    CHECK(Near(top_left.x, 50.0));
    CHECK(Near(top_left.y, -20.0));
    CHECK(Near(bottom_right.x, 1010.0));
    CHECK(Near(bottom_right.y, 520.0));
}

void TestEveryControlPointInterpolates() {
    LatticeData lattice{};
    InitializeLattice(lattice, 5, 3, 500.0, 300.0, 7);
    for (std::uint16_t row = 0; row <= lattice.divisions_y; ++row) {
        for (std::uint16_t column = 0;
             column <= lattice.divisions_x;
             ++column) {
            lattice.points[LatticePointIndex(
                lattice.divisions_x,
                row,
                column)].z = static_cast<float>(
                    row * row * 11 + column * column * 7);
        }
    }
    for (std::uint16_t row = 0; row <= lattice.divisions_y; ++row) {
        for (std::uint16_t column = 0;
             column <= lattice.divisions_x;
             ++column) {
            const Point3 actual = EvaluateLattice(
                lattice,
                static_cast<double>(column) / lattice.divisions_x,
                static_cast<double>(row) / lattice.divisions_y);
            const StoredPoint3& expected =
                lattice.points[LatticePointIndex(
                    lattice.divisions_x,
                    row,
                    column)];
            CHECK(Near(actual.x, expected.x));
            CHECK(Near(actual.y, expected.y));
            CHECK(Near(actual.z, expected.z));
        }
    }
}

void TestDegenerateAxesAreLinear() {
    LatticeData lattice{};
    InitializeLattice(lattice, 1, 1, 200.0, 100.0, 1);
    lattice.points[LatticePointIndex(1, 0, 0)].z = 0.0F;
    lattice.points[LatticePointIndex(1, 0, 1)].z = 20.0F;
    lattice.points[LatticePointIndex(1, 1, 0)].z = 40.0F;
    lattice.points[LatticePointIndex(1, 1, 1)].z = 60.0F;
    const Point3 point = EvaluateLattice(lattice, 0.25, 0.75);
    CHECK(Near(point.x, 50.0));
    CHECK(Near(point.y, 75.0));
    CHECK(Near(point.z, 35.0));
}

void TestLocalSupport() {
    LatticeData lattice{};
    InitializeLattice(lattice, 6, 6, 600.0, 600.0, 2);
    lattice.points[LatticePointIndex(6, 3, 3)].z = 100.0F;
    CHECK(Near(EvaluateLattice(lattice, 0.08, 0.08).z, 0.0));
    CHECK(EvaluateLattice(lattice, 0.5, 0.5).z > 99.9);
    CHECK(Near(EvaluateLattice(lattice, 0.92, 0.92).z, 0.0));
}

void TestResizeSamplesExistingShape() {
    LatticeData coarse{};
    InitializeLattice(coarse, 2, 2, 200.0, 120.0, 99);
    coarse.points[LatticePointIndex(2, 1, 1)].z = 80.0F;
    LatticeData fine{};
    CHECK(ResizeLattice(coarse, 8, 6, fine));
    CHECK(IsValidLattice(fine));
    CHECK(fine.surface_id == coarse.surface_id);
    for (std::uint16_t row = 0; row <= fine.divisions_y; ++row) {
        for (std::uint16_t column = 0;
             column <= fine.divisions_x;
             ++column) {
            const double u =
                static_cast<double>(column) / fine.divisions_x;
            const double v =
                static_cast<double>(row) / fine.divisions_y;
            const Point3 expected = EvaluateLattice(coarse, u, v);
            const StoredPoint3& actual =
                fine.points[LatticePointIndex(
                    fine.divisions_x,
                    row,
                    column)];
            CHECK(Near(expected.x, actual.x, 1.0e-4));
            CHECK(Near(expected.y, actual.y, 1.0e-4));
            CHECK(Near(expected.z, actual.z, 1.0e-4));
        }
    }
}

void TestKeyframeInterpolation() {
    LatticeData left{};
    InitializeLattice(left, 3, 2, 300.0, 200.0, 123);
    LatticeData right = left;
    for (std::size_t index = 0; index < right.point_count; ++index) {
        right.points[index].x += 20.0F;
        right.points[index].y -= 10.0F;
        right.points[index].z = static_cast<float>(index * 4);
    }
    LatticeData halfway{};
    CHECK(InterpolateLattice(left, right, 0.5, halfway));
    for (std::size_t index = 0; index < halfway.point_count; ++index) {
        CHECK(Near(
            halfway.points[index].x,
            left.points[index].x + 10.0));
        CHECK(Near(
            halfway.points[index].y,
            left.points[index].y - 5.0));
        CHECK(Near(
            halfway.points[index].z,
            right.points[index].z * 0.5));
    }

    right.surface_id += 1;
    CHECK(!InterpolateLattice(left, right, 0.5, halfway));
}

void TestFlattenRoundTrip() {
    LatticeData source{};
    InitializeLattice(
        source,
        16,
        1,
        1920.0,
        1080.0,
        0x1020304050607080ULL);
    source.points[9].z = -123.25F;
    const std::vector<std::uint8_t> bytes = FlattenLattice(source);
    CHECK(bytes.size() == 22 + source.point_count * 12);
    CHECK(bytes[0] == 0x53);
    CHECK(bytes[1] == 0x4C);
    CHECK(bytes[2] == 0x56);
    CHECK(bytes[3] == 0x31);
    LatticeData decoded{};
    CHECK(UnflattenLattice(bytes.data(), bytes.size(), decoded));
    CHECK(CompareLattices(source, decoded));

    // v1.2.4 and earlier did not serialize the reserved flags.
    std::vector<std::uint8_t> legacy_bytes = bytes;
    legacy_bytes.erase(legacy_bytes.begin() + 12, legacy_bytes.begin() + 14);
    LatticeData legacy_decoded{};
    CHECK(UnflattenLattice(
        legacy_bytes.data(),
        legacy_bytes.size(),
        legacy_decoded));
    CHECK(CompareLattices(source, legacy_decoded));
    CHECK(legacy_decoded.reserved == 0);

    CHECK(!UnflattenLattice(bytes.data(), bytes.size() - 1, decoded));
}

void TestValidationAndNormals() {
    LatticeData lattice{};
    InitializeLattice(lattice, 6, 4, 600.0, 400.0, 3);
    const Point3 normal =
        EvaluateLatticeNormal(lattice, 0.37, 0.81);
    CHECK(Near(normal.x, 0.0));
    CHECK(Near(normal.y, 0.0));
    CHECK(Near(normal.z, 1.0));
    lattice.points[2].z =
        std::numeric_limits<float>::quiet_NaN();
    CHECK(!IsValidLattice(lattice));
}

void TestSceneTransformRoundTrip() {
    SceneCoordinateTransform transform;
    transform.pivot = {50.0, 40.0, 0.0};
    transform.position = {300.0, 200.0, 50.0};
    transform.scale = {1.2, 0.8, 1.5};
    transform.rotation_radians = {0.2, -0.4, 0.1};
    const Point3 source{90.0, 25.0, 18.0};
    const Point3 transformed =
        ApplyScenePointTransform(source, transform);
    Point3 restored{};
    CHECK(TryInverseScenePointTransform(
        transformed,
        transform,
        restored));
    CHECK(Near(source.x, restored.x));
    CHECK(Near(source.y, restored.y));
    CHECK(Near(source.z, restored.z));
}

void TestAffineRoundTrip() {
    const Affine2D transform{
        1.2, 0.3, -0.2, 0.8, 40.0, -12.0};
    Affine2D inverse{};
    CHECK(TryInvertAffine2D(transform, inverse));
    const Point2 source{21.0, -7.0};
    const Point2 restored =
        ApplyAffine2D(inverse, ApplyAffine2D(transform, source));
    CHECK(Near(source.x, restored.x));
    CHECK(Near(source.y, restored.y));
}

void TestAffine3DRootTransforms() {
    const Affine3D transform{
        0.0, 1.0, 0.0,
        -1.0, 0.0, 0.0,
        0.0, 0.0, 1.5,
        320.0, -40.0, 12.0};
    Affine3D inverse{};
    CHECK(TryInvertAffine3D(transform, inverse));
    const Point3 source{21.0, -7.0, 8.0};
    const Point3 restored =
        ApplyAffine3D(
            inverse,
            ApplyAffine3D(transform, source));
    CHECK(Near(source.x, restored.x));
    CHECK(Near(source.y, restored.y));
    CHECK(Near(source.z, restored.z));

    const Affine3D scaled =
        ScaleAffine3DCoordinateSystem(
            transform,
            0.5,
            0.25,
            2.0);
    const Point3 scaled_source{
        source.x * 0.5,
        source.y * 0.25,
        source.z * 2.0};
    const Point3 scaled_result =
        ApplyAffine3D(scaled, scaled_source);
    const Point3 full_result =
        ApplyAffine3D(transform, source);
    CHECK(Near(scaled_result.x, full_result.x * 0.5));
    CHECK(Near(scaled_result.y, full_result.y * 0.25));
    CHECK(Near(scaled_result.z, full_result.z * 2.0));

    const Affine3D oriented_bind{
        0.0, 1.0, 0.0,
        -1.0, 0.0, 0.0,
        0.0, 0.0, 1.0,
        960.0, 540.0, 25.0};
    Affine3D delta{};
    CHECK(BuildAffineDeltaTransform(
        oriented_bind,
        oriented_bind,
        delta));
    const Point3 bound_point{820.0, 610.0, 40.0};
    const Point3 unchanged = ApplyAffine3D(delta, bound_point);
    CHECK(Near(unchanged.x, bound_point.x));
    CHECK(Near(unchanged.y, bound_point.y));
    CHECK(Near(unchanged.z, bound_point.z));

    Affine3D moved_root = oriented_bind;
    moved_root.tx += 120.0;
    moved_root.ty -= 35.0;
    moved_root.tz += 18.0;
    CHECK(BuildAffineDeltaTransform(
        oriented_bind,
        moved_root,
        delta));
    const Point3 moved = ApplyAffine3D(delta, bound_point);
    CHECK(Near(moved.x, bound_point.x + 120.0));
    CHECK(Near(moved.y, bound_point.y - 35.0));
    CHECK(Near(moved.z, bound_point.z + 18.0));
}

void TestPointAttachAfterRootMotion() {
    const Affine3D bind_root{
        0.0, 1.0, 0.0,
        -1.0, 0.0, 0.0,
        0.0, 0.0, 1.0,
        960.0, 540.0, 25.0};
    const Affine3D current_root{
        -1.0, 0.0, 0.0,
        0.0, -1.0, 0.0,
        0.0, 0.0, 1.0,
        1120.0, 470.0, 60.0};
    Affine3D root_delta{};
    CHECK(BuildAffineDeltaTransform(
        bind_root,
        current_root,
        root_delta));

    Affine3D inverse_bind{};
    CHECK(TryInvertAffine3D(bind_root, inverse_bind));
    const Point3 unrooted_point{820.0, 610.0, 40.0};
    const Point3 child_local =
        ApplyAffine3D(inverse_bind, unrooted_point);
    const Point3 issued_world =
        ApplyAffine3D(current_root, child_local);
    const Point3 rendered_world =
        ApplyAffine3D(root_delta, unrooted_point);
    CHECK(Near(issued_world.x, rendered_world.x));
    CHECK(Near(issued_world.y, rendered_world.y));
    CHECK(Near(issued_world.z, rendered_world.z));

    Affine3D inverse_delta{};
    CHECK(TryInvertAffine3D(root_delta, inverse_delta));
    const Point3 resolved_controller =
        ApplyAffine3D(inverse_delta, issued_world);
    CHECK(Near(resolved_controller.x, unrooted_point.x));
    CHECK(Near(resolved_controller.y, unrooted_point.y));
    CHECK(Near(resolved_controller.z, unrooted_point.z));

    // The bind-to-current mapping also carries the tangent frame, so a new
    // point Null faces the rooted polygon instead of using world orientation.
    const Point3 unrooted_x{
        unrooted_point.x + 1.0,
        unrooted_point.y,
        unrooted_point.z};
    const Point3 issued_x = ApplyAffine3D(
        current_root,
        ApplyAffine3D(inverse_bind, unrooted_x));
    const Point3 rendered_x =
        ApplyAffine3D(root_delta, unrooted_x);
    CHECK(Near(
        issued_x.x - issued_world.x,
        rendered_x.x - rendered_world.x));
    CHECK(Near(
        issued_x.y - issued_world.y,
        rendered_x.y - rendered_world.y));
    CHECK(Near(
        issued_x.z - issued_world.z,
        rendered_x.z - rendered_world.z));
}

void TestRootTransformConjugatesThroughScene() {
    SceneCoordinateTransform scene;
    scene.pivot = {960.0, 540.0, 0.0};
    scene.position = {1040.0, 500.0, 25.0};
    scene.scale = {1.2, 0.8, 1.1};
    scene.rotation_radians = {0.1, -0.2, 0.3};
    const Affine3D root{
        0.0, 1.0, 0.0,
        -1.0, 0.0, 0.0,
        0.0, 0.0, 1.0,
        1500.0, -400.0, 30.0};
    Affine3D pre_scene{};
    CHECK(BuildPreSceneRootTransform(
        root,
        scene,
        pre_scene));
    const Point3 source{700.0, 330.0, 45.0};
    const Point3 expected =
        ApplyAffine3D(
            root,
            ApplyScenePointTransform(source, scene));
    const Point3 actual =
        ApplyScenePointTransform(
            ApplyAffine3D(pre_scene, source),
            scene);
    CHECK(Near(actual.x, expected.x));
    CHECK(Near(actual.y, expected.y));
    CHECK(Near(actual.z, expected.z));

    const Point3 second{850.0, 410.0, 45.0};
    const Point3 first_rooted =
        ApplyAffine3D(
            root,
            ApplyScenePointTransform(source, scene));
    const Point3 second_rooted =
        ApplyAffine3D(
            root,
            ApplyScenePointTransform(second, scene));
    const Point3 first_unrooted =
        ApplyScenePointTransform(source, scene);
    const Point3 second_unrooted =
        ApplyScenePointTransform(second, scene);
    const auto distance = [](Point3 left, Point3 right) {
        const Point3 delta{
            left.x - right.x,
            left.y - right.y,
            left.z - right.z};
        return std::sqrt(Dot(delta, delta));
    };
    CHECK(Near(
        distance(first_rooted, second_rooted),
        distance(first_unrooted, second_unrooted)));
}

void TestSurfacePositionUsesRenderScaleOnce() {
    SurfaceData surface{};
    surface.transform_mode = 1;
    surface.position_x = 56.5F;
    surface.position_y = 32.0F;
    surface.position_z = 4.0F;
    surface.scale_x = 100.0F;
    surface.scale_y = 100.0F;
    surface.scale_z = 100.0F;
    const SurfaceCoordinateTransform transform =
        BuildSurfaceCoordinateTransform(
            surface,
            {960.0, 540.0, 0.0},
            {1.0 / 17.0, 1.0 / 17.0, 1.0 / 17.0});
    CHECK(Near(transform.pivot.x, 56.5 / 17.0));
    CHECK(Near(transform.pivot.y, 32.0 / 17.0));
    CHECK(Near(transform.pivot.z, 4.0 / 17.0));
}

void TestSubframeSampleTimes() {
    CHECK(BuildSubframeSampleTimes(1000, 100, 0.0, -0.25, 16) ==
          std::vector<std::int64_t>{1000});
    CHECK(BuildSubframeSampleTimes(1000, 100, 0.5, -0.25, 4) ==
          (std::vector<std::int64_t>{981, 994, 1006, 1019}));
    const std::vector<std::int64_t> full_shutter =
        BuildSubframeSampleTimes(500, 80, 1.0, -0.5, 4);
    CHECK(full_shutter ==
          (std::vector<std::int64_t>{470, 490, 510, 530}));
    CHECK(BuildSubframeSampleTimes(100, 1, 0.5, -0.25, 32).size() == 1);
}

void TestSurfaceRollIdentityAndCylinder() {
    LatticeData lattice{};
    InitializeLattice(lattice, 4, 1, 400.0, 100.0, 9);
    const double origin = RollOriginXForLattice(lattice, 0.0);
    CHECK(Near(origin, 0.0, 1.0e-4));

    SurfaceRollParams identity{
        0.0,
        0.0,
        100.0,
        0.0,
        origin};
    const Point3 flat = ApplySurfaceRoll({200.0, 50.0, 0.0}, identity);
    CHECK(Near(flat.x, 200.0));
    CHECK(Near(flat.y, 50.0));
    CHECK(Near(flat.z, 0.0));

    // One quarter turn of a radius-100 cylinder maps arc 50pi/2? 
    // rolled_length for 90deg = radius * pi/2 ≈ 157.08
    // Point at arc 0 stays put; point at arc = radius*(pi/2) sits at top.
    SurfaceRollParams quarter{
        90.0,
        0.0,
        100.0,
        0.0,
        origin};
    const Point3 start = ApplySurfaceRoll({0.0, 25.0, 0.0}, quarter);
    CHECK(Near(start.x, 0.0, 1.0e-4));
    CHECK(Near(start.z, 0.0, 1.0e-4));
    const double arc_top = 100.0 * 0.5 * 3.14159265358979323846;
    const Point3 top =
        ApplySurfaceRoll({arc_top, 25.0, 0.0}, quarter);
    CHECK(Near(top.x, 100.0, 1.0e-3));
    CHECK(Near(top.y, 25.0, 1.0e-3));
    CHECK(Near(top.z, 100.0, 1.0e-3));
}

}  // namespace

int main() {
    TestInitialization();
    TestMaterialValidation();
    TestShadowRayIntersections();
    TestDeferredInputSizedInitialization();
    TestFixedCageCenterUsesRenderSpaceOnce();
    TestEveryControlPointInterpolates();
    TestDegenerateAxesAreLinear();
    TestLocalSupport();
    TestResizeSamplesExistingShape();
    TestKeyframeInterpolation();
    TestFlattenRoundTrip();
    TestValidationAndNormals();
    TestSceneTransformRoundTrip();
    TestAffineRoundTrip();
    TestAffine3DRootTransforms();
    TestPointAttachAfterRootMotion();
    TestRootTransformConjugatesThroughScene();
    TestSurfacePositionUsesRenderScaleOnce();
    TestSubframeSampleTimes();
    TestSurfaceRollIdentityAndCylinder();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
