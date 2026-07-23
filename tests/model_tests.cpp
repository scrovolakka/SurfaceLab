// Host-independent unit tests for the SurfaceLab scene model and its V1..V13
// migration chain. These exercise SurfaceLabModel.{h,cpp} with no After Effects
// SDK present, so they build and run on any platform:
//
//   c++ -std=c++17 -I src src/SurfaceLabModel.cpp \
//       src/SurfaceLabGeometry.cpp tests/model_tests.cpp -o model_tests
//   ./model_tests
//
// The goal is a fast regression net around the most bug-prone, least
// AE-observable logic: on-disk migration and scene validation.

#include "SurfaceLabModel.h"
#include "SurfaceLabGeometry.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool condition, const char* expr, const char* test, int line) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL [%s:%d] %s\n", test, line, expr);
    }
}

#define CHECK(cond) Check((cond), #cond, test_name, __LINE__)

// Fill a versioned surface's identity + a recognizable control-point pattern.
template <typename SurfaceT>
void SeedSurface(SurfaceT& surface, std::uint32_t id) {
    surface.id = id;
    surface.enabled = 1;
    for (int i = 0; i < 16; ++i) {
        surface.control_points[i].x = static_cast<float>(id * 100 + i);
        surface.control_points[i].y = static_cast<float>(id * 200 + i);
        surface.control_points[i].z = static_cast<float>(i);
    }
    surface.rotation_x = 10.0F;
    surface.rotation_y = 20.0F;
    surface.rotation_z = 30.0F;
}

template <typename SceneT>
void SeedSceneHeader(SceneT& scene, std::uint32_t version, std::uint32_t count) {
    scene.magic = kSceneMagic;
    scene.schema_version = version;
    scene.active = 1;
    scene.surface_count = count;
    scene.selected_surface = 0;
    scene.next_surface_id = count + 1;
}

// ---- Tests ---------------------------------------------------------------

void TestInitializeScene() {
    const char* test_name = "InitializeScene";
    SceneData scene{};
    InitializeScene(scene, 1920.0, 1080.0);
    CHECK(scene.magic == kSceneMagic);
    CHECK(scene.schema_version == kSceneSchemaVersion);
    CHECK(scene.surface_count == 1);
    CHECK(scene.surfaces[0].enabled == 1);
    CHECK(IsValidScene(scene));
}

void TestMigrateV1PreservesGeometryAndValidates() {
    const char* test_name = "MigrateV1";
    SceneDataV1 src{};
    SeedSceneHeader(src, 1, 2);
    SeedSurface(src.surfaces[0], 1);
    SeedSurface(src.surfaces[1], 2);

    SceneData dst{};
    MigrateSceneV1(src, dst);

    CHECK(dst.magic == kSceneMagic);
    CHECK(dst.schema_version == kSceneSchemaVersion);
    CHECK(dst.surface_count == 2);
    // Geometry preserved verbatim.
    CHECK(dst.surfaces[0].control_points[5].x == 105.0F);
    CHECK(dst.surfaces[1].control_points[5].y == 405.0F);
    CHECK(dst.surfaces[0].rotation_z == 30.0F);
    // Fields absent in V1 get sane defaults.
    CHECK(dst.surfaces[0].scale_x == 100.0F);
    CHECK(dst.surfaces[0].opacity == 100.0F);

    // Two-phase legacy contract: MigrateScene* carries only geometry/material;
    // animation banks stay 0 until the loader assigns them for pre-v13 scenes.
    // Before that step a multi-surface scene has duplicate bank 0.
    CHECK(dst.surfaces[0].animation_bank == dst.surfaces[1].animation_bank);
    CHECK(!IsValidScene(dst));

    AssignLegacyAnimationBanks(dst);
    CHECK(IsValidScene(dst));
    CHECK(dst.surfaces[0].animation_bank != dst.surfaces[1].animation_bank);
}

// Fill in the middle of the migration chain so every version has a direct
// regression test. Each seeds a version-characteristic field and confirms the
// migration carries it into the current model and yields a valid single-surface
// scene.
void TestMigrateV2PreservesScale() {
    const char* test_name = "MigrateV2";
    SceneDataV2 src{};
    SeedSceneHeader(src, 2, 1);
    SeedSurface(src.surfaces[0], 1);
    src.surfaces[0].scale_x = 80.0F;
    SceneData dst{};
    MigrateSceneV2(src, dst);
    CHECK(dst.schema_version == kSceneSchemaVersion);
    CHECK(dst.surfaces[0].scale_x == 80.0F);
    CHECK(IsValidScene(dst));
}

void TestMigrateV3PreservesDivisions() {
    const char* test_name = "MigrateV3";
    SceneDataV3 src{};
    SeedSceneHeader(src, 3, 1);
    SeedSurface(src.surfaces[0], 1);
    src.surfaces[0].divisions_x = 20;
    src.surfaces[0].divisions_y = 24;
    SceneData dst{};
    MigrateSceneV3(src, dst);
    CHECK(dst.surfaces[0].divisions_x == 20);
    CHECK(dst.surfaces[0].divisions_y == 24);
    CHECK(IsValidScene(dst));
}

void TestMigrateV4PreservesOpacity() {
    const char* test_name = "MigrateV4";
    SceneDataV4 src{};
    SeedSceneHeader(src, 4, 1);
    SeedSurface(src.surfaces[0], 1);
    src.surfaces[0].opacity = 55.0F;
    src.surfaces[0].source_slot = 2;
    SceneData dst{};
    MigrateSceneV4(src, dst);
    CHECK(dst.surfaces[0].opacity == 55.0F);
    CHECK(dst.surfaces[0].source_slot == 2);
    CHECK(IsValidScene(dst));
}

void TestMigrateV5PreservesThickness() {
    const char* test_name = "MigrateV5";
    SceneDataV5 src{};
    SeedSceneHeader(src, 5, 1);
    SeedSurface(src.surfaces[0], 1);
    src.surfaces[0].thickness = 12.0F;
    SceneData dst{};
    MigrateSceneV5(src, dst);
    CHECK(dst.surfaces[0].thickness == 12.0F);
    CHECK(IsValidScene(dst));
}

void TestMigrateV6PreservesMaterial() {
    const char* test_name = "MigrateV6";
    SceneDataV6 src{};
    SeedSceneHeader(src, 6, 1);
    SeedSurface(src.surfaces[0], 1);
    src.surfaces[0].shininess = 8.0F;
    SceneData dst{};
    MigrateSceneV6(src, dst);
    CHECK(dst.surfaces[0].shininess == 8.0F);
    CHECK(IsValidScene(dst));
}

void TestMigrateV7PreservesDeform() {
    const char* test_name = "MigrateV7";
    SceneDataV7 src{};
    SeedSceneHeader(src, 7, 1);
    SeedSurface(src.surfaces[0], 1);
    src.surfaces[0].bend_x = 30.0F;
    src.surfaces[0].roll_angle = 20.0F;
    SceneData dst{};
    MigrateSceneV7(src, dst);
    CHECK(dst.surfaces[0].bend_x == 30.0F);
    CHECK(dst.surfaces[0].roll_angle == 20.0F);
    CHECK(IsValidScene(dst));
}

void TestMigrateV8PreservesCornerCurls() {
    const char* test_name = "MigrateV8";
    SceneDataV8 src{};
    SeedSceneHeader(src, 8, 1);
    SeedSurface(src.surfaces[0], 1);
    src.surfaces[0].corner_curls[0].amount = 25.0F;
    SceneData dst{};
    MigrateSceneV8(src, dst);
    CHECK(dst.surfaces[0].corner_curls[0].amount == 25.0F);
    CHECK(IsValidScene(dst));
}

void TestMigrateV9PreservesDeformStreams() {
    const char* test_name = "MigrateV9";
    SceneDataV9 src{};
    SeedSceneHeader(src, 9, 1);
    SeedSurface(src.surfaces[0], 1);
    src.surfaces[0].corner_curls[2].amount = 42.0F;
    src.surfaces[0].edge_twists[1].angle = 15.0F;

    SceneData dst{};
    MigrateSceneV9(src, dst);

    CHECK(dst.schema_version == kSceneSchemaVersion);
    CHECK(dst.surfaces[0].corner_curls[2].amount == 42.0F);
    CHECK(dst.surfaces[0].edge_twists[1].angle == 15.0F);
    CHECK(IsValidScene(dst));
}

void TestMigrateV12PreservesBackSlot() {
    const char* test_name = "MigrateV12";
    SceneDataV12 src{};
    SeedSceneHeader(src, 12, 1);
    SeedSurface(src.surfaces[0], 1);
    src.surfaces[0].back_source_slot = 3;

    SceneData dst{};
    MigrateSceneV12(src, dst);

    CHECK(dst.schema_version == kSceneSchemaVersion);
    CHECK(dst.surfaces[0].back_source_slot == 3);
    CHECK(IsValidScene(dst));
}

// V10 shares SceneDataV11's binary layout; the two migrations must both accept
// a V11-shaped source and produce a valid scene (guards the confusing naming).
void TestMigrateV10AndV11Layout() {
    const char* test_name = "MigrateV10/V11";
    SceneDataV11 src{};
    SeedSceneHeader(src, 11, 1);
    SeedSurface(src.surfaces[0], 1);
    src.surfaces[0].rotation_origin_mode = 6;  // custom
    src.surfaces[0].rotation_origin_x = 25.0F;

    SceneData via_v11{};
    MigrateSceneV11(src, via_v11);
    CHECK(via_v11.surfaces[0].rotation_origin_x == 25.0F);
    CHECK(IsValidScene(via_v11));

    SceneData via_v10{};
    MigrateSceneV10(src, via_v10);
    CHECK(via_v10.schema_version == kSceneSchemaVersion);
    CHECK(IsValidScene(via_v10));
}

bool AllFinite(const Point3& p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}

bool NearlyEqual(double a, double b, double eps = 1e-6) {
    return std::fabs(a - b) <= eps;
}

// EvaluatePatch must interpolate the bicubic Bezier cage endpoints exactly:
// (u,v)=(0,0) -> control point 0, (1,1) -> control point 15.
void TestEvaluatePatchCorners() {
    const char* test_name = "EvaluatePatchCorners";
    std::array<Point3, 16> pts{};
    for (int i = 0; i < 16; ++i) {
        pts[i] = Point3{static_cast<double>(i), static_cast<double>(2 * i), 0.0};
    }
    const Point3 origin = EvaluatePatch(pts, 0.0, 0.0);
    const Point3 far = EvaluatePatch(pts, 1.0, 1.0);
    CHECK(NearlyEqual(origin.x, pts[0].x) && NearlyEqual(origin.y, pts[0].y));
    CHECK(NearlyEqual(far.x, pts[15].x) && NearlyEqual(far.y, pts[15].y));
    CHECK(AllFinite(EvaluatePatch(pts, 0.5, 0.5)));
}

// A default surface carries no deformation (zero bend/roll/curl/twist amounts),
// so ApplySurfaceDeform must leave the sample point unchanged: the zero-input
// contract the deformation math should always honor.
void TestDeformIdentityWhenNoDeform() {
    const char* test_name = "DeformIdentity";
    SurfaceData surface{};
    InitializeFlatSurface(surface, 1, 200.0, 100.0, false);
    Point3 point{50.0, 25.0, 0.0};
    const Point3 original = point;
    ApplySurfaceDeform(point, surface, 0.5, 0.5, 100.0, 50.0, 0.0, 200.0, 100.0);
    CHECK(NearlyEqual(point.x, original.x));
    CHECK(NearlyEqual(point.y, original.y));
    CHECK(NearlyEqual(point.z, original.z));
}

// Under an extreme bend the result must still be finite (no NaN/Inf leaking into
// the rasterizer). Guards the near-degenerate-angle behavior of the arc math.
void TestDeformStaysFiniteUnderExtremeBend() {
    const char* test_name = "DeformFinite";
    SurfaceData surface{};
    InitializeFlatSurface(surface, 1, 200.0, 100.0, false);
    surface.bend_x = 180.0F;
    surface.bend_y = -180.0F;
    surface.roll_angle = 90.0F;
    for (double u = 0.0; u <= 1.0; u += 0.25) {
        for (double v = 0.0; v <= 1.0; v += 0.25) {
            Point3 point{u * 200.0, v * 100.0, 0.0};
            ApplySurfaceDeform(point, surface, u, v, 100.0, 50.0, 0.0, 200.0, 100.0);
            CHECK(AllFinite(point));
        }
    }
}

void TestValidatorRejectsDuplicateBanks() {
    const char* test_name = "ValidatorDuplicateBanks";
    SceneData scene{};
    InitializeScene(scene, 100.0, 100.0);
    scene.surface_count = 2;
    scene.surfaces[1] = scene.surfaces[0];
    // Both surfaces now share animation_bank 0 -> must be rejected.
    CHECK(!IsValidScene(scene));
}

void TestValidatorRejectsNonFiniteGeometry() {
    const char* test_name = "ValidatorNonFiniteGeometry";
    SceneData scene{};

    InitializeScene(scene, 100.0, 100.0);
    scene.surfaces[0].control_points[3].x =
        std::numeric_limits<float>::quiet_NaN();
    CHECK(!IsValidScene(scene));

    InitializeScene(scene, 100.0, 100.0);
    scene.surfaces[0].rotation_y =
        std::numeric_limits<float>::infinity();
    CHECK(!IsValidScene(scene));

    InitializeScene(scene, 100.0, 100.0);
    scene.surfaces[0].size_x =
        -std::numeric_limits<float>::infinity();
    CHECK(!IsValidScene(scene));

    InitializeScene(scene, 100.0, 100.0);
    scene.surfaces[0].position_z =
        std::numeric_limits<float>::quiet_NaN();
    CHECK(!IsValidScene(scene));

    InitializeScene(scene, 100.0, 100.0);
    scene.surfaces[0].scale_z =
        std::numeric_limits<float>::infinity();
    CHECK(!IsValidScene(scene));
}

void TestResolveDivisionsClampsAtUseSite() {
    const char* test_name = "ResolveDivisionsClamp";
    CHECK(ResolveDivisions(0, 1) == kMinimumDivisions);
    CHECK(ResolveDivisions(0, 99) == kMaximumDivisions);
    CHECK(ResolveDivisions(1, 12) == kMinimumDivisions);
    CHECK(ResolveDivisions(99, 12) == kMaximumDivisions);
    CHECK(ResolveDivisions(24, 12) == 24);
}

}  // namespace

int main() {
    std::printf("Running SurfaceLab model tests...\n");
    TestInitializeScene();
    TestMigrateV1PreservesGeometryAndValidates();
    TestMigrateV2PreservesScale();
    TestMigrateV3PreservesDivisions();
    TestMigrateV4PreservesOpacity();
    TestMigrateV5PreservesThickness();
    TestMigrateV6PreservesMaterial();
    TestMigrateV7PreservesDeform();
    TestMigrateV8PreservesCornerCurls();
    TestMigrateV9PreservesDeformStreams();
    TestMigrateV12PreservesBackSlot();
    TestMigrateV10AndV11Layout();
    TestEvaluatePatchCorners();
    TestDeformIdentityWhenNoDeform();
    TestDeformStaysFiniteUnderExtremeBend();
    TestValidatorRejectsDuplicateBanks();
    TestValidatorRejectsNonFiniteGeometry();
    TestResolveDivisionsClampsAtUseSite();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
