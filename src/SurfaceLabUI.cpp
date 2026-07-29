#include "SurfaceLabUI.h"

#include "AEGP_SuiteHandler.h"
#include "AEFX_SuiteHelper.h"
#include "SurfaceLabMetal.h"
#include "SurfaceLabRender.h"
#include <adobesdk/DrawbotSuite.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace {

PF_Err RunBundledScript(
    PF_InData* in_data,
    PF_OutData* out_data,
    const char* resource_name) {
#if defined(__APPLE__)
    if (!in_data || !out_data || !resource_name) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }
    auto* global = reinterpret_cast<GlobalData*>(
        in_data->global_data);
    if (!global) {
        return PF_Err_INTERNAL_STRUCT_DAMAGED;
    }
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    A_Boolean scripting_available = FALSE;
    A_Err error =
        suites.UtilitySuite6()->AEGP_IsScriptingAvailable(
            &scripting_available);
    if (error != A_Err_NONE || !scripting_available) {
        std::snprintf(
            out_data->return_msg,
            sizeof(out_data->return_msg),
            "After Effects scripting is not available.");
        out_data->out_flags |= PF_OutFlag_DISPLAY_ERROR_MESSAGE;
        return PF_Err_NONE;
    }

    const CFBundleRef bundle = CFBundleGetBundleWithIdentifier(
        CFSTR("com.xinpak.surfacelab"));
    if (!bundle) {
        std::snprintf(
            out_data->return_msg,
            sizeof(out_data->return_msg),
            "SurfaceLab could not resolve its plug-in bundle.");
        out_data->out_flags |= PF_OutFlag_DISPLAY_ERROR_MESSAGE;
        return PF_Err_NONE;
    }
    const CFStringRef name = CFStringCreateWithCString(
        kCFAllocatorDefault,
        resource_name,
        kCFStringEncodingUTF8);
    const CFURLRef url = name
        ? CFBundleCopyResourceURL(
              bundle,
              name,
              CFSTR("jsx"),
              nullptr)
        : nullptr;
    if (name) {
        CFRelease(name);
    }
    CFDataRef data = nullptr;
    if (url) {
        SInt32 resource_error = 0;
        CFURLCreateDataAndPropertiesFromResource(
            kCFAllocatorDefault,
            url,
            &data,
            nullptr,
            nullptr,
            &resource_error);
    }
    if (url) {
        CFRelease(url);
    }
    if (!data) {
        suites.UtilitySuite6()->AEGP_ReportInfo(
            global->plugin_id,
            "SurfaceLab could not load its bundled Null Rig script.");
        return PF_Err_NONE;
    }
    std::vector<A_char> script(
        static_cast<std::size_t>(CFDataGetLength(data)) + 1U,
        '\0');
    CFDataGetBytes(
        data,
        CFRangeMake(0, CFDataGetLength(data)),
        reinterpret_cast<UInt8*>(script.data()));
    CFRelease(data);
    error = suites.UtilitySuite6()->AEGP_ExecuteScript(
        global->plugin_id,
        script.data(),
        FALSE,
        nullptr,
        nullptr);
    if (error != A_Err_NONE) {
        std::snprintf(
            out_data->return_msg,
            sizeof(out_data->return_msg),
            "After Effects could not schedule the SurfaceLab Null Rig script.");
        out_data->out_flags |= PF_OutFlag_DISPLAY_ERROR_MESSAGE;
    }
    return PF_Err_NONE;
#else
    (void)in_data;
    (void)out_data;
    (void)resource_name;
    return PF_Err_NONE;
#endif
}

PF_Err AddPoint3D(
    PF_InData* in_data,
    PF_ParamDef& def,
    const char* name,
    double x,
    double y,
    double z,
    A_long disk_id,
    PF_ParamUIFlags ui_flags = PF_PUI_NONE,
    PF_ParamFlags flags = PF_ParamFlag_NONE) {
    AEFX_CLR_STRUCT(def);
    def.param_type = PF_Param_POINT_3D;
    PF_STRNNCPY(def.PF_DEF_NAME, name, sizeof(def.PF_DEF_NAME));
    def.u.point3d_d.x_value = def.u.point3d_d.x_dephault = x;
    def.u.point3d_d.y_value = def.u.point3d_d.y_dephault = y;
    def.u.point3d_d.z_value = def.u.point3d_d.z_dephault = z;
    def.uu.id = disk_id;
    def.ui_flags = ui_flags;
    def.flags = flags;
    return PF_ADD_PARAM(in_data, -1, &def);
}

bool SurfaceAndOffset(
    PF_ParamIndex parameter,
    std::uint32_t& surface,
    PF_ParamIndex& offset) {
    if (parameter < kParamSurfaceParametersStart ||
        parameter >= kParamSurfacesEnd) {
        return false;
    }
    const PF_ParamIndex relative =
        parameter - kParamSurfaceParametersStart;
    surface = static_cast<std::uint32_t>(
        relative / kSurfaceParameterStride);
    offset = relative % kSurfaceParameterStride;
    return surface < kSurfaceCount;
}

bool LayerPointToFrame(
    PF_InData* in_data,
    PF_EventExtra* event_extra,
    Point2 layer_point,
    Point2& frame_point) {
    PF_FixedPoint point{
        FLOAT2FIX(layer_point.x),
        FLOAT2FIX(layer_point.y)};
    if (event_extra->cbs.layer_to_comp(
            event_extra->cbs.refcon,
            event_extra->contextH,
            in_data->current_time,
            in_data->time_scale,
            &point) != PF_Err_NONE ||
        event_extra->cbs.source_to_frame(
            event_extra->cbs.refcon,
            event_extra->contextH,
            &point) != PF_Err_NONE) {
        return false;
    }
    frame_point = {FIX_2_FLOAT(point.x), FIX_2_FLOAT(point.y)};
    return std::isfinite(frame_point.x) &&
           std::isfinite(frame_point.y);
}

bool ProjectSurfacePointToFrame(
    PF_InData* in_data,
    PF_EventExtra* event_extra,
    const SurfaceData& surface,
    const CameraState& camera,
    double u,
    double v,
    Point2& frame_point) {
    const SurfaceEvaluationState evaluation =
        BuildSurfaceEvaluationState(
            surface,
            camera,
            1.0,
            1.0,
            1.0);
    const Point3 world = EvaluateTransformedPoint(
        surface,
        evaluation,
        u,
        v);
    const Vertex projected = ProjectVertex(
        world,
        {0.0, 0.0, -1.0},
        u,
        v,
        camera);
    return projected.visible &&
           LayerPointToFrame(
               in_data,
               event_extra,
               {projected.x, projected.y},
               frame_point);
}

CameraState BuildGizmoCamera(
    PF_InData* in_data,
    PF_ParamDef* params[],
    A_long width,
    A_long height) {
    return BuildResolvedCameraState(
        in_data,
        params,
        width * 0.5,
        height * 0.5,
        0.0,
        0.0,
        1.0,
        1.0,
        1.0);
}

// Process-local Comp selection. Not persisted; matches the existing gizmo
// drag memory model. Selection stays on one surface so multi-drag can share
// one screen→local Jacobian without cross-surface ambiguity.
struct LatticePointRef {
    std::uint32_t surface{};
    std::uint16_t row{};
    std::uint16_t column{};
};

enum class SelectionEntityKind {
    Vertex,
    EdgeHorizontal,
    EdgeVertical,
    Face,
    Surface,
    Row,
    Column
};

struct SelectionEntityRef {
    std::uint32_t surface{};
    SelectionEntityKind kind{SelectionEntityKind::Vertex};
    std::uint16_t row{};
    std::uint16_t column{};
};

enum class LatticeLineKind {
    Row,
    Column
};

struct LatticeLineRef {
    std::uint32_t surface{};
    LatticeLineKind kind{LatticeLineKind::Row};
    std::uint16_t index{};
};

enum class TranslateAxis {
    None,
    X,
    Y,
    Z
};

enum class RotateAxis {
    None,
    X,
    Y,
    Z
};

struct GizmoSelectionState {
    std::vector<SelectionEntityRef> entities{};
    std::vector<LatticePointRef> points{};
    bool dragging{};
    bool drag_moved{};
    bool has_snapshot{};
    // First DRAG sample after DO_CLICK only seeds the origin; AE can emit a
    // large coordinate jump between those two events.
    bool drag_origin_seeded{};
    bool marquee_active{};
    bool marquee_additive{};
    Point2 marquee_start{};
    Point2 marquee_end{};
    LatticePointRef primary{};
    Point2 mouse_down{};
    Point2 last_mouse{};
    // Translate gizmo: axis drag moves the whole selection in lattice space.
    TranslateAxis axis_drag{TranslateAxis::None};
    RotateAxis rotate_axis_drag{RotateAxis::None};
    bool uniform_scale_drag{};
    TranslateAxis axis_hover{TranslateAxis::None};
    RotateAxis rotate_axis_hover{RotateAxis::None};
    bool uniform_scale_hover{};
    A_long transform_tool_drag{kTransformToolMove};
    A_long transform_space_drag{kTransformSpaceLocal};
    Point3 selection_centroid{};
    Point3 centroid_down{};
    // Absolute drag base: every DRAG frame restores this then applies total
    // delta from mouse_down so bad Jacobians cannot compound.
    LatticeData drag_snapshot{};
};

GizmoSelectionState g_selection;

constexpr double kTranslateAxisPixels = 68.0;
constexpr double kTranslateAxisHitPixels = 12.0;
// Reject axes that project to nearly a point (depth-parallel / edge-on).
constexpr double kMinAxisPixelsPerUnit = 0.25;
// |det(J)| / (|col0|*|col1|) = |sin theta|; reject near-parallel cage axes.
constexpr double kMinJacobianSinAngle = 0.08;
// Ignore one-frame pointer teleports mid-drag (comp/frame space glitches).
constexpr double kMaxDragStepPixels = 64.0;
// Hard cap on total cage-space write from a single gesture.
constexpr double kMaxCageDelta = 500.0;

bool SameLatticePoint(const LatticePointRef& a, const LatticePointRef& b) {
    return a.surface == b.surface &&
           a.row == b.row &&
           a.column == b.column;
}

bool SelectionContains(const LatticePointRef& point) {
    return std::any_of(
        g_selection.points.begin(),
        g_selection.points.end(),
        [&](const LatticePointRef& candidate) {
            return SameLatticePoint(candidate, point);
        });
}

void ClearSelection() {
    g_selection.entities.clear();
    g_selection.points.clear();
    g_selection.dragging = false;
    g_selection.drag_moved = false;
    g_selection.has_snapshot = false;
    g_selection.drag_origin_seeded = false;
    g_selection.marquee_active = false;
    g_selection.marquee_additive = false;
    g_selection.primary = {};
    g_selection.axis_drag = TranslateAxis::None;
    g_selection.rotate_axis_drag = RotateAxis::None;
    g_selection.uniform_scale_drag = false;
    g_selection.axis_hover = TranslateAxis::None;
    g_selection.rotate_axis_hover = RotateAxis::None;
    g_selection.uniform_scale_hover = false;
    g_selection.transform_tool_drag = kTransformToolMove;
    g_selection.transform_space_drag = kTransformSpaceLocal;
    g_selection.selection_centroid = {};
    g_selection.centroid_down = {};
    g_selection.drag_snapshot = {};
}

// AE only sends PF_Event_DRAG after DO_CLICK if send_drag is set. Without
// this the gizmo highlights on click but never receives mouse motion.
void BeginCompDrag(PF_EventExtra* event_extra) {
    if (event_extra) {
        event_extra->u.do_click.send_drag = TRUE;
    }
    g_selection.drag_moved = false;
    g_selection.drag_origin_seeded = false;
}

bool InitializePendingLatticeForInput(
    PF_InData* in_data,
    PF_ParamDef* params[],
    std::uint32_t surface) {
    if (!in_data || !params || surface >= kSurfaceCount ||
        !params[kParamInput]) {
        return false;
    }
    const A_long input_width =
        params[kParamInput]->u.ld.width > 1
            ? params[kParamInput]->u.ld.width
            : in_data->width;
    const A_long input_height =
        params[kParamInput]->u.ld.height > 1
            ? params[kParamInput]->u.ld.height
            : in_data->height;
    if (input_width <= 1 || input_height <= 1) {
        return false;
    }
    PF_ParamDef* lattice_parameter =
        params[SurfaceLatticeParam(surface)];
    const PF_Handle handle = lattice_parameter->u.arb_d.value;
    if (!handle) {
        return false;
    }
    auto* lattice =
        static_cast<LatticeData*>(PF_LOCK_HANDLE(handle));
    if (!lattice || !IsValidLattice(*lattice)) {
        if (lattice) {
            PF_UNLOCK_HANDLE(handle);
        }
        return false;
    }
    if (!NeedsInputSizedInitialization(*lattice)) {
        PF_UNLOCK_HANDLE(handle);
        return false;
    }
    const std::uint64_t surface_id = lattice->surface_id;
    const std::uint16_t divisions_x = lattice->divisions_x;
    const std::uint16_t divisions_y = lattice->divisions_y;
    InitializeLattice(
        *lattice,
        divisions_x,
        divisions_y,
        input_width,
        input_height,
        surface_id);
    PF_UNLOCK_HANDLE(handle);
    lattice_parameter->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;

    PF_ParamDef* surface_position_parameter =
        params[SurfaceParam(surface, kSurfacePositionOffset)];
    PF_Point3DDef& surface_position =
        surface_position_parameter->u.point3d_d;
    surface_position.x_value = input_width * 0.5;
    surface_position.y_value = input_height * 0.5;
    surface_position.z_value = 0.0;
    surface_position_parameter->uu.change_flags |=
        PF_ChangeFlag_CHANGED_VALUE;

    PF_ParamDef* scene_position_parameter =
        params[kParamScenePosition];
    PF_Point3DDef& scene_position =
        scene_position_parameter->u.point3d_d;
    scene_position.x_value = input_width * 0.5;
    scene_position.y_value = input_height * 0.5;
    scene_position.z_value = 0.0;
    scene_position_parameter->uu.change_flags |=
        PF_ChangeFlag_CHANGED_VALUE;
    return true;
}

bool CaptureDragSnapshot(
    PF_InData* in_data,
    PF_ParamDef* params[],
    std::uint32_t surface,
    Point2 mouse_down,
    Point3 centroid_down) {
    if (!in_data || !params || surface >= kSurfaceCount) {
        g_selection.has_snapshot = false;
        return false;
    }
    InitializePendingLatticeForInput(
        in_data,
        params,
        surface);
    PF_Handle handle =
        params[SurfaceLatticeParam(surface)]->u.arb_d.value;
    if (!handle) {
        g_selection.has_snapshot = false;
        return false;
    }
    const auto* lattice =
        static_cast<const LatticeData*>(PF_LOCK_HANDLE(handle));
    if (!lattice || !IsValidLattice(*lattice) ||
        NeedsInputSizedInitialization(*lattice)) {
        if (lattice) {
            PF_UNLOCK_HANDLE(handle);
        }
        g_selection.has_snapshot = false;
        return false;
    }
    g_selection.drag_snapshot = *lattice;
    PF_UNLOCK_HANDLE(handle);
    g_selection.has_snapshot = true;
    g_selection.drag_origin_seeded = false;
    // Provisional origin; first DRAG sample re-seeds after AE's click→drag jump.
    g_selection.mouse_down = mouse_down;
    g_selection.last_mouse = mouse_down;
    g_selection.centroid_down = centroid_down;
    g_selection.selection_centroid = centroid_down;
    return true;
}

void EndCompDragIfFinished(PF_EventExtra* event_extra) {
    if (!event_extra || !event_extra->u.do_click.last_time) {
        return;
    }
    event_extra->u.do_click.send_drag = FALSE;
    g_selection.dragging = false;
    g_selection.drag_moved = false;
    g_selection.has_snapshot = false;
    g_selection.drag_origin_seeded = false;
    g_selection.marquee_active = false;
    g_selection.axis_drag = TranslateAxis::None;
    g_selection.rotate_axis_drag = RotateAxis::None;
    g_selection.uniform_scale_drag = false;
}

Point3 AxisUnit(TranslateAxis axis) {
    switch (axis) {
        case TranslateAxis::X:
            return {1.0, 0.0, 0.0};
        case TranslateAxis::Y:
            return {0.0, 1.0, 0.0};
        case TranslateAxis::Z:
            return {0.0, 0.0, 1.0};
        case TranslateAxis::None:
        default:
            return {};
    }
}

Point3 AxisUnit(RotateAxis axis) {
    switch (axis) {
        case RotateAxis::X:
            return {1.0, 0.0, 0.0};
        case RotateAxis::Y:
            return {0.0, 1.0, 0.0};
        case RotateAxis::Z:
            return {0.0, 0.0, 1.0};
        case RotateAxis::None:
        default:
            return {};
    }
}

void RotationPlaneBasis(
    RotateAxis axis,
    Point3& first,
    Point3& second) {
    switch (axis) {
        case RotateAxis::X:
            first = {0.0, 1.0, 0.0};
            second = {0.0, 0.0, 1.0};
            break;
        case RotateAxis::Y:
            first = {1.0, 0.0, 0.0};
            second = {0.0, 0.0, 1.0};
            break;
        case RotateAxis::Z:
            first = {1.0, 0.0, 0.0};
            second = {0.0, 1.0, 0.0};
            break;
        case RotateAxis::None:
        default:
            first = {};
            second = {};
            break;
    }
}

double PointSegmentDistanceSquared(
    Point2 point,
    Point2 a,
    Point2 b) {
    const double abx = b.x - a.x;
    const double aby = b.y - a.y;
    const double apx = point.x - a.x;
    const double apy = point.y - a.y;
    const double ab_length_squared = abx * abx + aby * aby;
    if (ab_length_squared <= 1.0e-12) {
        return apx * apx + apy * apy;
    }
    const double t = std::clamp(
        (apx * abx + apy * aby) / ab_length_squared,
        0.0,
        1.0);
    const double dx = point.x - (a.x + abx * t);
    const double dy = point.y - (a.y + aby * t);
    return dx * dx + dy * dy;
}

bool ComputeSelectionCentroid(
    const SceneData& scene,
    Point3& centroid) {
    if (g_selection.points.empty()) {
        return false;
    }
    const std::uint32_t surface_index = g_selection.points.front().surface;
    if (surface_index >= scene.surface_count) {
        return false;
    }
    const SurfaceData& surface = scene.surfaces[surface_index];
    if (!IsValidLattice(surface.lattice)) {
        return false;
    }
    Point3 sum{};
    std::size_t count = 0;
    for (const LatticePointRef& ref : g_selection.points) {
        if (ref.surface != surface_index) {
            continue;
        }
        const std::size_t point_index = LatticePointIndex(
            surface.lattice.divisions_x,
            ref.row,
            ref.column);
        if (point_index >= surface.lattice.point_count) {
            continue;
        }
        const StoredPoint3& point = surface.lattice.points[point_index];
        sum.x += point.x;
        sum.y += point.y;
        sum.z += point.z;
        ++count;
    }
    if (count == 0) {
        return false;
    }
    centroid = {
        sum.x / static_cast<double>(count),
        sum.y / static_cast<double>(count),
        sum.z / static_cast<double>(count)};
    return true;
}

// Build the affine portion of the raw lattice -> AE world transform. The
// procedural Roll layer is intentionally excluded: Transform Space describes
// the editable cage frame, while Roll remains a later evaluation layer.
bool BuildCageToWorldTransform(
    const SurfaceData& surface,
    const CameraState& camera,
    Affine3D& cage_to_world) {
    if (!IsValidLattice(surface.lattice)) {
        return false;
    }
    const SurfaceEvaluationState state = BuildSurfaceEvaluationState(
        surface,
        camera,
        1.0,
        1.0,
        1.0);
    const StoredPoint3& raw_zero = surface.lattice.points[0];
    const StoredPoint3& evaluated_zero = state.lattice.points[0];
    const Point3 recenter_offset{
        static_cast<double>(evaluated_zero.x) - raw_zero.x,
        static_cast<double>(evaluated_zero.y) - raw_zero.y,
        static_cast<double>(evaluated_zero.z) - raw_zero.z};
    const auto map = [&](Point3 raw) {
        Point3 pre_scene{
            raw.x + recenter_offset.x,
            raw.y + recenter_offset.y,
            raw.z + recenter_offset.z};
        pre_scene = ScaleSurfaceCagePoint(
            pre_scene,
            state.coordinate_transform);
        pre_scene = RotateSurfaceWorldPoint(
            pre_scene,
            state.coordinate_transform);
        if (state.root_transform_enabled) {
            pre_scene = ApplyAffine3D(
                state.root_pre_scene_transform,
                pre_scene);
        }
        return ApplyScenePointTransform(
            pre_scene,
            camera.scene_transform);
    };
    const Point3 origin = map({0.0, 0.0, 0.0});
    const Point3 x_axis = map({1.0, 0.0, 0.0});
    const Point3 y_axis = map({0.0, 1.0, 0.0});
    const Point3 z_axis = map({0.0, 0.0, 1.0});
    const double values[] = {
        origin.x, origin.y, origin.z,
        x_axis.x, x_axis.y, x_axis.z,
        y_axis.x, y_axis.y, y_axis.z,
        z_axis.x, z_axis.y, z_axis.z};
    for (double value : values) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    cage_to_world = {
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

bool ProjectWorldPointToFrame(
    PF_InData* in_data,
    PF_EventExtra* event_extra,
    const CameraState& camera,
    Point3 world,
    Point2& frame_point) {
    Point3 pre_scene;
    if (!TryInverseScenePointTransform(
            world,
            camera.scene_transform,
            pre_scene)) {
        return false;
    }
    const Vertex projected = ProjectVertex(
        pre_scene,
        {0.0, 0.0, -1.0},
        0.0,
        0.0,
        camera);
    return projected.visible &&
           LayerPointToFrame(
               in_data,
               event_extra,
               {projected.x, projected.y},
               frame_point);
}

// Project a raw cage-local lattice coordinate through the affine cage,
// optional Surface Root, Scene transform, and active AE camera.
bool ProjectCageLocalPointToFrame(
    PF_InData* in_data,
    PF_EventExtra* event_extra,
    const SurfaceData& surface,
    const CameraState& camera,
    Point3 local,
    Point2& frame_point) {
    Affine3D cage_to_world;
    return BuildCageToWorldTransform(
               surface,
               camera,
               cage_to_world) &&
           ProjectWorldPointToFrame(
               in_data,
               event_extra,
               camera,
               ApplyAffine3D(cage_to_world, local),
               frame_point);
}

bool ProjectSelectionCentroidToFrame(
    PF_InData* in_data,
    PF_EventExtra* event_extra,
    const SceneData& scene,
    const CameraState& camera,
    Point3 centroid,
    Point2& frame_point) {
    if (g_selection.points.empty()) {
        return false;
    }
    const std::uint32_t surface_index = g_selection.points.front().surface;
    if (surface_index >= scene.surface_count) {
        return false;
    }
    return ProjectCageLocalPointToFrame(
        in_data,
        event_extra,
        scene.surfaces[surface_index],
        camera,
        centroid,
        frame_point);
}

bool BuildTranslateAxisScreen(
    PF_InData* in_data,
    PF_EventExtra* event_extra,
    const SceneData& scene,
    const CameraState& camera,
    Point3 centroid,
    TranslateAxis axis,
    A_long transform_space,
    Point2& origin,
    Point2& tip,
    double& pixels_per_unit) {
    if (g_selection.points.empty()) {
        return false;
    }
    const std::uint32_t surface_index =
        g_selection.points.front().surface;
    if (surface_index >= scene.surface_count) {
        return false;
    }
    Affine3D cage_to_world;
    if (!BuildCageToWorldTransform(
            scene.surfaces[surface_index],
            camera,
            cage_to_world)) {
        return false;
    }
    const Point3 world_origin =
        ApplyAffine3D(cage_to_world, centroid);
    if (!ProjectWorldPointToFrame(
            in_data,
            event_extra,
            camera,
            world_origin,
            origin)) {
        return false;
    }
    const Point3 unit = AxisUnit(axis);
    // Probe far enough for a stable screen direction on shallow projections.
    constexpr double kProbe = 32.0;
    const Point3 world_probe =
        transform_space == kTransformSpaceWorld
            ? Point3{
                  world_origin.x + unit.x * kProbe,
                  world_origin.y + unit.y * kProbe,
                  world_origin.z + unit.z * kProbe}
            : ApplyAffine3D(
                  cage_to_world,
                  {centroid.x + unit.x * kProbe,
                   centroid.y + unit.y * kProbe,
                   centroid.z + unit.z * kProbe});
    Point2 probed;
    if (!ProjectWorldPointToFrame(
            in_data,
            event_extra,
            camera,
            world_probe,
            probed)) {
        return false;
    }
    const double dx = probed.x - origin.x;
    const double dy = probed.y - origin.y;
    const double length = std::sqrt(dx * dx + dy * dy);
    if (length <= 1.0e-6) {
        return false;
    }
    pixels_per_unit = length / kProbe;
    // Edge-on / depth-parallel axes make cage deltas explode (1/pixels_per_unit).
    if (pixels_per_unit < kMinAxisPixelsPerUnit) {
        return false;
    }
    const double scale = kTranslateAxisPixels / length;
    tip = {origin.x + dx * scale, origin.y + dy * scale};
    return true;
}

TranslateAxis HitTestTranslateAxes(
    PF_InData* in_data,
    PF_EventExtra* event_extra,
    const SceneData& scene,
    const CameraState& camera,
    Point3 centroid,
    A_long transform_space,
    Point2 mouse) {
    const double hit_radius_squared =
        kTranslateAxisHitPixels * kTranslateAxisHitPixels;
    double best = hit_radius_squared;
    TranslateAxis best_axis = TranslateAxis::None;
    for (TranslateAxis axis :
         {TranslateAxis::X, TranslateAxis::Y, TranslateAxis::Z}) {
        Point2 origin;
        Point2 tip;
        double pixels_per_unit = 0.0;
        if (!BuildTranslateAxisScreen(
                in_data,
                event_extra,
                scene,
                camera,
                centroid,
                axis,
                transform_space,
                origin,
                tip,
                pixels_per_unit)) {
            continue;
        }
        const double distance =
            PointSegmentDistanceSquared(mouse, origin, tip);
        if (distance <= best) {
            best = distance;
            best_axis = axis;
        }
    }
    return best_axis;
}

bool BuildRotateRingScreen(
    PF_InData* in_data,
    PF_EventExtra* event_extra,
    const SceneData& scene,
    const CameraState& camera,
    Point3 centroid,
    RotateAxis axis,
    A_long transform_space,
    std::vector<Point2>& ring) {
    if (g_selection.points.empty()) {
        return false;
    }
    const std::uint32_t surface_index =
        g_selection.points.front().surface;
    if (surface_index >= scene.surface_count) {
        return false;
    }
    Affine3D cage_to_world;
    if (!BuildCageToWorldTransform(
            scene.surfaces[surface_index],
            camera,
            cage_to_world)) {
        return false;
    }
    const Point3 world_origin =
        ApplyAffine3D(cage_to_world, centroid);
    Point2 origin;
    if (!ProjectWorldPointToFrame(
            in_data,
            event_extra,
            camera,
            world_origin,
            origin)) {
        return false;
    }
    Point3 basis_a;
    Point3 basis_b;
    RotationPlaneBasis(axis, basis_a, basis_b);
    constexpr double kProbe = 32.0;
    const auto basis_probe = [&](Point3 basis) {
        return transform_space == kTransformSpaceWorld
                   ? Point3{
                         world_origin.x + basis.x * kProbe,
                         world_origin.y + basis.y * kProbe,
                         world_origin.z + basis.z * kProbe}
                   : ApplyAffine3D(
                         cage_to_world,
                         {centroid.x + basis.x * kProbe,
                          centroid.y + basis.y * kProbe,
                          centroid.z + basis.z * kProbe});
    };
    Point2 projected_a;
    Point2 projected_b;
    if (!ProjectWorldPointToFrame(
            in_data,
            event_extra,
            camera,
            basis_probe(basis_a),
            projected_a) ||
        !ProjectWorldPointToFrame(
            in_data,
            event_extra,
            camera,
            basis_probe(basis_b),
            projected_b)) {
        return false;
    }
    const double pixels_a = std::hypot(
        projected_a.x - origin.x,
        projected_a.y - origin.y) /
        kProbe;
    const double pixels_b = std::hypot(
        projected_b.x - origin.x,
        projected_b.y - origin.y) /
        kProbe;
    const double average_pixels =
        (pixels_a + pixels_b) * 0.5;
    if (!std::isfinite(average_pixels) ||
        average_pixels < 0.05) {
        return false;
    }
    constexpr double kRingPixels = 54.0;
    const double radius = std::clamp(
        kRingPixels / average_pixels,
        1.0,
        2000.0);
    constexpr int kRingSegments = 64;
    ring.clear();
    ring.reserve(kRingSegments + 1);
    for (int segment = 0; segment <= kRingSegments; ++segment) {
        const double angle =
            static_cast<double>(segment) /
            kRingSegments *
            6.28318530717958647692;
        const double cosine = std::cos(angle);
        const double sine = std::sin(angle);
        const Point3 plane{
            basis_a.x * cosine + basis_b.x * sine,
            basis_a.y * cosine + basis_b.y * sine,
            basis_a.z * cosine + basis_b.z * sine};
        const Point3 world_ring_point =
            transform_space == kTransformSpaceWorld
                ? Point3{
                      world_origin.x + radius * plane.x,
                      world_origin.y + radius * plane.y,
                      world_origin.z + radius * plane.z}
                : ApplyAffine3D(
                      cage_to_world,
                      {centroid.x + radius * plane.x,
                       centroid.y + radius * plane.y,
                       centroid.z + radius * plane.z});
        Point2 projected;
        if (!ProjectWorldPointToFrame(
                in_data,
                event_extra,
                camera,
                world_ring_point,
                projected)) {
            return false;
        }
        ring.push_back(projected);
    }
    return ring.size() > 1;
}

RotateAxis HitTestRotateRings(
    PF_InData* in_data,
    PF_EventExtra* event_extra,
    const SceneData& scene,
    const CameraState& camera,
    Point3 centroid,
    A_long transform_space,
    Point2 mouse) {
    constexpr double kHitRadiusSquared = 100.0;
    double closest = kHitRadiusSquared;
    RotateAxis best = RotateAxis::None;
    for (RotateAxis axis :
         {RotateAxis::X, RotateAxis::Y, RotateAxis::Z}) {
        std::vector<Point2> ring;
        if (!BuildRotateRingScreen(
                in_data,
                event_extra,
                scene,
                camera,
                centroid,
                axis,
                transform_space,
                ring)) {
            continue;
        }
        for (std::size_t index = 1; index < ring.size(); ++index) {
            const double distance = PointSegmentDistanceSquared(
                mouse,
                ring[index - 1],
                ring[index]);
            if (distance <= closest) {
                closest = distance;
                best = axis;
            }
        }
    }
    return best;
}

bool BuildUniformScaleHandle(
    PF_InData* in_data,
    PF_EventExtra* event_extra,
    const SceneData& scene,
    const CameraState& camera,
    Point3 centroid,
    Point2& origin,
    Point2& tip) {
    if (!ProjectSelectionCentroidToFrame(
            in_data,
            event_extra,
            scene,
            camera,
            centroid,
            origin)) {
        return false;
    }
    tip = {origin.x + 42.0, origin.y - 42.0};
    return true;
}

bool HitTestUniformScaleHandle(
    PF_InData* in_data,
    PF_EventExtra* event_extra,
    const SceneData& scene,
    const CameraState& camera,
    Point3 centroid,
    Point2 mouse) {
    Point2 origin;
    Point2 tip;
    if (!BuildUniformScaleHandle(
            in_data,
            event_extra,
            scene,
            camera,
            centroid,
            origin,
            tip)) {
        return false;
    }
    const double dx = mouse.x - tip.x;
    const double dy = mouse.y - tip.y;
    return dx * dx + dy * dy <= 100.0;
}

bool SameSelectionEntity(
    const SelectionEntityRef& a,
    const SelectionEntityRef& b) {
    return a.surface == b.surface &&
           a.kind == b.kind &&
           a.row == b.row &&
           a.column == b.column;
}

std::vector<LatticePointRef> CollectEntityPoints(
    const SceneData& scene,
    const NullPointOverrideState& null_overrides,
    const SelectionEntityRef& entity) {
    std::vector<LatticePointRef> points;
    if (entity.surface >= scene.surface_count) {
        return points;
    }
    const SurfaceData& surface = scene.surfaces[entity.surface];
    if (surface.enabled == 0 || !IsValidLattice(surface.lattice)) {
        return points;
    }
    const std::uint16_t divisions_x = surface.lattice.divisions_x;
    const std::uint16_t divisions_y = surface.lattice.divisions_y;
    const auto add = [&](std::uint16_t row, std::uint16_t column) {
        if (row > divisions_y || column > divisions_x) {
            return;
        }
        const std::size_t point_index = LatticePointIndex(
            divisions_x,
            row,
            column);
        if (null_overrides.IsControlled(entity.surface, point_index)) {
            return;
        }
        points.push_back({entity.surface, row, column});
    };
    switch (entity.kind) {
        case SelectionEntityKind::Vertex:
            add(entity.row, entity.column);
            break;
        case SelectionEntityKind::EdgeHorizontal:
            if (entity.column < divisions_x) {
                add(entity.row, entity.column);
                add(entity.row, entity.column + 1);
            }
            break;
        case SelectionEntityKind::EdgeVertical:
            if (entity.row < divisions_y) {
                add(entity.row, entity.column);
                add(entity.row + 1, entity.column);
            }
            break;
        case SelectionEntityKind::Face:
            if (entity.row < divisions_y &&
                entity.column < divisions_x) {
                add(entity.row, entity.column);
                add(entity.row, entity.column + 1);
                add(entity.row + 1, entity.column);
                add(entity.row + 1, entity.column + 1);
            }
            break;
        case SelectionEntityKind::Surface:
            for (std::uint16_t row = 0; row <= divisions_y; ++row) {
                for (std::uint16_t column = 0;
                     column <= divisions_x;
                     ++column) {
                    add(row, column);
                }
            }
            break;
        case SelectionEntityKind::Row:
            if (entity.row <= divisions_y) {
                for (std::uint16_t column = 0;
                     column <= divisions_x;
                     ++column) {
                    add(entity.row, column);
                }
            }
            break;
        case SelectionEntityKind::Column:
            if (entity.column <= divisions_x) {
                for (std::uint16_t row = 0;
                     row <= divisions_y;
                     ++row) {
                    add(row, entity.column);
                }
            }
            break;
    }
    return points;
}

void RebuildSelectionPoints(
    const SceneData& scene,
    const NullPointOverrideState& null_overrides) {
    std::vector<LatticePointRef> points;
    for (const SelectionEntityRef& entity : g_selection.entities) {
        const std::vector<LatticePointRef> entity_points =
            CollectEntityPoints(scene, null_overrides, entity);
        for (const LatticePointRef& point : entity_points) {
            const bool duplicate = std::any_of(
                points.begin(),
                points.end(),
                [&](const LatticePointRef& candidate) {
                    return SameLatticePoint(candidate, point);
                });
            if (!duplicate) {
                points.push_back(point);
            }
        }
    }
    g_selection.points = std::move(points);
    g_selection.primary =
        g_selection.points.empty() ? LatticePointRef{}
                                   : g_selection.points.front();
}

void SetSelectionEntity(
    const SceneData& scene,
    const NullPointOverrideState& null_overrides,
    const SelectionEntityRef& entity) {
    g_selection.entities = {entity};
    RebuildSelectionPoints(scene, null_overrides);
    g_selection.dragging = false;
}

void ToggleSelectionEntity(
    const SceneData& scene,
    const NullPointOverrideState& null_overrides,
    const SelectionEntityRef& entity) {
    if (!g_selection.entities.empty() &&
        g_selection.entities.front().surface != entity.surface) {
        SetSelectionEntity(scene, null_overrides, entity);
        return;
    }
    const auto existing = std::find_if(
        g_selection.entities.begin(),
        g_selection.entities.end(),
        [&](const SelectionEntityRef& candidate) {
            return SameSelectionEntity(candidate, entity);
        });
    if (existing == g_selection.entities.end()) {
        g_selection.entities.push_back(entity);
    } else {
        g_selection.entities.erase(existing);
    }
    RebuildSelectionPoints(scene, null_overrides);
    g_selection.dragging = false;
}

void SetSelectionPoints(const std::vector<LatticePointRef>& points) {
    g_selection.entities.clear();
    g_selection.points = points;
    g_selection.primary =
        points.empty() ? LatticePointRef{} : points.front();
    g_selection.dragging = false;
}

void SetSelection(const LatticePointRef& point) {
    SetSelectionPoints({point});
}

void AddPointToSelection(const LatticePointRef& point) {
    if (!g_selection.points.empty() &&
        g_selection.points.front().surface != point.surface) {
        SetSelection(point);
        return;
    }
    if (!SelectionContains(point)) {
        g_selection.points.push_back(point);
    }
    g_selection.primary = point;
}

void MergePointsIntoSelection(const std::vector<LatticePointRef>& points) {
    for (const LatticePointRef& point : points) {
        AddPointToSelection(point);
    }
}

bool HitTestLatticeLine(
    PF_InData* in_data,
    PF_EventExtra* event_extra,
    const SceneData& scene,
    const CameraState& camera,
    Point2 mouse,
    double max_distance_squared,
    LatticeLineRef& hit) {
    constexpr int kCurveSamples = 24;
    double closest = max_distance_squared;
    bool found = false;
    for (std::uint32_t surface_index = 0;
         surface_index < scene.surface_count;
         ++surface_index) {
        const SurfaceData& surface = scene.surfaces[surface_index];
        if (surface.enabled == 0 || !IsValidLattice(surface.lattice)) {
            continue;
        }
        for (std::uint16_t row = 0;
             row <= surface.lattice.divisions_y;
             ++row) {
            Point2 previous{};
            bool have_previous = false;
            for (int sample = 0; sample <= kCurveSamples; ++sample) {
                Point2 projected;
                if (!ProjectSurfacePointToFrame(
                        in_data,
                        event_extra,
                        surface,
                        camera,
                        static_cast<double>(sample) / kCurveSamples,
                        static_cast<double>(row) /
                            surface.lattice.divisions_y,
                        projected)) {
                    have_previous = false;
                    continue;
                }
                if (have_previous) {
                    const double distance = PointSegmentDistanceSquared(
                        mouse,
                        previous,
                        projected);
                    if (distance <= closest) {
                        closest = distance;
                        hit = {
                            surface_index,
                            LatticeLineKind::Row,
                            row};
                        found = true;
                    }
                }
                previous = projected;
                have_previous = true;
            }
        }
        for (std::uint16_t column = 0;
             column <= surface.lattice.divisions_x;
             ++column) {
            Point2 previous{};
            bool have_previous = false;
            for (int sample = 0; sample <= kCurveSamples; ++sample) {
                Point2 projected;
                if (!ProjectSurfacePointToFrame(
                        in_data,
                        event_extra,
                        surface,
                        camera,
                        static_cast<double>(column) /
                            surface.lattice.divisions_x,
                        static_cast<double>(sample) / kCurveSamples,
                        projected)) {
                    have_previous = false;
                    continue;
                }
                if (have_previous) {
                    const double distance = PointSegmentDistanceSquared(
                        mouse,
                        previous,
                        projected);
                    if (distance <= closest) {
                        closest = distance;
                        hit = {
                            surface_index,
                            LatticeLineKind::Column,
                            column};
                        found = true;
                    }
                }
                previous = projected;
                have_previous = true;
            }
        }
    }
    return found;
}

bool ProjectControlPointToFrame(
    PF_InData* in_data,
    PF_EventExtra* event_extra,
    const SurfaceData& surface,
    const CameraState& camera,
    std::uint16_t row,
    std::uint16_t column,
    Point2& projected) {
    if (row > surface.lattice.divisions_y ||
        column > surface.lattice.divisions_x) {
        return false;
    }
    return ProjectSurfacePointToFrame(
        in_data,
        event_extra,
        surface,
        camera,
        static_cast<double>(column) /
            surface.lattice.divisions_x,
        static_cast<double>(row) /
            surface.lattice.divisions_y,
        projected);
}

bool HitTestEdgeEntity(
    PF_InData* in_data,
    PF_EventExtra* event_extra,
    const SceneData& scene,
    const CameraState& camera,
    Point2 mouse,
    double max_distance_squared,
    SelectionEntityRef& hit) {
    double closest = max_distance_squared;
    bool found = false;
    for (std::uint32_t surface_index = 0;
         surface_index < scene.surface_count;
         ++surface_index) {
        const SurfaceData& surface = scene.surfaces[surface_index];
        if (surface.enabled == 0 || !IsValidLattice(surface.lattice)) {
            continue;
        }
        for (std::uint16_t row = 0;
             row <= surface.lattice.divisions_y;
             ++row) {
            for (std::uint16_t column = 0;
                 column < surface.lattice.divisions_x;
                 ++column) {
                constexpr int kEdgeSamples = 12;
                Point2 previous;
                bool have_previous = false;
                for (int sample = 0; sample <= kEdgeSamples; ++sample) {
                    Point2 projected;
                    const double u =
                        (static_cast<double>(column) +
                         static_cast<double>(sample) / kEdgeSamples) /
                        surface.lattice.divisions_x;
                    const double v =
                        static_cast<double>(row) /
                        surface.lattice.divisions_y;
                    if (!ProjectSurfacePointToFrame(
                            in_data,
                            event_extra,
                            surface,
                            camera,
                            u,
                            v,
                            projected)) {
                        have_previous = false;
                        continue;
                    }
                    if (have_previous) {
                        const double distance =
                            PointSegmentDistanceSquared(
                                mouse,
                                previous,
                                projected);
                        if (distance <= closest) {
                            closest = distance;
                            hit = {
                                surface_index,
                                SelectionEntityKind::EdgeHorizontal,
                                row,
                                column};
                            found = true;
                        }
                    }
                    previous = projected;
                    have_previous = true;
                }
            }
        }
        for (std::uint16_t row = 0;
             row < surface.lattice.divisions_y;
             ++row) {
            for (std::uint16_t column = 0;
                 column <= surface.lattice.divisions_x;
                 ++column) {
                constexpr int kEdgeSamples = 12;
                Point2 previous;
                bool have_previous = false;
                for (int sample = 0; sample <= kEdgeSamples; ++sample) {
                    Point2 projected;
                    const double u =
                        static_cast<double>(column) /
                        surface.lattice.divisions_x;
                    const double v =
                        (static_cast<double>(row) +
                         static_cast<double>(sample) / kEdgeSamples) /
                        surface.lattice.divisions_y;
                    if (!ProjectSurfacePointToFrame(
                            in_data,
                            event_extra,
                            surface,
                            camera,
                            u,
                            v,
                            projected)) {
                        have_previous = false;
                        continue;
                    }
                    if (have_previous) {
                        const double distance =
                            PointSegmentDistanceSquared(
                                mouse,
                                previous,
                                projected);
                        if (distance <= closest) {
                            closest = distance;
                            hit = {
                                surface_index,
                                SelectionEntityKind::EdgeVertical,
                                row,
                                column};
                            found = true;
                        }
                    }
                    previous = projected;
                    have_previous = true;
                }
            }
        }
    }
    return found;
}

double TriangleSign(Point2 point, Point2 a, Point2 b) {
    return (point.x - b.x) * (a.y - b.y) -
           (a.x - b.x) * (point.y - b.y);
}

bool PointInTriangle(Point2 point, Point2 a, Point2 b, Point2 c) {
    const double d1 = TriangleSign(point, a, b);
    const double d2 = TriangleSign(point, b, c);
    const double d3 = TriangleSign(point, c, a);
    constexpr double kEpsilon = 1.0e-6;
    const bool has_negative =
        d1 < -kEpsilon || d2 < -kEpsilon || d3 < -kEpsilon;
    const bool has_positive =
        d1 > kEpsilon || d2 > kEpsilon || d3 > kEpsilon;
    return !(has_negative && has_positive);
}

bool HitTestFaceEntity(
    PF_InData* in_data,
    PF_EventExtra* event_extra,
    const SceneData& scene,
    const CameraState& camera,
    Point2 mouse,
    SelectionEntityRef& hit) {
    double closest_centroid_distance =
        std::numeric_limits<double>::infinity();
    bool found = false;
    for (std::uint32_t surface_index = 0;
         surface_index < scene.surface_count;
         ++surface_index) {
        const SurfaceData& surface = scene.surfaces[surface_index];
        if (surface.enabled == 0 || !IsValidLattice(surface.lattice)) {
            continue;
        }
        for (std::uint16_t row = 0;
             row < surface.lattice.divisions_y;
             ++row) {
            for (std::uint16_t column = 0;
                 column < surface.lattice.divisions_x;
                 ++column) {
                Point2 top_left;
                Point2 top_right;
                Point2 bottom_left;
                Point2 bottom_right;
                if (!ProjectControlPointToFrame(
                        in_data,
                        event_extra,
                        surface,
                        camera,
                        row,
                        column,
                        top_left) ||
                    !ProjectControlPointToFrame(
                        in_data,
                        event_extra,
                        surface,
                        camera,
                        row,
                        column + 1,
                        top_right) ||
                    !ProjectControlPointToFrame(
                        in_data,
                        event_extra,
                        surface,
                        camera,
                        row + 1,
                        column,
                        bottom_left) ||
                    !ProjectControlPointToFrame(
                        in_data,
                        event_extra,
                        surface,
                        camera,
                        row + 1,
                        column + 1,
                        bottom_right)) {
                    continue;
                }
                if (!PointInTriangle(
                        mouse,
                        top_left,
                        top_right,
                        bottom_right) &&
                    !PointInTriangle(
                        mouse,
                        top_left,
                        bottom_right,
                        bottom_left)) {
                    continue;
                }
                const Point2 centroid{
                    (top_left.x + top_right.x +
                     bottom_left.x + bottom_right.x) /
                        4.0,
                    (top_left.y + top_right.y +
                     bottom_left.y + bottom_right.y) /
                        4.0};
                const double dx = mouse.x - centroid.x;
                const double dy = mouse.y - centroid.y;
                const double distance = dx * dx + dy * dy;
                if (distance <= closest_centroid_distance) {
                    closest_centroid_distance = distance;
                    hit = {
                        surface_index,
                        SelectionEntityKind::Face,
                        row,
                        column};
                    found = true;
                }
            }
        }
    }
    return found;
}

std::vector<LatticePointRef> CollectFreePointsInMarquee(
    PF_InData* in_data,
    PF_EventExtra* event_extra,
    const SceneData& scene,
    const CameraState& camera,
    const NullPointOverrideState& null_overrides,
    Point2 corner_a,
    Point2 corner_b) {
    const double min_x = std::min(corner_a.x, corner_b.x);
    const double max_x = std::max(corner_a.x, corner_b.x);
    const double min_y = std::min(corner_a.y, corner_b.y);
    const double max_y = std::max(corner_a.y, corner_b.y);
    std::array<std::vector<LatticePointRef>, kMaximumSurfaces> by_surface{};
    std::array<std::size_t, kMaximumSurfaces> counts{};
    for (std::uint32_t surface_index = 0;
         surface_index < scene.surface_count;
         ++surface_index) {
        const SurfaceData& surface = scene.surfaces[surface_index];
        if (surface.enabled == 0 || !IsValidLattice(surface.lattice)) {
            continue;
        }
        for (std::uint16_t row = 0;
             row <= surface.lattice.divisions_y;
             ++row) {
            for (std::uint16_t column = 0;
                 column <= surface.lattice.divisions_x;
                 ++column) {
                const std::size_t point_index = LatticePointIndex(
                    surface.lattice.divisions_x,
                    row,
                    column);
                if (null_overrides.IsControlled(
                        surface_index,
                        point_index)) {
                    continue;
                }
                Point2 projected;
                if (!ProjectSurfacePointToFrame(
                        in_data,
                        event_extra,
                        surface,
                        camera,
                        static_cast<double>(column) /
                            surface.lattice.divisions_x,
                        static_cast<double>(row) /
                            surface.lattice.divisions_y,
                        projected)) {
                    continue;
                }
                if (projected.x < min_x || projected.x > max_x ||
                    projected.y < min_y || projected.y > max_y) {
                    continue;
                }
                by_surface[surface_index].push_back(
                    {surface_index, row, column});
                ++counts[surface_index];
            }
        }
    }
    std::uint32_t best_surface = 0;
    std::size_t best_count = 0;
    for (std::uint32_t surface_index = 0;
         surface_index < scene.surface_count;
         ++surface_index) {
        if (counts[surface_index] > best_count) {
            best_count = counts[surface_index];
            best_surface = surface_index;
        }
    }
    if (best_count == 0) {
        return {};
    }
    return by_surface[best_surface];
}

void ApplyMarqueeSelection(
    PF_InData* in_data,
    PF_EventExtra* event_extra,
    const SceneData& scene,
    const CameraState& camera,
    const NullPointOverrideState& null_overrides) {
    std::vector<LatticePointRef> boxed = CollectFreePointsInMarquee(
        in_data,
        event_extra,
        scene,
        camera,
        null_overrides,
        g_selection.marquee_start,
        g_selection.marquee_end);
    if (g_selection.marquee_additive && !g_selection.points.empty() &&
        !boxed.empty() &&
        g_selection.points.front().surface == boxed.front().surface) {
        MergePointsIntoSelection(boxed);
        return;
    }
    if (g_selection.marquee_additive && !boxed.empty() &&
        !g_selection.points.empty() &&
        g_selection.points.front().surface != boxed.front().surface) {
        // Keep same-surface rule: replace rather than mix surfaces.
        SetSelectionPoints(boxed);
        return;
    }
    if (g_selection.marquee_additive && g_selection.points.empty()) {
        SetSelectionPoints(boxed);
        return;
    }
    if (!g_selection.marquee_additive) {
        SetSelectionPoints(boxed);
    }
}

bool ConfirmLatticeReduction(
    std::uint16_t old_x,
    std::uint16_t old_y,
    std::uint16_t new_x,
    std::uint16_t new_y) {
#if defined(__APPLE__)
    char message[256]{};
    std::snprintf(
        message,
        sizeof(message),
        "Reducing the lattice from %ux%u to %ux%u points resamples "
        "the current shape and is irreversible. Continue?",
        old_x + 1,
        old_y + 1,
        new_x + 1,
        new_y + 1);
    CFStringRef text = CFStringCreateWithCString(
        kCFAllocatorDefault,
        message,
        kCFStringEncodingUTF8);
    CFOptionFlags response{};
    const SInt32 result = CFUserNotificationDisplayAlert(
        0,
        kCFUserNotificationCautionAlertLevel,
        nullptr,
        nullptr,
        nullptr,
        CFSTR("SurfaceLab lattice reduction"),
        text,
        CFSTR("Reduce"),
        CFSTR("Cancel"),
        nullptr,
        &response);
    if (text) {
        CFRelease(text);
    }
    return result == 0 &&
           (response & 0x3U) ==
               kCFUserNotificationDefaultResponse;
#else
    (void)old_x;
    (void)old_y;
    (void)new_x;
    (void)new_y;
    return true;
#endif
}

PF_Err ResizeSurfaceLattice(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_ParamDef* params[],
    std::uint32_t surface) {
    InitializePendingLatticeForInput(
        in_data,
        params,
        surface);
    const std::uint16_t divisions_x =
        static_cast<std::uint16_t>(std::clamp<A_long>(
            params[SurfaceParam(surface, kSurfaceDivisionsXOffset)]
                ->u.sd.value,
            kMinimumLatticeDivisions,
            kMaximumLatticeDivisions));
    const std::uint16_t divisions_y =
        static_cast<std::uint16_t>(std::clamp<A_long>(
            params[SurfaceParam(surface, kSurfaceDivisionsYOffset)]
                ->u.sd.value,
            kMinimumLatticeDivisions,
            kMaximumLatticeDivisions));
    PF_Handle handle =
        params[SurfaceLatticeParam(surface)]->u.arb_d.value;
    if (!handle) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }
    auto* lattice = static_cast<LatticeData*>(PF_LOCK_HANDLE(handle));
    if (!lattice) {
        return PF_Err_OUT_OF_MEMORY;
    }
    if (!IsValidLattice(*lattice) ||
        NeedsInputSizedInitialization(*lattice)) {
        PF_UNLOCK_HANDLE(handle);
        return PF_Err_INTERNAL_STRUCT_DAMAGED;
    }
    const std::uint16_t old_x = lattice->divisions_x;
    const std::uint16_t old_y = lattice->divisions_y;
    const bool reducing =
        divisions_x < old_x || divisions_y < old_y;
    const LatticeData source = *lattice;
    PF_UNLOCK_HANDLE(handle);
    if (reducing &&
        !ConfirmLatticeReduction(
            old_x,
            old_y,
            divisions_x,
            divisions_y)) {
        params[SurfaceParam(surface, kSurfaceDivisionsXOffset)]
            ->u.sd.value = old_x;
        params[SurfaceParam(surface, kSurfaceDivisionsYOffset)]
            ->u.sd.value = old_y;
        return PF_Err_NONE;
    }
    LatticeData resized{};
    const bool valid =
        ResizeLattice(source, divisions_x, divisions_y, resized);
    if (valid) {
        lattice = static_cast<LatticeData*>(PF_LOCK_HANDLE(handle));
        if (!lattice) {
            return PF_Err_OUT_OF_MEMORY;
        }
        *lattice = resized;
        PF_UNLOCK_HANDLE(handle);
    }
    if (!valid) {
        return PF_Err_INTERNAL_STRUCT_DAMAGED;
    }
    (void)out_data;
    params[SurfaceLatticeParam(surface)]->uu.change_flags |=
        PF_ChangeFlag_CHANGED_VALUE;
    return PF_Err_NONE;
}

void MarkChanged(PF_ParamDef* parameter) {
    if (parameter) {
        parameter->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
    }
}

PF_Err UpdateSurfaceSlotUi(
    PF_InData* in_data,
    PF_ParamDef* params[],
    std::uint32_t surface) {
    if (!in_data || !params || surface >= kSurfaceCount) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }
    // Surface 1 always has the host input fallback. Later slots are enabled
    // only by assigning their Source Layer.
    bool enabled = surface == 0;
    if (!enabled) {
        PF_ParamDef source_value;
        AEFX_CLR_STRUCT(source_value);
        const PF_Err checkout_error = PF_CHECKOUT_PARAM(
            in_data,
            SurfaceSourceParam(surface),
            in_data->current_time,
            in_data->time_step,
            in_data->time_scale,
            &source_value);
        if (checkout_error == PF_Err_NONE) {
            enabled = source_value.u.ld.data != nullptr;
            const PF_Err checkin_error =
                PF_CHECKIN_PARAM(in_data, &source_value);
            if (checkin_error != PF_Err_NONE) {
                return checkin_error;
            }
        }
    }
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    const auto update_parameter =
        [&](PF_ParamIndex index) -> PF_Err {
            return suites.ParamUtilsSuite3()->PF_UpdateParamUI(
                in_data->effect_ref,
                index,
                params[index]);
        };

    const PF_ParamIndex topic =
        SurfaceParam(surface, kSurfaceTopicStartOffset);
    char topic_name[32]{};
    std::snprintf(
        topic_name,
        sizeof(topic_name),
        enabled ? "Surface %u" : "Surface %u (empty)",
        surface + 1);
    if (std::strncmp(
            params[topic]->PF_DEF_NAME,
            topic_name,
            sizeof(params[topic]->PF_DEF_NAME)) != 0) {
        PF_STRNNCPY(
            params[topic]->PF_DEF_NAME,
            topic_name,
            sizeof(params[topic]->PF_DEF_NAME));
        const PF_Err error = update_parameter(topic);
        if (error != PF_Err_NONE) {
            return error;
        }
    }

    const PF_ParamIndex source = SurfaceSourceParam(surface);
    const char* source_name =
        enabled ? "Source Layer" : "Source Layer (enable)";
    if (std::strncmp(
            params[source]->PF_DEF_NAME,
            source_name,
            sizeof(params[source]->PF_DEF_NAME)) != 0) {
        PF_STRNNCPY(
            params[source]->PF_DEF_NAME,
            source_name,
            sizeof(params[source]->PF_DEF_NAME));
        const PF_Err error = update_parameter(source);
        if (error != PF_Err_NONE) {
            return error;
        }
    }

    for (PF_ParamIndex offset = kSurfacePositionOffset;
         offset < kSurfaceTopicEndOffset;
         ++offset) {
        if (offset == kSurfaceLatticeOffset) {
            continue;
        }
        const PF_ParamIndex index = SurfaceParam(
            surface,
            static_cast<SurfaceParamOffset>(offset));
        const PF_ParamUIFlags old_flags = params[index]->ui_flags;
        if (enabled) {
            params[index]->ui_flags &= ~PF_PUI_INVISIBLE;
        } else {
            params[index]->ui_flags |= PF_PUI_INVISIBLE;
        }
        if (params[index]->ui_flags != old_flags) {
            const PF_Err error = update_parameter(index);
            if (error != PF_Err_NONE) {
                return error;
            }
        }
    }
    return PF_Err_NONE;
}

PF_Err PublishRigBridge(
    PF_InData* in_data,
    PF_ParamDef* params[]) {
    if (!in_data || !params) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }
    PF_Point3DDef& request =
        params[kParamRigRequest]->u.point3d_d;
    const std::uint32_t surface_index =
        static_cast<std::uint32_t>(std::clamp<A_long>(
            static_cast<A_long>(std::llround(request.x_value)),
            1,
            kSurfaceCount) - 1);
    InitializePendingLatticeForInput(
        in_data,
        params,
        surface_index);
    const PF_Handle handle =
        params[SurfaceLatticeParam(surface_index)]->u.arb_d.value;
    if (!handle) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }
    const auto* lattice =
        static_cast<const LatticeData*>(PF_LOCK_HANDLE(handle));
    if (!lattice) {
        return PF_Err_OUT_OF_MEMORY;
    }
    if (!IsValidLattice(*lattice)) {
        PF_UNLOCK_HANDLE(handle);
        return PF_Err_INTERNAL_STRUCT_DAMAGED;
    }
    LatticeData published = *lattice;
    PF_UNLOCK_HANDLE(handle);
    if (NeedsInputSizedInitialization(published)) {
        const double width =
            params[kParamInput]->u.ld.width > 0
                ? params[kParamInput]->u.ld.width
                : in_data->width;
        const double height =
            params[kParamInput]->u.ld.height > 0
                ? params[kParamInput]->u.ld.height
                : in_data->height;
        InitializeLattice(
            published,
            published.divisions_x,
            published.divisions_y,
            width,
            height,
            published.surface_id);
    }
    const std::uint16_t row =
        static_cast<std::uint16_t>(std::clamp<A_long>(
            static_cast<A_long>(std::llround(request.y_value)),
            0,
            published.divisions_y));
    request.x_value = static_cast<double>(surface_index + 1U);
    request.y_value = static_cast<double>(row);
    MarkChanged(params[kParamRigRequest]);

    // Doubles exactly represent every 32-bit integer. Packing the 64-bit ID
    // into high/low halves and Dx/Dy into one small integer keeps script
    // transport lossless while replacing six legacy slider streams.
    PF_Point3DDef& metadata =
        params[kParamRigMetadata]->u.point3d_d;
    metadata.x_value = static_cast<double>(
        static_cast<std::uint32_t>(published.surface_id >> 32U));
    metadata.y_value = static_cast<double>(
        static_cast<std::uint32_t>(
            published.surface_id & 0xffffffffU));
    metadata.z_value = static_cast<double>(
        static_cast<std::uint32_t>(published.divisions_x) * 32U +
        static_cast<std::uint32_t>(published.divisions_y));
    MarkChanged(params[kParamRigMetadata]);
    for (std::uint32_t column = 0;
         column < kMaximumLatticeAxisPoints;
         ++column) {
        StoredPoint3 point{};
        if (column <= published.divisions_x) {
            point = published.points[LatticePointIndex(
                published.divisions_x,
                row,
                static_cast<std::uint16_t>(column))];
        }
        PF_ParamDef* output = params[RigPointParam(column)];
        output->u.point3d_d.x_value = point.x;
        output->u.point3d_d.y_value = point.y;
        output->u.point3d_d.z_value = point.z;
        MarkChanged(output);
    }
    return PF_Err_NONE;
}

bool DecodePointAnimationSlot(
    const PF_ParamDef* metadata,
    std::uint32_t& surface,
    std::uint16_t& row,
    std::uint16_t& column) {
    if (!metadata) {
        return false;
    }
    const PF_Point3DDef& point = metadata->u.point3d_d;
    const A_long encoded_surface =
        static_cast<A_long>(std::llround(point.x_value));
    const A_long encoded_row =
        static_cast<A_long>(std::llround(point.y_value));
    const A_long encoded_column =
        static_cast<A_long>(std::llround(point.z_value));
    if (encoded_surface < 1 ||
        encoded_surface > static_cast<A_long>(kSurfaceCount) ||
        encoded_row < 1 ||
        encoded_row > static_cast<A_long>(kMaximumLatticeAxisPoints) ||
        encoded_column < 1 ||
        encoded_column > static_cast<A_long>(kMaximumLatticeAxisPoints)) {
        return false;
    }
    surface = static_cast<std::uint32_t>(encoded_surface - 1);
    row = static_cast<std::uint16_t>(encoded_row - 1);
    column = static_cast<std::uint16_t>(encoded_column - 1);
    return true;
}

PF_Err ExposeSelectedPointAnimations(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_ParamDef* params[]) {
    if (!in_data || !params) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }
    std::size_t exposed = 0;
    for (const LatticePointRef& selected : g_selection.points) {
        std::uint32_t chosen_slot = kPointAnimationSlotCount;
        for (std::uint32_t slot = 0;
             slot < kPointAnimationSlotCount;
             ++slot) {
            std::uint32_t surface{};
            std::uint16_t row{};
            std::uint16_t column{};
            if (DecodePointAnimationSlot(
                    params[PointAnimationMetadataParam(slot)],
                    surface,
                    row,
                    column)) {
                if (surface == selected.surface &&
                    row == selected.row &&
                    column == selected.column) {
                    chosen_slot = slot;
                    break;
                }
            } else if (chosen_slot == kPointAnimationSlotCount) {
                chosen_slot = slot;
            }
        }
        if (chosen_slot >= kPointAnimationSlotCount) {
            break;
        }
        std::uint32_t existing_surface{};
        std::uint16_t existing_row{};
        std::uint16_t existing_column{};
        const bool already_assigned = DecodePointAnimationSlot(
            params[PointAnimationMetadataParam(chosen_slot)],
            existing_surface,
            existing_row,
            existing_column);
        if (!already_assigned) {
            InitializePendingLatticeForInput(
                in_data,
                params,
                selected.surface);
            const PF_Handle handle =
                params[SurfaceLatticeParam(selected.surface)]
                    ->u.arb_d.value;
            if (!handle) {
                continue;
            }
            const auto* lattice =
                static_cast<const LatticeData*>(
                    PF_LOCK_HANDLE(handle));
            if (!lattice || !IsValidLattice(*lattice) ||
                selected.row > lattice->divisions_y ||
                selected.column > lattice->divisions_x) {
                if (lattice) {
                    PF_UNLOCK_HANDLE(handle);
                }
                continue;
            }
            const StoredPoint3 point = lattice->points[
                LatticePointIndex(
                    lattice->divisions_x,
                    selected.row,
                    selected.column)];
            PF_UNLOCK_HANDLE(handle);
            PF_Point3DDef& metadata =
                params[PointAnimationMetadataParam(chosen_slot)]
                    ->u.point3d_d;
            metadata.x_value = selected.surface + 1;
            metadata.y_value = selected.row + 1;
            metadata.z_value = selected.column + 1;
            MarkChanged(
                params[PointAnimationMetadataParam(chosen_slot)]);
            PF_Point3DDef& value =
                params[PointAnimationValueParam(chosen_slot)]
                    ->u.point3d_d;
            value.x_value = point.x;
            value.y_value = point.y;
            value.z_value = point.z;
            MarkChanged(
                params[PointAnimationValueParam(chosen_slot)]);
        }
        ++exposed;
    }
    if (out_data) {
        out_data->out_flags |=
            PF_OutFlag_REFRESH_UI |
            PF_OutFlag_FORCE_RERENDER;
        if (exposed == 0) {
            std::snprintf(
                out_data->return_msg,
                sizeof(out_data->return_msg),
                "Select free lattice points in the Composition panel first.");
            out_data->out_flags |= PF_OutFlag_DISPLAY_ERROR_MESSAGE;
        }
    }
    return PF_Err_NONE;
}

PF_Err ClearPointAnimationSlots(
    PF_OutData* out_data,
    PF_ParamDef* params[]) {
    if (!params) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }
    for (std::uint32_t slot = 0;
         slot < kPointAnimationSlotCount;
         ++slot) {
        PF_Point3DDef& metadata =
            params[PointAnimationMetadataParam(slot)]->u.point3d_d;
        metadata.x_value = 0.0;
        metadata.y_value = 0.0;
        metadata.z_value = 0.0;
        MarkChanged(params[PointAnimationMetadataParam(slot)]);
    }
    if (out_data) {
        out_data->out_flags |=
            PF_OutFlag_REFRESH_UI |
            PF_OutFlag_FORCE_RERENDER;
    }
    return PF_Err_NONE;
}

PF_Err MatchSurfaceToSourceAspect(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_ParamDef* params[]) {
    if (!in_data || !params) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }
    const std::uint32_t surface =
        static_cast<std::uint32_t>(std::clamp<A_long>(
            params[kParamSurfaceUtilitiesTarget]->u.pd.value,
            1,
            static_cast<A_long>(kSurfaceCount)) - 1);

    A_long source_width = 0;
    A_long source_height = 0;
    double pixel_aspect = 1.0;
    PF_ParamDef source_value;
    AEFX_CLR_STRUCT(source_value);
    const PF_Err checkout_error = PF_CHECKOUT_PARAM(
        in_data,
        SurfaceSourceParam(surface),
        in_data->current_time,
        in_data->time_step,
        in_data->time_scale,
        &source_value);
    if (checkout_error == PF_Err_NONE) {
        source_width = source_value.u.ld.width;
        source_height = source_value.u.ld.height;
        const PF_RationalScale par =
            source_value.u.ld.pix_aspect_ratio;
        if (par.num > 0 && par.den > 0) {
            pixel_aspect =
                static_cast<double>(par.num) /
                static_cast<double>(par.den);
        }
        const PF_Err checkin_error =
            PF_CHECKIN_PARAM(in_data, &source_value);
        if (checkin_error != PF_Err_NONE) {
            return checkin_error;
        }
    }
    if ((source_width <= 0 || source_height <= 0) &&
        surface == 0 && params[kParamInput]) {
        source_width = params[kParamInput]->u.ld.width;
        source_height = params[kParamInput]->u.ld.height;
        const PF_RationalScale par =
            params[kParamInput]->u.ld.pix_aspect_ratio;
        if (par.num > 0 && par.den > 0) {
            pixel_aspect =
                static_cast<double>(par.num) /
                static_cast<double>(par.den);
        }
    }
    if (source_width <= 0 || source_height <= 0) {
        if (out_data) {
            std::snprintf(
                out_data->return_msg,
                sizeof(out_data->return_msg),
                "Assign a Source Layer to Surface %u first.",
                surface + 1);
            out_data->out_flags |=
                PF_OutFlag_DISPLAY_ERROR_MESSAGE;
        }
        return PF_Err_NONE;
    }

    InitializePendingLatticeForInput(in_data, params, surface);
    PF_Handle handle =
        params[SurfaceLatticeParam(surface)]->u.arb_d.value;
    if (!handle) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }
    auto* lattice =
        static_cast<LatticeData*>(PF_LOCK_HANDLE(handle));
    if (!lattice || !IsValidLattice(*lattice)) {
        if (lattice) {
            PF_UNLOCK_HANDLE(handle);
        }
        return PF_Err_INTERNAL_STRUCT_DAMAGED;
    }
    double minimum_x = std::numeric_limits<double>::infinity();
    double maximum_x = -std::numeric_limits<double>::infinity();
    double minimum_y = std::numeric_limits<double>::infinity();
    double maximum_y = -std::numeric_limits<double>::infinity();
    Point3 centroid{};
    std::size_t point_count = 0;
    for (std::uint16_t row = 0;
         row <= lattice->divisions_y;
         ++row) {
        for (std::uint16_t column = 0;
             column <= lattice->divisions_x;
             ++column) {
            const StoredPoint3& point =
                lattice->points[LatticePointIndex(
                    lattice->divisions_x,
                    row,
                    column)];
            minimum_x = std::min(minimum_x, static_cast<double>(point.x));
            maximum_x = std::max(maximum_x, static_cast<double>(point.x));
            minimum_y = std::min(minimum_y, static_cast<double>(point.y));
            maximum_y = std::max(maximum_y, static_cast<double>(point.y));
            centroid.x += point.x;
            centroid.y += point.y;
            centroid.z += point.z;
            ++point_count;
        }
    }
    const double current_width = maximum_x - minimum_x;
    const double current_height = maximum_y - minimum_y;
    if (point_count == 0 || current_width <= 1.0e-6 ||
        current_height <= 1.0e-6) {
        PF_UNLOCK_HANDLE(handle);
        if (out_data) {
            std::snprintf(
                out_data->return_msg,
                sizeof(out_data->return_msg),
                "Surface %u has no measurable X/Y extent.",
                surface + 1);
            out_data->out_flags |=
                PF_OutFlag_DISPLAY_ERROR_MESSAGE;
        }
        return PF_Err_NONE;
    }
    centroid.x /= point_count;
    centroid.y /= point_count;
    centroid.z /= point_count;
    const double target_aspect =
        static_cast<double>(source_width) * pixel_aspect /
        static_cast<double>(source_height);
    const double x_scale =
        target_aspect * current_height / current_width;
    for (std::uint16_t row = 0;
         row <= lattice->divisions_y;
         ++row) {
        for (std::uint16_t column = 0;
             column <= lattice->divisions_x;
             ++column) {
            StoredPoint3& point =
                lattice->points[LatticePointIndex(
                    lattice->divisions_x,
                    row,
                    column)];
            point.x = static_cast<float>(
                centroid.x +
                (static_cast<double>(point.x) - centroid.x) *
                    x_scale);
        }
    }
    PF_UNLOCK_HANDLE(handle);
    MarkChanged(params[SurfaceLatticeParam(surface)]);

    for (std::uint32_t slot = 0;
         slot < kPointAnimationSlotCount;
         ++slot) {
        std::uint32_t bound_surface{};
        std::uint16_t row{};
        std::uint16_t column{};
        if (!DecodePointAnimationSlot(
                params[PointAnimationMetadataParam(slot)],
                bound_surface,
                row,
                column) ||
            bound_surface != surface) {
            continue;
        }
        PF_Point3DDef& value =
            params[PointAnimationValueParam(slot)]->u.point3d_d;
        value.x_value =
            centroid.x +
            (value.x_value - centroid.x) * x_scale;
        MarkChanged(params[PointAnimationValueParam(slot)]);
    }
    if (out_data) {
        out_data->out_flags |=
            PF_OutFlag_REFRESH_UI |
            PF_OutFlag_FORCE_RERENDER;
    }
    return PF_Err_NONE;
}

PF_Err UpdatePointAnimationUi(
    PF_InData* in_data,
    PF_ParamDef* params[]) {
    if (!in_data || !params) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    for (std::uint32_t slot = 0;
         slot < kPointAnimationSlotCount;
         ++slot) {
        std::uint32_t surface{};
        std::uint16_t row{};
        std::uint16_t column{};
        const bool assigned = DecodePointAnimationSlot(
            params[PointAnimationMetadataParam(slot)],
            surface,
            row,
            column);
        bool active = false;
        if (assigned) {
            InitializePendingLatticeForInput(in_data, params, surface);
            const PF_Handle handle =
                params[SurfaceLatticeParam(surface)]->u.arb_d.value;
            if (handle) {
                const auto* lattice =
                    static_cast<const LatticeData*>(
                        PF_LOCK_HANDLE(handle));
                active = lattice && IsValidLattice(*lattice) &&
                         row <= lattice->divisions_y &&
                         column <= lattice->divisions_x;
                if (lattice) {
                    PF_UNLOCK_HANDLE(handle);
                }
            }
        }

        PF_ParamDef* value =
            params[PointAnimationValueParam(slot)];
        char name[32]{};
        if (assigned) {
            std::snprintf(
                name,
                sizeof(name),
                active ? "S%u Point %u,%u"
                       : "S%u Point %u,%u (inactive)",
                surface + 1,
                column,
                row);
        } else {
            std::snprintf(
                name,
                sizeof(name),
                "Point Slot %u",
                slot + 1);
        }
        const PF_ParamUIFlags old_flags = value->ui_flags;
        if (assigned) {
            value->ui_flags &= ~PF_PUI_INVISIBLE;
        } else {
            value->ui_flags |= PF_PUI_INVISIBLE;
        }
        if (active) {
            value->ui_flags &= ~PF_PUI_DISABLED;
        } else {
            value->ui_flags |= PF_PUI_DISABLED;
        }
        const bool name_changed =
            std::strncmp(
                value->PF_DEF_NAME,
                name,
                sizeof(value->PF_DEF_NAME)) != 0;
        if (name_changed) {
            PF_STRNNCPY(
                value->PF_DEF_NAME,
                name,
                sizeof(value->PF_DEF_NAME));
        }
        if (name_changed || value->ui_flags != old_flags) {
            const PF_Err error =
                suites.ParamUtilsSuite3()->PF_UpdateParamUI(
                    in_data->effect_ref,
                    PointAnimationValueParam(slot),
                    value);
            if (error != PF_Err_NONE) {
                return error;
            }
        }
    }
    return PF_Err_NONE;
}

}  // namespace

PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data) {
    PF_ParamDef def;
    AEFX_CLR_STRUCT(def);

    // PF_Point3DDef defaults are percentages of the layer dimensions, not
    // pixels. 50/50 becomes the actual input centre (for example 960/540 in a
    // 1920x1080 comp). Passing pixel coordinates here produced 18432/5832.
    constexpr double default_center_x = 50.0;
    constexpr double default_center_y = 50.0;

    PF_ADD_TOPIC("Scene", kDiskSceneStart);
    PF_Err error = AddPoint3D(
        in_data,
        def,
        "Position",
        default_center_x,
        default_center_y,
        0.0,
        kDiskScenePosition);
    if (error != PF_Err_NONE) {
        return error;
    }
    AEFX_CLR_STRUCT(def);
    PF_ADD_ANGLE("Rotation X", 0.0, kDiskSceneRotationX);
    AEFX_CLR_STRUCT(def);
    PF_ADD_ANGLE("Rotation Y", 0.0, kDiskSceneRotationY);
    AEFX_CLR_STRUCT(def);
    PF_ADD_ANGLE("Rotation Z", 0.0, kDiskSceneRotationZ);
    const char* scene_scale_names[] = {"Scale X", "Scale Y", "Scale Z"};
    for (int axis = 0; axis < 3; ++axis) {
        AEFX_CLR_STRUCT(def);
        PF_ADD_FLOAT_SLIDERX(
            scene_scale_names[axis],
            -1000.0,
            1000.0,
            -400.0,
            400.0,
            100.0,
            PF_Precision_TENTHS,
            PF_ValueDisplayFlag_PERCENT,
            PF_ParamFlag_NONE,
            kDiskSceneScaleX + axis);
    }
    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(kDiskSceneEnd);

    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Surfaces", kDiskSurfacesStart);
    for (std::uint32_t surface = 0; surface < kSurfaceCount; ++surface) {
        char group_name[32]{};
        std::snprintf(
            group_name,
            sizeof(group_name),
            "Surface %u",
            surface + 1);
        AEFX_CLR_STRUCT(def);
        def.flags = PF_ParamFlag_START_COLLAPSED;
        PF_ADD_TOPIC(
            group_name,
            SurfaceDiskId(surface, kSurfaceTopicStartOffset));

        AEFX_CLR_STRUCT(def);
        def.flags = PF_ParamFlag_SUPERVISE;
        PF_ADD_LAYER(
            "Source Layer",
            surface == 0
                ? PF_LayerDefault_MYSELF
                : PF_LayerDefault_NONE,
            SurfaceDiskId(surface, kSurfaceSourceOffset));

        error = AddPoint3D(
            in_data,
            def,
            "Position",
            default_center_x,
            default_center_y,
            0.0,
            SurfaceDiskId(surface, kSurfacePositionOffset));
        if (error != PF_Err_NONE) {
            return error;
        }
        AEFX_CLR_STRUCT(def);
        PF_ADD_ANGLE(
            "Rotation X",
            0.0,
            SurfaceDiskId(surface, kSurfaceRotationXOffset));
        AEFX_CLR_STRUCT(def);
        PF_ADD_ANGLE(
            "Rotation Y",
            0.0,
            SurfaceDiskId(surface, kSurfaceRotationYOffset));
        AEFX_CLR_STRUCT(def);
        PF_ADD_ANGLE(
            "Rotation Z",
            0.0,
            SurfaceDiskId(surface, kSurfaceRotationZOffset));

        const char* scale_names[] = {"Scale X", "Scale Y", "Scale Z"};
        for (int axis = 0; axis < 3; ++axis) {
            AEFX_CLR_STRUCT(def);
            PF_ADD_FLOAT_SLIDERX(
                scale_names[axis],
                -1000.0,
                1000.0,
                -400.0,
                400.0,
                100.0,
                PF_Precision_TENTHS,
                PF_ValueDisplayFlag_PERCENT,
                PF_ParamFlag_NONE,
                SurfaceDiskId(
                    surface,
                    static_cast<SurfaceParamOffset>(
                        kSurfaceScaleXOffset + axis)));
        }

        AEFX_CLR_STRUCT(def);
        def.flags = PF_ParamFlag_SUPERVISE;
        PF_ADD_SLIDER(
            "Divisions X",
            kMinimumLatticeDivisions,
            kMaximumLatticeDivisions,
            kMinimumLatticeDivisions,
            kMaximumLatticeDivisions,
            3,
            SurfaceDiskId(surface, kSurfaceDivisionsXOffset));
        AEFX_CLR_STRUCT(def);
        def.flags = PF_ParamFlag_SUPERVISE;
        PF_ADD_SLIDER(
            "Divisions Y",
            kMinimumLatticeDivisions,
            kMaximumLatticeDivisions,
            kMinimumLatticeDivisions,
            kMaximumLatticeDivisions,
            3,
            SurfaceDiskId(surface, kSurfaceDivisionsYOffset));
        AEFX_CLR_STRUCT(def);
        PF_ADD_SLIDER(
            "Mesh Quality",
            kMinimumMeshQuality,
            kMaximumMeshQuality,
            kMinimumMeshQuality,
            kMaximumMeshQuality,
            4,
            SurfaceDiskId(surface, kSurfaceMeshQualityOffset));

        AEFX_CLR_STRUCT(def);
        PF_ADD_ANGLE(
            "Roll Angle",
            0.0,
            SurfaceDiskId(surface, kSurfaceRollAngleOffset));
        AEFX_CLR_STRUCT(def);
        PF_ADD_ANGLE(
            "Roll Tilt",
            0.0,
            SurfaceDiskId(surface, kSurfaceRollTiltOffset));
        AEFX_CLR_STRUCT(def);
        PF_ADD_FLOAT_SLIDERX(
            "Roll Radius",
            1.0,
            10000.0,
            10.0,
            2000.0,
            200.0,
            PF_Precision_TENTHS,
            PF_ValueDisplayFlag_NONE,
            PF_ParamFlag_NONE,
            SurfaceDiskId(surface, kSurfaceRollRadiusOffset));
        AEFX_CLR_STRUCT(def);
        PF_ADD_FLOAT_SLIDERX(
            "Roll Expand / Turn",
            0.0,
            10000.0,
            0.0,
            1000.0,
            0.0,
            PF_Precision_TENTHS,
            PF_ValueDisplayFlag_NONE,
            PF_ParamFlag_NONE,
            SurfaceDiskId(surface, kSurfaceRollExpandOffset));

        PF_ArbitraryH default_lattice = nullptr;
        error = CreateLatticeHandle(
            in_data,
            &default_lattice,
            in_data->width,
            in_data->height,
            surface);
        if (error != PF_Err_NONE) {
            return error;
        }
        AEFX_CLR_STRUCT(def);
        PF_ADD_ARBITRARY2(
            "Lattice",
            0,
            0,
            PF_ParamFlag_NONE,
            PF_PUI_NO_ECW_UI,
            default_lattice,
            SurfaceDiskId(surface, kSurfaceLatticeOffset),
            LatticeRefcon(surface));

        AEFX_CLR_STRUCT(def);
        PF_ADD_LAYER(
            "Back Source",
            PF_LayerDefault_NONE,
            SurfaceDiskId(surface, kSurfaceBackSourceOffset));

        AEFX_CLR_STRUCT(def);
        PF_ADD_POPUP(
            "Image Size",
            3,
            kImageSizeStretch,
            "Stretch|Fill|Fit",
            SurfaceDiskId(surface, kSurfaceImageSizeOffset));

        AEFX_CLR_STRUCT(def);
        PF_ADD_FLOAT_SLIDERX(
            "Specular",
            0.0,
            100.0,
            0.0,
            100.0,
            0.0,
            PF_Precision_TENTHS,
            PF_ValueDisplayFlag_PERCENT,
            PF_ParamFlag_NONE,
            SurfaceDiskId(surface, kSurfaceSpecularOffset));

        AEFX_CLR_STRUCT(def);
        PF_ADD_FLOAT_SLIDERX(
            "Roughness",
            0.0,
            100.0,
            0.0,
            100.0,
            50.0,
            PF_Precision_TENTHS,
            PF_ValueDisplayFlag_PERCENT,
            PF_ParamFlag_NONE,
            SurfaceDiskId(surface, kSurfaceRoughnessOffset));

        AEFX_CLR_STRUCT(def);
        PF_ADD_FLOAT_SLIDERX(
            "Metalness",
            0.0,
            100.0,
            0.0,
            100.0,
            0.0,
            PF_Precision_TENTHS,
            PF_ValueDisplayFlag_PERCENT,
            PF_ParamFlag_NONE,
            SurfaceDiskId(surface, kSurfaceMetalnessOffset));

        AEFX_CLR_STRUCT(def);
        PF_ADD_FLOAT_SLIDERX(
            "Thickness",
            0.0,
            1000.0,
            0.0,
            500.0,
            0.0,
            PF_Precision_TENTHS,
            PF_ValueDisplayFlag_NONE,
            PF_ParamFlag_NONE,
            SurfaceDiskId(surface, kSurfaceThicknessOffset));

        error = AddPoint3D(
            in_data,
            def,
            "Image Transform (X% Y% Z deg)",
            0.0,
            0.0,
            0.0,
            SurfaceDiskId(
                surface,
                kSurfaceImageTransformOffset));
        if (error != PF_Err_NONE) {
            return error;
        }

        AEFX_CLR_STRUCT(def);
        PF_ADD_FLOAT_SLIDERX(
            "Image Scale",
            1.0,
            1000.0,
            1.0,
            400.0,
            100.0,
            PF_Precision_TENTHS,
            PF_ValueDisplayFlag_PERCENT,
            PF_ParamFlag_NONE,
            SurfaceDiskId(surface, kSurfaceImageScaleOffset));

        AEFX_CLR_STRUCT(def);
        PF_END_TOPIC(
            SurfaceDiskId(surface, kSurfaceTopicEndOffset));
    }
    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(kDiskSurfacesEnd);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP(
        "Render View",
        4,
        kRenderViewFinish,
        "Finish|Depth|UV|Normal",
        kDiskRenderView);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP(
        "Antialiasing",
        3,
        kAntialiasing2Samples,
        "Off|2 Samples|4 Samples",
        kDiskAntialiasing);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP(
        "Edit Mode",
        4,
        kEditModeVertex,
        "Vertex|Edge|Face|Surface",
        kDiskEditMode);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP(
        "Transform Tool",
        3,
        kTransformToolMove,
        "Move|Rotate|Scale",
        kDiskTransformTool);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP(
        "Transform Space",
        2,
        kTransformSpaceLocal,
        "Local|World",
        kDiskTransformSpace);

    error = AddPoint3D(
        in_data,
        def,
        "Rig Request",
        0.0,
        0.0,
        0.0,
        kDiskRigRequest,
        PF_PUI_INVISIBLE,
        PF_ParamFlag_SUPERVISE);
    if (error != PF_Err_NONE) {
        return error;
    }
    error = AddPoint3D(
        in_data,
        def,
        "Rig Metadata",
        0.0,
        0.0,
        0.0,
        kDiskRigMetadata,
        PF_PUI_INVISIBLE | PF_PUI_DISABLED);
    if (error != PF_Err_NONE) {
        return error;
    }
    for (std::uint32_t column = 0;
         column < kMaximumLatticeAxisPoints;
         ++column) {
        char point_name[32]{};
        std::snprintf(
            point_name,
            sizeof(point_name),
            "Rig Point %u",
            column);
        error = AddPoint3D(
            in_data,
            def,
            point_name,
            0.0,
            0.0,
            0.0,
            kDiskRigPointsStart + static_cast<A_long>(column),
            PF_PUI_INVISIBLE | PF_PUI_DISABLED);
        if (error != PF_Err_NONE) {
            return error;
        }
    }

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_START_COLLAPSED;
    PF_ADD_TOPIC(
        "Point Animation",
        kDiskPointAnimationStart);

    AEFX_CLR_STRUCT(def);
    PF_ADD_BUTTON(
        "Selected Points",
        "Expose Selected Points",
        0,
        PF_ParamFlag_SUPERVISE,
        kDiskPointAnimationExpose);

    AEFX_CLR_STRUCT(def);
    PF_ADD_BUTTON(
        "Point Slots",
        "Clear All Point Slots",
        0,
        PF_ParamFlag_SUPERVISE,
        kDiskPointAnimationClear);

    for (std::uint32_t slot = 0;
         slot < kPointAnimationSlotCount;
         ++slot) {
        char metadata_name[32]{};
        std::snprintf(
            metadata_name,
            sizeof(metadata_name),
            "Point Slot %u Metadata",
            slot + 1);
        error = AddPoint3D(
            in_data,
            def,
            metadata_name,
            0.0,
            0.0,
            0.0,
            kDiskPointAnimationSlotsStart +
                static_cast<A_long>(slot) *
                    kPointAnimationSlotStride,
            PF_PUI_INVISIBLE | PF_PUI_DISABLED);
        if (error != PF_Err_NONE) {
            return error;
        }
        char value_name[32]{};
        std::snprintf(
            value_name,
            sizeof(value_name),
            "Point Slot %u",
            slot + 1);
        error = AddPoint3D(
            in_data,
            def,
            value_name,
            0.0,
            0.0,
            0.0,
            kDiskPointAnimationSlotsStart +
                static_cast<A_long>(slot) *
                    kPointAnimationSlotStride + 1,
            PF_PUI_INVISIBLE);
        if (error != PF_Err_NONE) {
            return error;
        }
    }
    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(kDiskPointAnimationEnd);

    AEFX_CLR_STRUCT(def);
    PF_ADD_BUTTON(
        "Null Controllers",
        "Create Null Rig...",
        0,
        PF_ParamFlag_SUPERVISE,
        kDiskCreateNullRig);

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_SUPERVISE;
    {
        char version_button[32]{};
        std::snprintf(
            version_button,
            sizeof(version_button),
            "v%s",
            kSurfaceLabVersionString);
        PF_ADD_BUTTON(
            "SurfaceLab Version",
            version_button,
            0,
            PF_ParamFlag_SUPERVISE,
            kDiskAboutVersion);
    }

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_START_COLLAPSED;
    PF_ADD_TOPIC(
        "Surface Utilities",
        kDiskSurfaceUtilitiesStart);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP(
        "Target Surface",
        kSurfaceCount,
        1,
        "Surface 1|Surface 2|Surface 3|Surface 4|"
        "Surface 5|Surface 6|Surface 7|Surface 8",
        kDiskSurfaceUtilitiesTarget);

    AEFX_CLR_STRUCT(def);
    PF_ADD_BUTTON(
        "Source Aspect",
        "Match Source Aspect",
        0,
        PF_ParamFlag_SUPERVISE,
        kDiskSurfaceUtilitiesMatchAspect);

    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(kDiskSurfaceUtilitiesEnd);

    PF_CustomUIInfo custom_ui;
    AEFX_CLR_STRUCT(custom_ui);
    custom_ui.events = PF_CustomEFlag_COMP;
    custom_ui.comp_ui_alignment = PF_UIAlignment_NONE;
    error = in_data->inter.register_ui(
        in_data->effect_ref,
        &custom_ui);
    if (error != PF_Err_NONE) {
        return error;
    }

    out_data->num_params = kParamCount;
    // Catch layout drift early: last registered index must be kParamCount - 1.
    static_assert(
        kParamSurfaceUtilitiesEnd + 1 == kParamCount,
        "Surface Utilities must terminate immediately before kParamCount");
    return PF_Err_NONE;
}

PF_Err UserChangedParam(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_ParamDef* params[],
    const PF_UserChangedParamExtra* extra) {
    if (!extra) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }
    if (extra->param_index ==
        kParamSurfaceUtilitiesMatchAspect) {
        return MatchSurfaceToSourceAspect(
            in_data,
            out_data,
            params);
    }
    if (extra->param_index == kParamPointAnimationExpose) {
        return ExposeSelectedPointAnimations(
            in_data,
            out_data,
            params);
    }
    if (extra->param_index == kParamPointAnimationClear) {
        return ClearPointAnimationSlots(out_data, params);
    }
    if (extra->param_index == kParamCreateNullRig) {
        return RunBundledScript(
            in_data,
            out_data,
            "SurfaceLabCreateNullRig");
    }
    if (extra->param_index == kParamAboutVersion) {
        if (out_data) {
            std::snprintf(
                out_data->return_msg,
                sizeof(out_data->return_msg),
                "SurfaceLab %s\n3D interpolating control-point lattice\n"
                "Metal device: %s\n"
                "Frame raster: CPU SmartFX.",
                kSurfaceLabVersionString,
                IsMetalDeviceReady(in_data) ? "ready" : "CPU fallback");
            out_data->out_flags |= PF_OutFlag_DISPLAY_ERROR_MESSAGE;
        }
        return PF_Err_NONE;
    }
    if (extra->param_index == kParamRigRequest) {
        return PublishRigBridge(in_data, params);
    }
    std::uint32_t surface{};
    PF_ParamIndex offset{};
    if (!SurfaceAndOffset(extra->param_index, surface, offset)) {
        return PF_Err_NONE;
    }
    if (offset == kSurfaceDivisionsXOffset ||
        offset == kSurfaceDivisionsYOffset) {
        return ResizeSurfaceLattice(
            in_data,
            out_data,
            params,
            surface);
    }
    if (offset == kSurfaceSourceOffset) {
        const PF_Err error =
            UpdateSurfaceSlotUi(in_data, params, surface);
        if (out_data) {
            out_data->out_flags |=
                PF_OutFlag_REFRESH_UI |
                PF_OutFlag_FORCE_RERENDER;
        }
        return error;
    }
    return PF_Err_NONE;
}

PF_Err UpdateParameterUi(
    PF_InData* in_data,
    PF_OutData*,
    PF_ParamDef* params[]) {
    char version_name[32]{};
#if SURFACELAB_METAL_DIAGNOSTIC_COPY
    const auto* global =
        in_data
            ? reinterpret_cast<const GlobalData*>(
                  in_data->global_data)
            : nullptr;
    const unsigned int gpu_calls =
        global
            ? global->metal_gpu_render_calls.load(
                  std::memory_order_relaxed)
            : 0U;
    std::snprintf(
        version_name,
        sizeof(version_name),
        "SurfaceLab %s | Metal G%u",
        kSurfaceLabVersionString,
        gpu_calls);
#else
    std::snprintf(
        version_name,
        sizeof(version_name),
        "SurfaceLab %s | %s",
        kSurfaceLabVersionString,
        IsMetalDeviceReady(in_data) ? "Metal Ready" : "CPU");
#endif
    if (std::strncmp(
            params[kParamAboutVersion]->PF_DEF_NAME,
            version_name,
            sizeof(params[kParamAboutVersion]->PF_DEF_NAME)) != 0) {
        PF_STRNNCPY(
            params[kParamAboutVersion]->PF_DEF_NAME,
            version_name,
            sizeof(params[kParamAboutVersion]->PF_DEF_NAME));
        AEGP_SuiteHandler suites(in_data->pica_basicP);
        const PF_Err version_error =
            suites.ParamUtilsSuite3()->PF_UpdateParamUI(
                in_data->effect_ref,
                kParamAboutVersion,
                params[kParamAboutVersion]);
        if (version_error != PF_Err_NONE) {
            return version_error;
        }
    }
    for (std::uint32_t surface = 0; surface < kSurfaceCount; ++surface) {
        const PF_Err ui_error =
            UpdateSurfaceSlotUi(in_data, params, surface);
        if (ui_error != PF_Err_NONE) {
            return ui_error;
        }
        InitializePendingLatticeForInput(
            in_data,
            params,
            surface);
        const PF_Handle handle =
            params[SurfaceLatticeParam(surface)]->u.arb_d.value;
        if (!handle) {
            continue;
        }
        auto* lattice =
            static_cast<LatticeData*>(PF_LOCK_HANDLE(handle));
        if (lattice && IsValidLattice(*lattice)) {
            params[SurfaceParam(surface, kSurfaceDivisionsXOffset)]
                ->u.sd.value = lattice->divisions_x;
            params[SurfaceParam(surface, kSurfaceDivisionsYOffset)]
                ->u.sd.value = lattice->divisions_y;
        }
        if (lattice) {
            PF_UNLOCK_HANDLE(handle);
        }
    }
    const PF_Err animation_ui_error =
        UpdatePointAnimationUi(in_data, params);
    if (animation_ui_error != PF_Err_NONE) {
        return animation_ui_error;
    }
    return PublishRigBridge(in_data, params);
}

PF_Err HandleSurfaceGizmoEvent(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_ParamDef* params[],
    PF_EventExtra* event_extra) {
    if (!event_extra || !event_extra->contextH ||
        (*event_extra->contextH)->w_type != PF_Window_COMP ||
        !params || !params[kParamInput]) {
        return PF_Err_NONE;
    }
    if (event_extra->e_type != PF_Event_DRAW &&
        event_extra->e_type != PF_Event_DO_CLICK &&
        event_extra->e_type != PF_Event_DRAG &&
        event_extra->e_type != PF_Event_ADJUST_CURSOR &&
        event_extra->e_type != PF_Event_MOUSE_EXITED) {
        return PF_Err_NONE;
    }

    const A_long width = params[kParamInput]->u.ld.width > 0
                             ? params[kParamInput]->u.ld.width
                             : in_data->width;
    const A_long height = params[kParamInput]->u.ld.height > 0
                              ? params[kParamInput]->u.ld.height
                              : in_data->height;
    for (std::uint32_t surface = 0;
         surface < kSurfaceCount;
         ++surface) {
        InitializePendingLatticeForInput(
            in_data,
            params,
            surface);
    }
    SceneData scene =
        ResolveSceneForFrame(in_data, params, width, height);
    const CameraState camera =
        BuildGizmoCamera(in_data, params, width, height);
    const NullPointOverrideState null_overrides =
        ResolveNullPointOverrides(
            in_data,
            scene,
            camera,
            1.0,
            1.0,
            1.0);
    if (!g_selection.dragging &&
        !g_selection.entities.empty()) {
        RebuildSelectionPoints(scene, null_overrides);
    }

    if (event_extra->e_type == PF_Event_MOUSE_EXITED) {
        g_selection.axis_hover = TranslateAxis::None;
        g_selection.rotate_axis_hover = RotateAxis::None;
        g_selection.uniform_scale_hover = false;
        event_extra->evt_out_flags = static_cast<PF_EventOutFlags>(
            PF_EO_HANDLED_EVENT | PF_EO_ALWAYS_UPDATE);
        return PF_Err_NONE;
    }

    if (event_extra->e_type == PF_Event_ADJUST_CURSOR) {
        g_selection.axis_hover = TranslateAxis::None;
        g_selection.rotate_axis_hover = RotateAxis::None;
        g_selection.uniform_scale_hover = false;
        Point3 centroid{};
        if (!g_selection.points.empty() &&
            !g_selection.marquee_active &&
            ComputeSelectionCentroid(scene, centroid)) {
            const Point2 hover_mouse{
                static_cast<double>(
                    event_extra->u.adjust_cursor.screen_point.h),
                static_cast<double>(
                    event_extra->u.adjust_cursor.screen_point.v)};
            const A_long hover_tool = std::clamp<A_long>(
                params[kParamTransformTool]->u.pd.value,
                kTransformToolMove,
                kTransformToolScale);
            const A_long hover_space = std::clamp<A_long>(
                params[kParamTransformSpace]->u.pd.value,
                kTransformSpaceLocal,
                kTransformSpaceWorld);
            if (hover_tool == kTransformToolRotate) {
                g_selection.rotate_axis_hover = HitTestRotateRings(
                    in_data,
                    event_extra,
                    scene,
                    camera,
                    centroid,
                    hover_space,
                    hover_mouse);
            } else if (hover_tool == kTransformToolScale) {
                g_selection.uniform_scale_hover =
                    HitTestUniformScaleHandle(
                        in_data,
                        event_extra,
                        scene,
                        camera,
                        centroid,
                        hover_mouse);
                if (!g_selection.uniform_scale_hover) {
                    g_selection.axis_hover = HitTestTranslateAxes(
                        in_data,
                        event_extra,
                        scene,
                        camera,
                        centroid,
                        hover_space,
                        hover_mouse);
                }
            } else {
                g_selection.axis_hover = HitTestTranslateAxes(
                    in_data,
                    event_extra,
                    scene,
                    camera,
                    centroid,
                    hover_space,
                    hover_mouse);
            }
        }
        const bool over_gizmo =
            g_selection.axis_hover != TranslateAxis::None ||
            g_selection.rotate_axis_hover != RotateAxis::None ||
            g_selection.uniform_scale_hover;
        if (over_gizmo) {
            event_extra->u.adjust_cursor.set_cursor =
                PF_Cursor_CROSSHAIRS;
        }
        event_extra->evt_out_flags = static_cast<PF_EventOutFlags>(
            PF_EO_HANDLED_EVENT | PF_EO_ALWAYS_UPDATE);
        return PF_Err_NONE;
    }

    if (event_extra->e_type == PF_Event_DRAW) {
        DRAWBOT_Suites drawbot{};
        PF_Err error =
            AEFX_AcquireDrawbotSuites(in_data, out_data, &drawbot);
        if (error != PF_Err_NONE) {
            return error;
        }
        AEGP_SuiteHandler suites(in_data->pica_basicP);
        DRAWBOT_DrawRef drawing_ref = nullptr;
        DRAWBOT_SurfaceRef drawing_surface = nullptr;
        DRAWBOT_SupplierRef supplier = nullptr;
        error = suites.EffectCustomUISuite1()->PF_GetDrawingReference(
            event_extra->contextH,
            &drawing_ref);
        if (error == PF_Err_NONE) {
            error = suites.DrawbotSuiteCurrent()->GetSurface(
                drawing_ref,
                &drawing_surface);
        }
        if (error == PF_Err_NONE) {
            error = drawbot.drawbot_suiteP->GetSupplier(
                drawing_ref,
                &supplier);
        }
        if (error != PF_Err_NONE || !drawing_surface || !supplier) {
            AEFX_ReleaseDrawbotSuites(in_data, out_data);
            return error;
        }

        const DRAWBOT_ColorRGBA selected_point_color{
            1.0F, 0.86F, 0.20F, 1.0F};
        const bool has_active_surface =
            !g_selection.points.empty() &&
            g_selection.primary.surface < scene.surface_count;
        const std::uint32_t active_surface =
            has_active_surface
                ? g_selection.primary.surface
                : scene.surface_count;
        // Draw the active surface last so its cage is never buried by another
        // overlapping surface. With no selection, preserve numeric order and
        // the original equal-emphasis display.
        std::array<std::uint32_t, kMaximumSurfaces> draw_order{};
        std::uint32_t draw_count = 0;
        for (std::uint32_t surface_index = 0;
             surface_index < scene.surface_count;
             ++surface_index) {
            if (surface_index != active_surface) {
                draw_order[draw_count++] = surface_index;
            }
        }
        if (has_active_surface) {
            draw_order[draw_count++] = active_surface;
        }
        for (std::uint32_t draw_index = 0;
             draw_index < draw_count;
             ++draw_index) {
            const std::uint32_t surface_index =
                draw_order[draw_index];
            const SurfaceData& surface =
                scene.surfaces[surface_index];
            if (surface.enabled == 0 ||
                !IsValidLattice(surface.lattice)) {
                continue;
            }
            const bool active =
                has_active_surface &&
                surface_index == active_surface;
            const bool inactive =
                has_active_surface && !active;
            const DRAWBOT_ColorRGBA grid_color =
                active
                    ? DRAWBOT_ColorRGBA{
                          0.08F, 0.86F, 1.0F, 1.0F}
                    : inactive
                          ? DRAWBOT_ColorRGBA{
                                0.22F, 0.52F, 0.62F, 0.34F}
                          : DRAWBOT_ColorRGBA{
                                0.12F, 0.78F, 1.0F, 0.82F};
            const DRAWBOT_ColorRGBA point_color =
                active
                    ? DRAWBOT_ColorRGBA{
                          0.94F, 0.99F, 1.0F, 1.0F}
                    : inactive
                          ? DRAWBOT_ColorRGBA{
                                0.62F, 0.72F, 0.75F, 0.50F}
                          : DRAWBOT_ColorRGBA{
                                0.93F, 0.98F, 1.0F, 1.0F};
            const DRAWBOT_ColorRGBA controlled_point_color =
                inactive
                    ? DRAWBOT_ColorRGBA{
                          0.82F, 0.46F, 0.10F, 0.55F}
                    : DRAWBOT_ColorRGBA{
                          1.0F, 0.63F, 0.12F, 1.0F};
            DRAWBOT_PathP grid(
                drawbot.supplier_suiteP,
                supplier);
            constexpr int kCurveSamples = 32;
            for (std::uint16_t row = 0;
                 row <= surface.lattice.divisions_y;
                 ++row) {
                bool started = false;
                for (int sample = 0;
                     sample <= kCurveSamples;
                     ++sample) {
                    Point2 point;
                    if (!ProjectSurfacePointToFrame(
                            in_data,
                            event_extra,
                            surface,
                            camera,
                            static_cast<double>(sample) /
                                kCurveSamples,
                            static_cast<double>(row) /
                                surface.lattice.divisions_y,
                            point)) {
                        started = false;
                        continue;
                    }
                    if (!started) {
                        drawbot.path_suiteP->MoveTo(
                            grid,
                            static_cast<float>(point.x),
                            static_cast<float>(point.y));
                        started = true;
                    } else {
                        drawbot.path_suiteP->LineTo(
                            grid,
                            static_cast<float>(point.x),
                            static_cast<float>(point.y));
                    }
                }
            }
            for (std::uint16_t column = 0;
                 column <= surface.lattice.divisions_x;
                 ++column) {
                bool started = false;
                for (int sample = 0;
                     sample <= kCurveSamples;
                     ++sample) {
                    Point2 point;
                    if (!ProjectSurfacePointToFrame(
                            in_data,
                            event_extra,
                            surface,
                            camera,
                            static_cast<double>(column) /
                                surface.lattice.divisions_x,
                            static_cast<double>(sample) /
                                kCurveSamples,
                            point)) {
                        started = false;
                        continue;
                    }
                    if (!started) {
                        drawbot.path_suiteP->MoveTo(
                            grid,
                            static_cast<float>(point.x),
                            static_cast<float>(point.y));
                        started = true;
                    } else {
                        drawbot.path_suiteP->LineTo(
                            grid,
                            static_cast<float>(point.x),
                            static_cast<float>(point.y));
                    }
                }
            }
            DRAWBOT_PenP pen(
                drawbot.supplier_suiteP,
                supplier,
                &grid_color,
                active ? 1.75F : 1.0F);
            drawbot.surface_suiteP->StrokePath(
                drawing_surface,
                pen,
                grid);

            DRAWBOT_PathP selected_entities(
                drawbot.supplier_suiteP,
                supplier);
            bool have_selected_entity_path = false;
            const auto append_selected_curve =
                [&](double u0,
                    double v0,
                    double u1,
                    double v1,
                    int samples) {
                    bool started = false;
                    for (int sample = 0; sample <= samples; ++sample) {
                        const double t =
                            static_cast<double>(sample) / samples;
                        Point2 point;
                        if (!ProjectSurfacePointToFrame(
                                in_data,
                                event_extra,
                                surface,
                                camera,
                                u0 + (u1 - u0) * t,
                                v0 + (v1 - v0) * t,
                                point)) {
                            started = false;
                            continue;
                        }
                        if (!started) {
                            drawbot.path_suiteP->MoveTo(
                                selected_entities,
                                static_cast<float>(point.x),
                                static_cast<float>(point.y));
                            started = true;
                        } else {
                            drawbot.path_suiteP->LineTo(
                                selected_entities,
                                static_cast<float>(point.x),
                                static_cast<float>(point.y));
                            have_selected_entity_path = true;
                        }
                    }
                };
            for (const SelectionEntityRef& entity :
                 g_selection.entities) {
                if (entity.surface != surface_index) {
                    continue;
                }
                if (CollectEntityPoints(
                        scene,
                        null_overrides,
                        entity).empty()) {
                    continue;
                }
                const double u0 =
                    static_cast<double>(entity.column) /
                    surface.lattice.divisions_x;
                const double v0 =
                    static_cast<double>(entity.row) /
                    surface.lattice.divisions_y;
                const double u1 =
                    static_cast<double>(entity.column + 1) /
                    surface.lattice.divisions_x;
                const double v1 =
                    static_cast<double>(entity.row + 1) /
                    surface.lattice.divisions_y;
                switch (entity.kind) {
                    case SelectionEntityKind::EdgeHorizontal:
                        append_selected_curve(
                            u0, v0, u1, v0, 12);
                        break;
                    case SelectionEntityKind::EdgeVertical:
                        append_selected_curve(
                            u0, v0, u0, v1, 12);
                        break;
                    case SelectionEntityKind::Face:
                        append_selected_curve(
                            u0, v0, u1, v0, 12);
                        append_selected_curve(
                            u1, v0, u1, v1, 12);
                        append_selected_curve(
                            u1, v1, u0, v1, 12);
                        append_selected_curve(
                            u0, v1, u0, v0, 12);
                        break;
                    case SelectionEntityKind::Surface:
                        append_selected_curve(
                            0.0, 0.0, 1.0, 0.0, kCurveSamples);
                        append_selected_curve(
                            1.0, 0.0, 1.0, 1.0, kCurveSamples);
                        append_selected_curve(
                            1.0, 1.0, 0.0, 1.0, kCurveSamples);
                        append_selected_curve(
                            0.0, 1.0, 0.0, 0.0, kCurveSamples);
                        break;
                    case SelectionEntityKind::Row:
                        append_selected_curve(
                            0.0, v0, 1.0, v0, kCurveSamples);
                        break;
                    case SelectionEntityKind::Column:
                        append_selected_curve(
                            u0, 0.0, u0, 1.0, kCurveSamples);
                        break;
                    case SelectionEntityKind::Vertex:
                    default:
                        break;
                }
            }
            if (have_selected_entity_path) {
                const DRAWBOT_ColorRGBA selected_entity_color{
                    1.0F, 0.86F, 0.20F, 0.95F};
                DRAWBOT_PenP selected_entity_pen(
                    drawbot.supplier_suiteP,
                    supplier,
                    &selected_entity_color,
                    3.0F);
                drawbot.surface_suiteP->StrokePath(
                    drawing_surface,
                    selected_entity_pen,
                    selected_entities);
            }

            for (std::uint16_t row = 0;
                 row <= surface.lattice.divisions_y;
                 ++row) {
                for (std::uint16_t column = 0;
                     column <= surface.lattice.divisions_x;
                     ++column) {
                    const std::size_t point_index = LatticePointIndex(
                        surface.lattice.divisions_x,
                        row,
                        column);
                    Point2 point;
                    if (!ProjectSurfacePointToFrame(
                            in_data,
                            event_extra,
                            surface,
                            camera,
                            static_cast<double>(column) /
                                surface.lattice.divisions_x,
                            static_cast<double>(row) /
                                surface.lattice.divisions_y,
                            point)) {
                        continue;
                    }
                    const LatticePointRef ref{
                        surface_index,
                        row,
                        column};
                    const bool controlled = null_overrides.IsControlled(
                        surface_index,
                        point_index);
                    const bool selected =
                        !controlled && SelectionContains(ref);
                    const float half =
                        selected ? 5.0F
                        : active ? 4.0F
                        : inactive ? 2.75F
                                   : 3.5F;
                    DRAWBOT_RectF32 rect{
                        static_cast<float>(point.x - half),
                        static_cast<float>(point.y - half),
                        half * 2.0F,
                        half * 2.0F};
                    suites.SurfaceSuiteCurrent()->PaintRect(
                        drawing_surface,
                        controlled ? &controlled_point_color
                        : selected ? &selected_point_color
                                   : &point_color,
                        &rect);
                }
            }
        }

        if (g_selection.marquee_active) {
            const DRAWBOT_ColorRGBA marquee_color{
                1.0F, 0.92F, 0.35F, 0.95F};
            DRAWBOT_PathP marquee(
                drawbot.supplier_suiteP,
                supplier);
            const float x0 = static_cast<float>(
                g_selection.marquee_start.x);
            const float y0 = static_cast<float>(
                g_selection.marquee_start.y);
            const float x1 = static_cast<float>(
                g_selection.marquee_end.x);
            const float y1 = static_cast<float>(
                g_selection.marquee_end.y);
            drawbot.path_suiteP->MoveTo(marquee, x0, y0);
            drawbot.path_suiteP->LineTo(marquee, x1, y0);
            drawbot.path_suiteP->LineTo(marquee, x1, y1);
            drawbot.path_suiteP->LineTo(marquee, x0, y1);
            drawbot.path_suiteP->LineTo(marquee, x0, y0);
            DRAWBOT_PenP marquee_pen(
                drawbot.supplier_suiteP,
                supplier,
                &marquee_color,
                1.0F);
            drawbot.surface_suiteP->StrokePath(
                drawing_surface,
                marquee_pen,
                marquee);
        }

        Point3 centroid{};
        if (!g_selection.points.empty() &&
            !g_selection.marquee_active &&
            ComputeSelectionCentroid(scene, centroid)) {
            const A_long draw_tool = std::clamp<A_long>(
                params[kParamTransformTool]->u.pd.value,
                kTransformToolMove,
                kTransformToolScale);
            const A_long draw_space = std::clamp<A_long>(
                params[kParamTransformSpace]->u.pd.value,
                kTransformSpaceLocal,
                kTransformSpaceWorld);
            const DRAWBOT_ColorRGBA axis_colors[3] = {
                {0.95F, 0.28F, 0.28F, 0.95F},  // X
                {0.30F, 0.85F, 0.35F, 0.95F},  // Y
                {0.30F, 0.55F, 1.0F, 0.95F},   // Z
            };
            if (draw_tool == kTransformToolRotate) {
                const RotateAxis axes[3] = {
                    RotateAxis::X,
                    RotateAxis::Y,
                    RotateAxis::Z};
                for (int axis_index = 0;
                     axis_index < 3;
                     ++axis_index) {
                    std::vector<Point2> ring;
                    if (!BuildRotateRingScreen(
                            in_data,
                            event_extra,
                            scene,
                            camera,
                            centroid,
                            axes[axis_index],
                            draw_space,
                            ring)) {
                        continue;
                    }
                    DRAWBOT_PathP ring_path(
                        drawbot.supplier_suiteP,
                        supplier);
                    drawbot.path_suiteP->MoveTo(
                        ring_path,
                        static_cast<float>(ring.front().x),
                        static_cast<float>(ring.front().y));
                    for (std::size_t index = 1;
                         index < ring.size();
                         ++index) {
                        drawbot.path_suiteP->LineTo(
                            ring_path,
                            static_cast<float>(ring[index].x),
                            static_cast<float>(ring[index].y));
                    }
                    const bool emphasized =
                        g_selection.rotate_axis_drag ==
                            axes[axis_index] ||
                        g_selection.rotate_axis_hover ==
                            axes[axis_index];
                    const float stroke = emphasized ? 4.0F : 2.0F;
                    const DRAWBOT_ColorRGBA shadow_color{
                        0.02F, 0.02F, 0.02F, 0.78F};
                    DRAWBOT_PenP ring_shadow(
                        drawbot.supplier_suiteP,
                        supplier,
                        &shadow_color,
                        stroke + 3.0F);
                    drawbot.surface_suiteP->StrokePath(
                        drawing_surface,
                        ring_shadow,
                        ring_path);
                    DRAWBOT_PenP ring_pen(
                        drawbot.supplier_suiteP,
                        supplier,
                        &axis_colors[axis_index],
                        stroke);
                    drawbot.surface_suiteP->StrokePath(
                        drawing_surface,
                        ring_pen,
                        ring_path);
                }
            } else {
                const TranslateAxis axes[3] = {
                    TranslateAxis::X,
                    TranslateAxis::Y,
                    TranslateAxis::Z};
                for (int axis_index = 0; axis_index < 3; ++axis_index) {
                    Point2 origin;
                    Point2 tip;
                    double pixels_per_unit = 0.0;
                    if (!BuildTranslateAxisScreen(
                            in_data,
                            event_extra,
                            scene,
                            camera,
                            centroid,
                            axes[axis_index],
                            draw_space,
                            origin,
                            tip,
                            pixels_per_unit)) {
                        continue;
                    }
                    DRAWBOT_PathP axis_path(
                        drawbot.supplier_suiteP,
                        supplier);
                    drawbot.path_suiteP->MoveTo(
                        axis_path,
                        static_cast<float>(origin.x),
                        static_cast<float>(origin.y));
                    drawbot.path_suiteP->LineTo(
                        axis_path,
                        static_cast<float>(tip.x),
                        static_cast<float>(tip.y));
                    const bool emphasized =
                        g_selection.axis_drag == axes[axis_index] ||
                        g_selection.axis_hover == axes[axis_index];
                    const float stroke = emphasized ? 4.5F : 2.75F;
                    const DRAWBOT_ColorRGBA shadow_color{
                        0.02F, 0.02F, 0.02F, 0.82F};
                    DRAWBOT_PenP axis_shadow(
                        drawbot.supplier_suiteP,
                        supplier,
                        &shadow_color,
                        stroke + 3.5F);
                    drawbot.surface_suiteP->StrokePath(
                        drawing_surface,
                        axis_shadow,
                        axis_path);
                    DRAWBOT_PenP axis_pen(
                        drawbot.supplier_suiteP,
                        supplier,
                        &axis_colors[axis_index],
                        stroke);
                    drawbot.surface_suiteP->StrokePath(
                        drawing_surface,
                        axis_pen,
                        axis_path);
                    if (draw_tool == kTransformToolMove) {
                        const double dx = tip.x - origin.x;
                        const double dy = tip.y - origin.y;
                        const double length =
                            std::sqrt(dx * dx + dy * dy);
                        if (length > 1.0e-6) {
                            const double ux = dx / length;
                            const double uy = dy / length;
                            const double arrow_length =
                                emphasized ? 16.0 : 13.0;
                            const double arrow_half_width =
                                emphasized ? 7.5 : 6.0;
                            const Point2 base{
                                tip.x - ux * arrow_length,
                                tip.y - uy * arrow_length};
                            DRAWBOT_PathP arrow(
                                drawbot.supplier_suiteP,
                                supplier);
                            drawbot.path_suiteP->MoveTo(
                                arrow,
                                static_cast<float>(tip.x),
                                static_cast<float>(tip.y));
                            drawbot.path_suiteP->LineTo(
                                arrow,
                                static_cast<float>(
                                    base.x - uy * arrow_half_width),
                                static_cast<float>(
                                    base.y + ux * arrow_half_width));
                            drawbot.path_suiteP->LineTo(
                                arrow,
                                static_cast<float>(
                                    base.x + uy * arrow_half_width),
                                static_cast<float>(
                                    base.y - ux * arrow_half_width));
                            drawbot.path_suiteP->Close(arrow);
                            DRAWBOT_BrushP arrow_brush(
                                drawbot.supplier_suiteP,
                                supplier,
                                &axis_colors[axis_index]);
                            drawbot.surface_suiteP->FillPath(
                                drawing_surface,
                                arrow_brush,
                                arrow,
                                kDRAWBOT_FillType_Default);
                        }
                    } else {
                        const float half = emphasized ? 6.0F : 4.5F;
                        DRAWBOT_RectF32 tip_rect{
                            static_cast<float>(tip.x - half),
                            static_cast<float>(tip.y - half),
                            half * 2.0F,
                            half * 2.0F};
                        suites.SurfaceSuiteCurrent()->PaintRect(
                            drawing_surface,
                            &axis_colors[axis_index],
                            &tip_rect);
                    }
                }
                Point2 hub;
                if (ProjectSelectionCentroidToFrame(
                        in_data,
                        event_extra,
                        scene,
                        camera,
                        centroid,
                        hub)) {
                    const DRAWBOT_ColorRGBA hub_color{
                        0.96F, 0.96F, 0.96F, 0.98F};
                    const DRAWBOT_ColorRGBA hub_shadow{
                        0.02F, 0.02F, 0.02F, 0.85F};
                    DRAWBOT_RectF32 shadow_rect{
                        static_cast<float>(hub.x - 6.0),
                        static_cast<float>(hub.y - 6.0),
                        12.0F,
                        12.0F};
                    suites.SurfaceSuiteCurrent()->PaintRect(
                        drawing_surface,
                        &hub_shadow,
                        &shadow_rect);
                    DRAWBOT_RectF32 hub_rect{
                        static_cast<float>(hub.x - 3.5),
                        static_cast<float>(hub.y - 3.5),
                        7.0F,
                        7.0F};
                    suites.SurfaceSuiteCurrent()->PaintRect(
                        drawing_surface,
                        &hub_color,
                        &hub_rect);
                }
            }
            if (draw_tool == kTransformToolScale) {
                Point2 origin;
                Point2 tip;
                if (BuildUniformScaleHandle(
                        in_data,
                        event_extra,
                        scene,
                        camera,
                        centroid,
                        origin,
                        tip)) {
                    const DRAWBOT_ColorRGBA uniform_color{
                        0.95F, 0.95F, 0.95F, 0.95F};
                    DRAWBOT_PathP uniform_path(
                        drawbot.supplier_suiteP,
                        supplier);
                    drawbot.path_suiteP->MoveTo(
                        uniform_path,
                        static_cast<float>(origin.x),
                        static_cast<float>(origin.y));
                    drawbot.path_suiteP->LineTo(
                        uniform_path,
                        static_cast<float>(tip.x),
                        static_cast<float>(tip.y));
                    DRAWBOT_PenP uniform_pen(
                        drawbot.supplier_suiteP,
                        supplier,
                        &uniform_color,
                        g_selection.uniform_scale_drag ||
                                g_selection.uniform_scale_hover
                            ? 4.0F
                            : 2.0F);
                    drawbot.surface_suiteP->StrokePath(
                        drawing_surface,
                        uniform_pen,
                        uniform_path);
                    const float uniform_half =
                        g_selection.uniform_scale_drag ||
                                g_selection.uniform_scale_hover
                            ? 6.0F
                            : 4.5F;
                    DRAWBOT_RectF32 uniform_tip{
                        static_cast<float>(tip.x - uniform_half),
                        static_cast<float>(tip.y - uniform_half),
                        uniform_half * 2.0F,
                        uniform_half * 2.0F};
                    suites.SurfaceSuiteCurrent()->PaintRect(
                        drawing_surface,
                        &uniform_color,
                        &uniform_tip);
                }
            }
        }

        AEFX_ReleaseDrawbotSuites(in_data, out_data);
        event_extra->evt_out_flags = PF_EO_HANDLED_EVENT;
        return PF_Err_NONE;
    }

    const Point2 mouse{
        static_cast<double>(
            event_extra->u.do_click.screen_point.h),
        static_cast<double>(
            event_extra->u.do_click.screen_point.v)};
    const A_long edit_mode = std::clamp<A_long>(
        params[kParamEditMode]->u.pd.value,
        kEditModeVertex,
        kEditModeSurface);
    const A_long transform_tool = std::clamp<A_long>(
        params[kParamTransformTool]->u.pd.value,
        kTransformToolMove,
        kTransformToolScale);
    const A_long transform_space = std::clamp<A_long>(
        params[kParamTransformSpace]->u.pd.value,
        kTransformSpaceLocal,
        kTransformSpaceWorld);
    if (event_extra->e_type == PF_Event_DO_CLICK) {
        const bool shift =
            (event_extra->u.do_click.modifiers & PF_Mod_SHIFT_KEY) != 0;
        const bool command =
            (event_extra->u.do_click.modifiers &
             PF_Mod_CMD_CTRL_KEY) != 0;
        g_selection.dragging = false;
        g_selection.drag_moved = false;
        g_selection.has_snapshot = false;
        g_selection.drag_origin_seeded = false;
        g_selection.marquee_active = false;
        g_selection.axis_drag = TranslateAxis::None;
        g_selection.rotate_axis_drag = RotateAxis::None;
        g_selection.uniform_scale_drag = false;
        g_selection.transform_tool_drag = transform_tool;
        g_selection.transform_space_drag = transform_space;

        // Cmd/Ctrl-drag starts a same-surface marquee (Foldspace-style).
        if (command) {
            g_selection.marquee_active = true;
            g_selection.marquee_additive = shift;
            g_selection.marquee_start = mouse;
            g_selection.marquee_end = mouse;
            if (!shift) {
                g_selection.entities.clear();
                g_selection.points.clear();
                g_selection.primary = {};
            }
            ApplyMarqueeSelection(
                in_data,
                event_extra,
                scene,
                camera,
                null_overrides);
            BeginCompDrag(event_extra);
            event_extra->evt_out_flags =
                static_cast<PF_EventOutFlags>(
                    PF_EO_HANDLED_EVENT | PF_EO_ALWAYS_UPDATE);
            return PF_Err_NONE;
        }

        Point3 centroid{};
        if (!g_selection.points.empty() &&
            ComputeSelectionCentroid(scene, centroid)) {
            bool gizmo_hit = false;
            if (transform_tool == kTransformToolRotate) {
                g_selection.rotate_axis_drag = HitTestRotateRings(
                    in_data,
                    event_extra,
                    scene,
                    camera,
                    centroid,
                    transform_space,
                    mouse);
                gizmo_hit =
                    g_selection.rotate_axis_drag != RotateAxis::None;
            } else if (transform_tool == kTransformToolScale) {
                g_selection.uniform_scale_drag =
                    HitTestUniformScaleHandle(
                        in_data,
                        event_extra,
                        scene,
                        camera,
                        centroid,
                        mouse);
                if (!g_selection.uniform_scale_drag) {
                    g_selection.axis_drag = HitTestTranslateAxes(
                        in_data,
                        event_extra,
                        scene,
                        camera,
                        centroid,
                        transform_space,
                        mouse);
                }
                gizmo_hit =
                    g_selection.uniform_scale_drag ||
                    g_selection.axis_drag != TranslateAxis::None;
            } else {
                g_selection.axis_drag = HitTestTranslateAxes(
                    in_data,
                    event_extra,
                    scene,
                    camera,
                    centroid,
                    transform_space,
                    mouse);
                gizmo_hit =
                    g_selection.axis_drag != TranslateAxis::None;
            }
            if (gizmo_hit) {
                g_selection.dragging = CaptureDragSnapshot(
                    in_data,
                    params,
                    g_selection.points.front().surface,
                    mouse,
                    centroid);
                if (g_selection.dragging) {
                    BeginCompDrag(event_extra);
                } else {
                    g_selection.axis_drag = TranslateAxis::None;
                    g_selection.rotate_axis_drag = RotateAxis::None;
                    g_selection.uniform_scale_drag = false;
                }
                event_extra->evt_out_flags =
                    static_cast<PF_EventOutFlags>(
                        PF_EO_HANDLED_EVENT | PF_EO_ALWAYS_UPDATE);
                return PF_Err_NONE;
            }
        }

        constexpr double kPointHitRadiusSquared = 100.0;
        constexpr double kEdgeHitRadiusSquared = 64.0;
        SelectionEntityRef entity_hit{};
        bool found_entity = false;

        if (edit_mode == kEditModeVertex) {
            double closest = kPointHitRadiusSquared;
            for (std::uint32_t surface_index = 0;
                 surface_index < scene.surface_count;
                 ++surface_index) {
                const SurfaceData& surface =
                    scene.surfaces[surface_index];
                if (surface.enabled == 0) {
                    continue;
                }
                for (std::uint16_t row = 0;
                     row <= surface.lattice.divisions_y;
                     ++row) {
                    for (std::uint16_t column = 0;
                         column <= surface.lattice.divisions_x;
                         ++column) {
                        const std::size_t point_index =
                            LatticePointIndex(
                                surface.lattice.divisions_x,
                                row,
                                column);
                        if (null_overrides.IsControlled(
                                surface_index,
                                point_index)) {
                            continue;
                        }
                        Point2 point;
                        if (!ProjectControlPointToFrame(
                                in_data,
                                event_extra,
                                surface,
                                camera,
                                row,
                                column,
                                point)) {
                            continue;
                        }
                        const double dx = mouse.x - point.x;
                        const double dy = mouse.y - point.y;
                        const double distance = dx * dx + dy * dy;
                        if (distance <= closest) {
                            closest = distance;
                            entity_hit = {
                                surface_index,
                                SelectionEntityKind::Vertex,
                                row,
                                column};
                            found_entity = true;
                        }
                    }
                }
            }
            if (!found_entity) {
                LatticeLineRef line_hit{};
                if (HitTestLatticeLine(
                        in_data,
                        event_extra,
                        scene,
                        camera,
                        mouse,
                        kEdgeHitRadiusSquared,
                        line_hit)) {
                    entity_hit = {
                        line_hit.surface,
                        line_hit.kind == LatticeLineKind::Row
                            ? SelectionEntityKind::Row
                            : SelectionEntityKind::Column,
                        static_cast<std::uint16_t>(
                            line_hit.kind == LatticeLineKind::Row
                                ? line_hit.index
                                : 0),
                        static_cast<std::uint16_t>(
                            line_hit.kind == LatticeLineKind::Column
                                ? line_hit.index
                                : 0)};
                    found_entity = true;
                }
            }
        } else if (edit_mode == kEditModeEdge) {
            found_entity = HitTestEdgeEntity(
                in_data,
                event_extra,
                scene,
                camera,
                mouse,
                kEdgeHitRadiusSquared,
                entity_hit);
            if (found_entity &&
                event_extra->u.do_click.num_clicks >= 2) {
                if (entity_hit.kind ==
                    SelectionEntityKind::EdgeHorizontal) {
                    entity_hit.kind = SelectionEntityKind::Row;
                    entity_hit.column = 0;
                } else {
                    entity_hit.kind = SelectionEntityKind::Column;
                    entity_hit.row = 0;
                }
            }
        } else {
            SelectionEntityRef face_hit{};
            found_entity = HitTestFaceEntity(
                in_data,
                event_extra,
                scene,
                camera,
                mouse,
                face_hit);
            if (found_entity) {
                entity_hit = face_hit;
                if (edit_mode == kEditModeSurface) {
                    entity_hit.kind = SelectionEntityKind::Surface;
                    entity_hit.row = 0;
                    entity_hit.column = 0;
                }
            }
        }

        if (found_entity) {
            if (shift) {
                ToggleSelectionEntity(
                    scene,
                    null_overrides,
                    entity_hit);
            } else {
                const bool already_selected = std::any_of(
                    g_selection.entities.begin(),
                    g_selection.entities.end(),
                    [&](const SelectionEntityRef& candidate) {
                        return SameSelectionEntity(
                            candidate,
                            entity_hit);
                    });
                if (!already_selected) {
                    SetSelectionEntity(
                        scene,
                        null_overrides,
                        entity_hit);
                }
            }
            if (!g_selection.points.empty() &&
                transform_tool == kTransformToolMove) {
                Point3 entity_centroid{};
                ComputeSelectionCentroid(scene, entity_centroid);
                g_selection.dragging = CaptureDragSnapshot(
                    in_data,
                    params,
                    g_selection.primary.surface,
                    mouse,
                    entity_centroid);
                if (g_selection.dragging) {
                    BeginCompDrag(event_extra);
                }
            }
            event_extra->evt_out_flags =
                static_cast<PF_EventOutFlags>(
                    PF_EO_HANDLED_EVENT | PF_EO_ALWAYS_UPDATE);
            return PF_Err_NONE;
        }

        if (!shift && !g_selection.points.empty()) {
            ClearSelection();
            event_extra->evt_out_flags =
                static_cast<PF_EventOutFlags>(
                    PF_EO_HANDLED_EVENT | PF_EO_ALWAYS_UPDATE);
        }
        return PF_Err_NONE;
    }

    // Lattice mutation is DRAG-only. DO_CLICK arms selection + send_drag;
    // a zero-length click (last_time without motion) must not write the lattice.
    if (event_extra->e_type != PF_Event_DRAG) {
        return PF_Err_NONE;
    }
    event_extra->u.do_click.send_drag = TRUE;

    if (g_selection.marquee_active) {
        g_selection.marquee_end = mouse;
        ApplyMarqueeSelection(
            in_data,
            event_extra,
            scene,
            camera,
            null_overrides);
        event_extra->evt_out_flags = static_cast<PF_EventOutFlags>(
            PF_EO_HANDLED_EVENT |
            PF_EO_ALWAYS_UPDATE |
            PF_EO_UPDATE_NOW);
        EndCompDragIfFinished(event_extra);
        return PF_Err_NONE;
    }

    if (!g_selection.dragging ||
        !g_selection.has_snapshot ||
        g_selection.points.empty() ||
        g_selection.primary.surface >= scene.surface_count) {
        EndCompDragIfFinished(event_extra);
        return PF_Err_NONE;
    }

    // 1) AE can report a different coordinate space between DO_CLICK and the
    // first DRAG. Only re-seed when that first step is an actual teleport;
    // a normal short gesture may contain just one DRAG sample and must still
    // transform the selection.
    if (!g_selection.drag_origin_seeded) {
        g_selection.drag_origin_seeded = true;
        const double first_x = mouse.x - g_selection.mouse_down.x;
        const double first_y = mouse.y - g_selection.mouse_down.y;
        if (std::hypot(first_x, first_y) > kMaxDragStepPixels) {
            g_selection.mouse_down = mouse;
            g_selection.last_mouse = mouse;
            event_extra->evt_out_flags = static_cast<PF_EventOutFlags>(
                PF_EO_HANDLED_EVENT | PF_EO_ALWAYS_UPDATE);
            EndCompDragIfFinished(event_extra);
            return PF_Err_NONE;
        }
    }

    // 2) Reject mid-drag teleports. last_mouse alone is not enough: absolute
    // deltas use mouse_down, so the origin must jump by the same step or the
    // next small move still looks like a multi-hundred-pixel drag.
    const double step_x = mouse.x - g_selection.last_mouse.x;
    const double step_y = mouse.y - g_selection.last_mouse.y;
    const double step = std::sqrt(step_x * step_x + step_y * step_y);
    if (step > kMaxDragStepPixels) {
        g_selection.mouse_down.x += step_x;
        g_selection.mouse_down.y += step_y;
        g_selection.last_mouse = mouse;
        event_extra->evt_out_flags = static_cast<PF_EventOutFlags>(
            PF_EO_HANDLED_EVENT | PF_EO_ALWAYS_UPDATE);
        EndCompDragIfFinished(event_extra);
        return PF_Err_NONE;
    }

    // Total motion from the seeded origin (absolute, not cumulative).
    const double screen_x = mouse.x - g_selection.mouse_down.x;
    const double screen_y = mouse.y - g_selection.mouse_down.y;
    const double screen_distance = std::sqrt(
        screen_x * screen_x + screen_y * screen_y);
    if (screen_distance < 0.75) {
        g_selection.last_mouse = mouse;
        EndCompDragIfFinished(event_extra);
        event_extra->evt_out_flags = static_cast<PF_EventOutFlags>(
            PF_EO_HANDLED_EVENT | PF_EO_ALWAYS_UPDATE);
        return PF_Err_NONE;
    }
    if (event_extra->u.do_click.last_time && !g_selection.drag_moved &&
        screen_distance < 2.0) {
        EndCompDragIfFinished(event_extra);
        return PF_Err_NONE;
    }
    g_selection.drag_moved = true;

    float apply_x = 0.0F;
    float apply_y = 0.0F;
    float apply_z = 0.0F;
    double rotation_angle = 0.0;
    double scale_x = 1.0;
    double scale_y = 1.0;
    double scale_z = 1.0;

    // Evaluate Jacobian against the pre-drag snapshot so the mapping is stable.
    SurfaceData surface = scene.surfaces[g_selection.primary.surface];
    surface.lattice = g_selection.drag_snapshot;
    if (!IsValidLattice(surface.lattice)) {
        EndCompDragIfFinished(event_extra);
        return PF_Err_NONE;
    }
    const bool world_space =
        g_selection.transform_space_drag == kTransformSpaceWorld;
    Affine3D cage_to_world;
    Affine3D world_to_cage;
    Point3 world_centroid{};
    if (world_space) {
        if (!BuildCageToWorldTransform(
                surface,
                camera,
                cage_to_world) ||
            !TryInvertAffine3D(
                cage_to_world,
                world_to_cage)) {
            EndCompDragIfFinished(event_extra);
            return PF_Err_NONE;
        }
        world_centroid = ApplyAffine3D(
            cage_to_world,
            g_selection.centroid_down);
    }
    Point3 world_apply{};

    if (g_selection.transform_tool_drag == kTransformToolRotate) {
        if (g_selection.rotate_axis_drag == RotateAxis::None) {
            EndCompDragIfFinished(event_extra);
            return PF_Err_NONE;
        }
        Point2 center;
        const bool projected_center = world_space
            ? ProjectWorldPointToFrame(
                  in_data,
                  event_extra,
                  camera,
                  world_centroid,
                  center)
            : ProjectSelectionCentroidToFrame(
                  in_data,
                  event_extra,
                  scene,
                  camera,
                  g_selection.centroid_down,
                  center);
        if (!projected_center) {
            return PF_Err_NONE;
        }
        const double start_angle = std::atan2(
            g_selection.mouse_down.y - center.y,
            g_selection.mouse_down.x - center.x);
        const double current_angle = std::atan2(
            mouse.y - center.y,
            mouse.x - center.x);
        rotation_angle = current_angle - start_angle;
        constexpr double kTwoPi = 6.28318530717958647692;
        constexpr double kPi = 3.14159265358979323846;
        while (rotation_angle > kPi) {
            rotation_angle -= kTwoPi;
        }
        while (rotation_angle < -kPi) {
            rotation_angle += kTwoPi;
        }
    } else if (g_selection.transform_tool_drag == kTransformToolScale) {
        double handle_delta = 0.0;
        if (g_selection.uniform_scale_drag) {
            Point2 origin;
            Point2 tip;
            if (!BuildUniformScaleHandle(
                    in_data,
                    event_extra,
                    scene,
                    camera,
                    g_selection.centroid_down,
                    origin,
                    tip)) {
                return PF_Err_NONE;
            }
            const double dir_x = tip.x - origin.x;
            const double dir_y = tip.y - origin.y;
            const double dir_length = std::hypot(dir_x, dir_y);
            if (dir_length <= 1.0e-8) {
                return PF_Err_NONE;
            }
            handle_delta =
                (screen_x * dir_x + screen_y * dir_y) / dir_length;
        } else if (g_selection.axis_drag != TranslateAxis::None) {
            Point2 origin;
            Point2 tip;
            double pixels_per_unit = 0.0;
            if (!BuildTranslateAxisScreen(
                    in_data,
                    event_extra,
                    scene,
                    camera,
                    g_selection.centroid_down,
                    g_selection.axis_drag,
                    g_selection.transform_space_drag,
                    origin,
                    tip,
                    pixels_per_unit)) {
                return PF_Err_NONE;
            }
            const double dir_x = tip.x - origin.x;
            const double dir_y = tip.y - origin.y;
            const double dir_length = std::hypot(dir_x, dir_y);
            if (dir_length <= 1.0e-8) {
                return PF_Err_NONE;
            }
            handle_delta =
                (screen_x * dir_x + screen_y * dir_y) / dir_length;
        } else {
            EndCompDragIfFinished(event_extra);
            return PF_Err_NONE;
        }
        const double factor = std::clamp(
            std::exp(handle_delta / 100.0),
            0.01,
            100.0);
        if (g_selection.uniform_scale_drag) {
            scale_x = factor;
            scale_y = factor;
            scale_z = factor;
        } else {
            switch (g_selection.axis_drag) {
                case TranslateAxis::X:
                    scale_x = factor;
                    break;
                case TranslateAxis::Y:
                    scale_y = factor;
                    break;
                case TranslateAxis::Z:
                    scale_z = factor;
                    break;
                case TranslateAxis::None:
                default:
                    break;
            }
        }
    } else if (g_selection.axis_drag != TranslateAxis::None) {
        Point2 origin;
        Point2 tip;
        double pixels_per_unit = 0.0;
        if (!BuildTranslateAxisScreen(
                in_data,
                event_extra,
                scene,
                camera,
                g_selection.centroid_down,
                g_selection.axis_drag,
                g_selection.transform_space_drag,
                origin,
                tip,
                pixels_per_unit) ||
            pixels_per_unit < kMinAxisPixelsPerUnit) {
            g_selection.last_mouse = mouse;
            return PF_Err_NONE;
        }
        const double dir_x = tip.x - origin.x;
        const double dir_y = tip.y - origin.y;
        const double dir_length = std::sqrt(dir_x * dir_x + dir_y * dir_y);
        if (dir_length <= 1.0e-8) {
            return PF_Err_NONE;
        }
        const double axis_delta =
            (screen_x * dir_x + screen_y * dir_y) /
            (dir_length * pixels_per_unit);
        const Point3 unit = AxisUnit(g_selection.axis_drag);
        const Point3 delta{
            std::clamp(
                unit.x * axis_delta,
                -kMaxCageDelta,
                kMaxCageDelta),
            std::clamp(
                unit.y * axis_delta,
                -kMaxCageDelta,
                kMaxCageDelta),
            std::clamp(
                unit.z * axis_delta,
                -kMaxCageDelta,
                kMaxCageDelta)};
        if (world_space) {
            world_apply = delta;
        } else {
            apply_x = static_cast<float>(delta.x);
            apply_y = static_cast<float>(delta.y);
            apply_z = static_cast<float>(delta.z);
            g_selection.selection_centroid = {
                g_selection.centroid_down.x + apply_x,
                g_selection.centroid_down.y + apply_y,
                g_selection.centroid_down.z + apply_z};
        }
    } else if (world_space) {
        const std::size_t primary_index = LatticePointIndex(
            surface.lattice.divisions_x,
            g_selection.primary.row,
            g_selection.primary.column);
        if (primary_index >= surface.lattice.point_count ||
            null_overrides.IsControlled(
                g_selection.primary.surface,
                primary_index)) {
            ClearSelection();
            return PF_Err_NONE;
        }
        const StoredPoint3& primary =
            g_selection.drag_snapshot.points[primary_index];
        const Point3 anchor_world = ApplyAffine3D(
            cage_to_world,
            {primary.x, primary.y, primary.z});
        Point2 origin;
        Point2 projected_x;
        Point2 projected_y;
        if (!ProjectWorldPointToFrame(
                in_data,
                event_extra,
                camera,
                anchor_world,
                origin) ||
            !ProjectWorldPointToFrame(
                in_data,
                event_extra,
                camera,
                {anchor_world.x + 1.0,
                 anchor_world.y,
                 anchor_world.z},
                projected_x) ||
            !ProjectWorldPointToFrame(
                in_data,
                event_extra,
                camera,
                {anchor_world.x,
                 anchor_world.y + 1.0,
                 anchor_world.z},
                projected_y)) {
            return PF_Err_NONE;
        }
        const double jxx = projected_x.x - origin.x;
        const double jyx = projected_x.y - origin.y;
        const double jxy = projected_y.x - origin.x;
        const double jyy = projected_y.y - origin.y;
        const double jx_len = std::hypot(jxx, jyx);
        const double jy_len = std::hypot(jxy, jyy);
        const double determinant = jxx * jyy - jxy * jyx;
        const bool depth_drag =
            (event_extra->u.do_click.modifiers &
             PF_Mod_OPT_ALT_KEY) != 0;
        if (!depth_drag) {
            const double column_area = jx_len * jy_len;
            if (jx_len < kMinAxisPixelsPerUnit ||
                jy_len < kMinAxisPixelsPerUnit ||
                column_area <= 1.0e-12 ||
                std::abs(determinant) / column_area <
                    kMinJacobianSinAngle) {
                g_selection.last_mouse = mouse;
                return PF_Err_NONE;
            }
        }
        double delta_z = 0.0;
        if (depth_drag) {
            Point2 projected_z;
            if (!ProjectWorldPointToFrame(
                    in_data,
                    event_extra,
                    camera,
                    {anchor_world.x,
                     anchor_world.y,
                     anchor_world.z + 1.0},
                    projected_z)) {
                return PF_Err_NONE;
            }
            const double jzx = projected_z.x - origin.x;
            const double jzy = projected_z.y - origin.y;
            const double length_squared = jzx * jzx + jzy * jzy;
            if (length_squared <
                kMinAxisPixelsPerUnit * kMinAxisPixelsPerUnit) {
                g_selection.last_mouse = mouse;
                return PF_Err_NONE;
            }
            delta_z =
                (screen_x * jzx + screen_y * jzy) /
                length_squared;
        }
        const double delta_x = depth_drag
                                   ? 0.0
                                   : (screen_x * jyy -
                                      screen_y * jxy) / determinant;
        const double delta_y = depth_drag
                                   ? 0.0
                                   : (jxx * screen_y -
                                      jyx * screen_x) / determinant;
        if (!std::isfinite(delta_x) ||
            !std::isfinite(delta_y) ||
            !std::isfinite(delta_z) ||
            std::abs(delta_x) > kMaxCageDelta ||
            std::abs(delta_y) > kMaxCageDelta ||
            std::abs(delta_z) > kMaxCageDelta) {
            g_selection.last_mouse = mouse;
            return PF_Err_NONE;
        }
        world_apply = {delta_x, delta_y, delta_z};
    } else {
        const std::size_t primary_index = LatticePointIndex(
            surface.lattice.divisions_x,
            g_selection.primary.row,
            g_selection.primary.column);
        if (primary_index >= surface.lattice.point_count ||
            null_overrides.IsControlled(
                g_selection.primary.surface,
                primary_index)) {
            ClearSelection();
            return PF_Err_NONE;
        }
        const double u =
            static_cast<double>(g_selection.primary.column) /
            surface.lattice.divisions_x;
        const double v =
            static_cast<double>(g_selection.primary.row) /
            surface.lattice.divisions_y;
        Point2 origin;
        if (!ProjectSurfacePointToFrame(
                in_data,
                event_extra,
                surface,
                camera,
                u,
                v,
                origin)) {
            return PF_Err_NONE;
        }
        SurfaceData probe_x = surface;
        probe_x.lattice.points[primary_index].x += 1.0F;
        SurfaceData probe_y = surface;
        probe_y.lattice.points[primary_index].y += 1.0F;
        Point2 projected_x;
        Point2 projected_y;
        if (!ProjectSurfacePointToFrame(
                in_data,
                event_extra,
                probe_x,
                camera,
                u,
                v,
                projected_x) ||
            !ProjectSurfacePointToFrame(
                in_data,
                event_extra,
                probe_y,
                camera,
                u,
                v,
                projected_y)) {
            return PF_Err_NONE;
        }
        const double jxx = projected_x.x - origin.x;
        const double jyx = projected_x.y - origin.y;
        const double jxy = projected_y.x - origin.x;
        const double jyy = projected_y.y - origin.y;
        const double jx_len = std::sqrt(jxx * jxx + jyx * jyx);
        const double jy_len = std::sqrt(jxy * jxy + jyy * jyy);
        const double determinant = jxx * jyy - jxy * jyx;
        const bool depth_drag =
            (event_extra->u.do_click.modifiers &
             PF_Mod_OPT_ALT_KEY) != 0;
        // Reject short columns and near-parallel columns. Column lengths alone
        // miss the case where both axes project long but almost collinear;
        // |det| / (|c0|*|c1|) = |sin theta| is the normalised area test.
        if (!depth_drag) {
            const double column_area = jx_len * jy_len;
            if (jx_len < kMinAxisPixelsPerUnit ||
                jy_len < kMinAxisPixelsPerUnit ||
                column_area <= 1.0e-12 ||
                std::abs(determinant) / column_area < kMinJacobianSinAngle) {
                g_selection.last_mouse = mouse;
                return PF_Err_NONE;
            }
        }
        double delta_z = 0.0;
        if (depth_drag) {
            SurfaceData probe_z = surface;
            probe_z.lattice.points[primary_index].z += 1.0F;
            Point2 projected_z;
            if (ProjectSurfacePointToFrame(
                    in_data,
                    event_extra,
                    probe_z,
                    camera,
                    u,
                    v,
                    projected_z)) {
                const double jzx = projected_z.x - origin.x;
                const double jzy = projected_z.y - origin.y;
                const double length_squared = jzx * jzx + jzy * jzy;
                if (length_squared <
                    kMinAxisPixelsPerUnit * kMinAxisPixelsPerUnit) {
                    g_selection.last_mouse = mouse;
                    return PF_Err_NONE;
                }
                delta_z = (screen_x * jzx + screen_y * jzy) / length_squared;
            } else {
                delta_z = -screen_y;
            }
        }
        const double delta_x = depth_drag
                                   ? 0.0
                                   : (screen_x * jyy -
                                      screen_y * jxy) / determinant;
        const double delta_y = depth_drag
                                   ? 0.0
                                   : (jxx * screen_y -
                                      jyx * screen_x) / determinant;
        // Ill-conditioned inverses still spike; refuse rather than clamp-write.
        if (!std::isfinite(delta_x) || !std::isfinite(delta_y) ||
            !std::isfinite(delta_z) ||
            std::abs(delta_x) > kMaxCageDelta ||
            std::abs(delta_y) > kMaxCageDelta ||
            std::abs(delta_z) > kMaxCageDelta) {
            g_selection.last_mouse = mouse;
            return PF_Err_NONE;
        }
        apply_x = static_cast<float>(delta_x);
        apply_y = static_cast<float>(delta_y);
        apply_z = static_cast<float>(delta_z);
    }

    PF_Handle handle =
        params[SurfaceLatticeParam(g_selection.primary.surface)]
            ->u.arb_d.value;
    auto* lattice =
        static_cast<LatticeData*>(PF_LOCK_HANDLE(handle));
    if (!lattice || !IsValidLattice(g_selection.drag_snapshot)) {
        if (lattice) {
            PF_UNLOCK_HANDLE(handle);
        }
        return PF_Err_NONE;
    }
    // Restore pre-drag lattice, then apply the total delta once.
    *lattice = g_selection.drag_snapshot;
    for (const LatticePointRef& ref : g_selection.points) {
        if (ref.surface != g_selection.primary.surface) {
            continue;
        }
        const std::size_t point_index = LatticePointIndex(
            lattice->divisions_x,
            ref.row,
            ref.column);
        if (point_index >= lattice->point_count ||
            null_overrides.IsControlled(ref.surface, point_index)) {
            continue;
        }
        Point3 transformed{
            g_selection.drag_snapshot.points[point_index].x,
            g_selection.drag_snapshot.points[point_index].y,
            g_selection.drag_snapshot.points[point_index].z};
        if (world_space) {
            Point3 world = ApplyAffine3D(
                cage_to_world,
                transformed);
            if (g_selection.transform_tool_drag ==
                kTransformToolRotate) {
                const Point3 axis =
                    AxisUnit(g_selection.rotate_axis_drag);
                world = RotatePoint(
                    world,
                    world_centroid.x,
                    world_centroid.y,
                    world_centroid.z,
                    axis.x * rotation_angle,
                    axis.y * rotation_angle,
                    axis.z * rotation_angle);
            } else if (
                g_selection.transform_tool_drag ==
                kTransformToolScale) {
                world.x =
                    world_centroid.x +
                    (world.x - world_centroid.x) * scale_x;
                world.y =
                    world_centroid.y +
                    (world.y - world_centroid.y) * scale_y;
                world.z =
                    world_centroid.z +
                    (world.z - world_centroid.z) * scale_z;
            } else {
                world.x += world_apply.x;
                world.y += world_apply.y;
                world.z += world_apply.z;
            }
            transformed = ApplyAffine3D(
                world_to_cage,
                world);
        } else if (
            g_selection.transform_tool_drag ==
            kTransformToolRotate) {
            const Point3 axis = AxisUnit(g_selection.rotate_axis_drag);
            transformed = RotatePoint(
                transformed,
                g_selection.centroid_down.x,
                g_selection.centroid_down.y,
                g_selection.centroid_down.z,
                axis.x * rotation_angle,
                axis.y * rotation_angle,
                axis.z * rotation_angle);
        } else if (
            g_selection.transform_tool_drag == kTransformToolScale) {
            transformed.x =
                g_selection.centroid_down.x +
                (transformed.x - g_selection.centroid_down.x) * scale_x;
            transformed.y =
                g_selection.centroid_down.y +
                (transformed.y - g_selection.centroid_down.y) * scale_y;
            transformed.z =
                g_selection.centroid_down.z +
                (transformed.z - g_selection.centroid_down.z) * scale_z;
        } else {
            transformed.x += apply_x;
            transformed.y += apply_y;
            transformed.z += apply_z;
        }
        lattice->points[point_index].x =
            static_cast<float>(transformed.x);
        lattice->points[point_index].y =
            static_cast<float>(transformed.y);
        lattice->points[point_index].z =
            static_cast<float>(transformed.z);
    }
    PF_UNLOCK_HANDLE(handle);
    params[SurfaceLatticeParam(g_selection.primary.surface)]
        ->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
    g_selection.last_mouse = mouse;
    event_extra->evt_out_flags = static_cast<PF_EventOutFlags>(
        PF_EO_HANDLED_EVENT |
        PF_EO_ALWAYS_UPDATE |
        PF_EO_UPDATE_NOW);
    EndCompDragIfFinished(event_extra);
    return PF_Err_NONE;
}
