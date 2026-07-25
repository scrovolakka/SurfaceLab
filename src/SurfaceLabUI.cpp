#include "SurfaceLabUI.h"

#include "AEGP_SuiteHandler.h"
#include "AEFX_SuiteHelper.h"
#include "SurfaceLabRender.h"
#include <adobesdk/DrawbotSuite.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace {

PF_Err AddPoint3D(
    PF_InData* in_data,
    PF_ParamDef& def,
    const char* name,
    double x,
    double y,
    double z,
    A_long disk_id,
    PF_ParamUIFlags ui_flags = PF_PUI_NONE) {
    AEFX_CLR_STRUCT(def);
    def.param_type = PF_Param_POINT_3D;
    PF_STRNNCPY(def.PF_DEF_NAME, name, sizeof(def.PF_DEF_NAME));
    def.u.point3d_d.x_value = def.u.point3d_d.x_dephault = x;
    def.u.point3d_d.y_value = def.u.point3d_d.y_dephault = y;
    def.u.point3d_d.z_value = def.u.point3d_d.z_dephault = z;
    def.uu.id = disk_id;
    def.ui_flags = ui_flags;
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

struct GizmoSelectionState {
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
    Point3 selection_centroid{};
    Point3 centroid_down{};
    // Absolute drag base: every DRAG frame restores this then applies total
    // delta from mouse_down so bad Jacobians cannot compound.
    LatticeData drag_snapshot{};
};

GizmoSelectionState g_selection;

constexpr double kTranslateAxisPixels = 56.0;
constexpr double kTranslateAxisHitPixels = 10.0;
// Reject axes that project to nearly a point (depth-parallel / edge-on).
constexpr double kMinAxisPixelsPerUnit = 0.25;
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
    g_selection.points.clear();
    g_selection.dragging = false;
    g_selection.drag_moved = false;
    g_selection.has_snapshot = false;
    g_selection.drag_origin_seeded = false;
    g_selection.marquee_active = false;
    g_selection.marquee_additive = false;
    g_selection.primary = {};
    g_selection.axis_drag = TranslateAxis::None;
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
    PF_Handle handle =
        params[SurfaceLatticeParam(surface)]->u.arb_d.value;
    if (!handle) {
        g_selection.has_snapshot = false;
        return false;
    }
    const auto* lattice =
        static_cast<const LatticeData*>(PF_LOCK_HANDLE(handle));
    if (!lattice || !IsValidLattice(*lattice)) {
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

// Project a cage-local lattice coordinate through the same transform chain as
// EvaluateTransformedPoint, without routing through UV evaluation.
bool ProjectCageLocalPointToFrame(
    PF_InData* in_data,
    PF_EventExtra* event_extra,
    const SurfaceData& surface,
    const CameraState& camera,
    Point3 local,
    Point2& frame_point) {
    const SurfaceEvaluationState state = BuildSurfaceEvaluationState(
        surface,
        camera,
        1.0,
        1.0,
        1.0);
    Point3 world = ScaleSurfaceCagePoint(local, state.coordinate_transform);
    world = RotateSurfaceWorldPoint(world, state.coordinate_transform);
    if (state.root_transform_enabled) {
        world = ApplyAffine3D(state.root_pre_scene_transform, world);
    }
    const Vertex projected = ProjectVertex(
        world,
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
    Point2& origin,
    Point2& tip,
    double& pixels_per_unit) {
    if (!ProjectSelectionCentroidToFrame(
            in_data,
            event_extra,
            scene,
            camera,
            centroid,
            origin)) {
        return false;
    }
    const Point3 unit = AxisUnit(axis);
    // Probe far enough for a stable screen direction on shallow projections.
    constexpr double kProbe = 32.0;
    Point2 probed;
    if (!ProjectSelectionCentroidToFrame(
            in_data,
            event_extra,
            scene,
            camera,
            {centroid.x + unit.x * kProbe,
             centroid.y + unit.y * kProbe,
             centroid.z + unit.z * kProbe},
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

void SetSelectionPoints(const std::vector<LatticePointRef>& points) {
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

void ToggleSelection(const LatticePointRef& point) {
    if (!g_selection.points.empty() &&
        g_selection.points.front().surface != point.surface) {
        SetSelection(point);
        return;
    }
    const auto existing = std::find_if(
        g_selection.points.begin(),
        g_selection.points.end(),
        [&](const LatticePointRef& candidate) {
            return SameLatticePoint(candidate, point);
        });
    if (existing != g_selection.points.end()) {
        g_selection.points.erase(existing);
        if (SameLatticePoint(g_selection.primary, point)) {
            g_selection.primary =
                g_selection.points.empty() ? LatticePointRef{}
                                           : g_selection.points.front();
        }
        return;
    }
    g_selection.points.push_back(point);
    g_selection.primary = point;
}

void MergePointsIntoSelection(const std::vector<LatticePointRef>& points) {
    for (const LatticePointRef& point : points) {
        AddPointToSelection(point);
    }
}

std::vector<LatticePointRef> CollectFreeLinePoints(
    const SceneData& scene,
    const NullPointOverrideState& null_overrides,
    const LatticeLineRef& line) {
    std::vector<LatticePointRef> points;
    if (line.surface >= scene.surface_count) {
        return points;
    }
    const SurfaceData& surface = scene.surfaces[line.surface];
    if (surface.enabled == 0 || !IsValidLattice(surface.lattice)) {
        return points;
    }
    if (line.kind == LatticeLineKind::Row) {
        if (line.index > surface.lattice.divisions_y) {
            return points;
        }
        for (std::uint16_t column = 0;
             column <= surface.lattice.divisions_x;
             ++column) {
            const std::size_t point_index = LatticePointIndex(
                surface.lattice.divisions_x,
                line.index,
                column);
            if (null_overrides.IsControlled(line.surface, point_index)) {
                continue;
            }
            points.push_back({line.surface, line.index, column});
        }
        return points;
    }
    if (line.index > surface.lattice.divisions_x) {
        return points;
    }
    for (std::uint16_t row = 0;
         row <= surface.lattice.divisions_y;
         ++row) {
        const std::size_t point_index = LatticePointIndex(
            surface.lattice.divisions_x,
            row,
            line.index);
        if (null_overrides.IsControlled(line.surface, point_index)) {
            continue;
        }
        points.push_back({line.surface, row, line.index});
    }
    return points;
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

PF_Err PublishRigBridge(
    PF_InData* in_data,
    PF_ParamDef* params[]) {
    if (!in_data || !params) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }
    const std::uint32_t surface_index =
        static_cast<std::uint32_t>(std::clamp<A_long>(
            params[kParamRigSurface]->u.sd.value,
            1,
            kSurfaceCount) - 1);
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
    StoredPoint3 minimum = published.points[0];
    StoredPoint3 maximum = minimum;
    for (std::size_t index = 1;
         index < published.point_count;
         ++index) {
        const StoredPoint3& point = published.points[index];
        minimum.x = std::min(minimum.x, point.x);
        minimum.y = std::min(minimum.y, point.y);
        minimum.z = std::min(minimum.z, point.z);
        maximum.x = std::max(maximum.x, point.x);
        maximum.y = std::max(maximum.y, point.y);
        maximum.z = std::max(maximum.z, point.z);
    }
    if (maximum.x - minimum.x <= 1.0e-4F &&
        maximum.y - minimum.y <= 1.0e-4F &&
        maximum.z - minimum.z <= 1.0e-4F) {
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
            params[kParamRigRow]->u.sd.value,
            0,
            published.divisions_y));
    params[kParamRigRow]->u.sd.value = row;
    const std::array<PF_ParamIndex, 4> id_parameters = {
        kParamRigSurfaceId0,
        kParamRigSurfaceId1,
        kParamRigSurfaceId2,
                kParamRigSurfaceId3};
    for (std::size_t chunk = 0; chunk < id_parameters.size(); ++chunk) {
        const unsigned shift =
            static_cast<unsigned>((3U - chunk) * 16U);
        params[id_parameters[chunk]]->u.sd.value =
            static_cast<A_long>(
                (published.surface_id >> shift) & 0xffffU);
        MarkChanged(params[id_parameters[chunk]]);
    }
    params[kParamRigDivisionsX]->u.sd.value =
        published.divisions_x;
    params[kParamRigDivisionsY]->u.sd.value =
        published.divisions_y;
    MarkChanged(params[kParamRigDivisionsX]);
    MarkChanged(params[kParamRigDivisionsY]);
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
        const std::array<double, 3> coordinates = {
            point.x,
            point.y,
            point.z};
        for (std::uint32_t axis = 0; axis < coordinates.size(); ++axis) {
            PF_ParamDef* output =
                params[RigPointCoordinateParam(column, axis)];
            output->u.fs_d.value = coordinates[axis];
            MarkChanged(output);
        }
    }
    return PF_Err_NONE;
}

}  // namespace

PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data) {
    PF_ParamDef def;
    AEFX_CLR_STRUCT(def);

    const double default_center_x =
        in_data && in_data->width > 0 ? in_data->width * 0.5 : 960.0;
    const double default_center_y =
        in_data && in_data->height > 0 ? in_data->height * 0.5 : 540.0;

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
    def.flags = PF_ParamFlag_START_COLLAPSED;
    PF_ADD_TOPIC("Null Rig Bridge", kDiskRigBridgeStart);
    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_SUPERVISE;
    PF_ADD_SLIDER(
        "Rig Surface",
        1,
        kSurfaceCount,
        1,
        kSurfaceCount,
        1,
        kDiskRigSurface);
    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_SUPERVISE;
    PF_ADD_SLIDER(
        "Rig Row",
        0,
        kMaximumLatticeDivisions,
        0,
        kMaximumLatticeDivisions,
        0,
        kDiskRigRow);
    const char* id_names[] = {
        "Rig Surface ID 0",
        "Rig Surface ID 1",
        "Rig Surface ID 2",
        "Rig Surface ID 3"};
    for (std::size_t chunk = 0; chunk < 4; ++chunk) {
        AEFX_CLR_STRUCT(def);
        def.ui_flags = PF_PUI_DISABLED;
        PF_ADD_SLIDER(
            id_names[chunk],
            0,
            65535,
            0,
            65535,
            0,
            kDiskRigSurfaceId0 + static_cast<A_long>(chunk));
    }
    AEFX_CLR_STRUCT(def);
    def.ui_flags = PF_PUI_DISABLED;
    PF_ADD_SLIDER(
        "Rig Divisions X",
        kMinimumLatticeDivisions,
        kMaximumLatticeDivisions,
        kMinimumLatticeDivisions,
        kMaximumLatticeDivisions,
        3,
        kDiskRigDivisionsX);
    AEFX_CLR_STRUCT(def);
    def.ui_flags = PF_PUI_DISABLED;
    PF_ADD_SLIDER(
        "Rig Divisions Y",
        kMinimumLatticeDivisions,
        kMaximumLatticeDivisions,
        kMinimumLatticeDivisions,
        kMaximumLatticeDivisions,
        3,
        kDiskRigDivisionsY);
    for (std::uint32_t column = 0;
         column < kMaximumLatticeAxisPoints;
         ++column) {
        const char* axis_names[] = {"X", "Y", "Z"};
        for (std::uint32_t axis = 0; axis < 3; ++axis) {
            char coordinate_name[32]{};
            std::snprintf(
                coordinate_name,
                sizeof(coordinate_name),
                "Rig Point %u %s",
                column,
                axis_names[axis]);
            AEFX_CLR_STRUCT(def);
            def.ui_flags = PF_PUI_DISABLED;
            PF_ADD_FLOAT_SLIDERX(
                coordinate_name,
                -1000000.0,
                1000000.0,
                -1000000.0,
                1000000.0,
                0.0,
                PF_Precision_THOUSANDTHS,
                PF_ValueDisplayFlag_NONE,
                PF_ParamFlag_NONE,
                kDiskRigPointsStart +
                    static_cast<A_long>(column * 3U + axis));
        }
    }
    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(kDiskRigBridgeEnd);

    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("About", kDiskAboutStart);
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
    PF_END_TOPIC(kDiskAboutEnd);

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
        kParamAboutEnd + 1 == kParamCount,
        "About block must terminate immediately before kParamCount");
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
    if (extra->param_index == kParamAboutVersion) {
        if (out_data) {
            std::snprintf(
                out_data->return_msg,
                sizeof(out_data->return_msg),
                "SurfaceLab %s\n3D interpolating control-point lattice\n"
                "Effect UI and Comp gizmo build identity.",
                kSurfaceLabVersionString);
            out_data->out_flags |= PF_OutFlag_DISPLAY_ERROR_MESSAGE;
        }
        return PF_Err_NONE;
    }
    if (extra->param_index == kParamRigSurface ||
        extra->param_index == kParamRigRow) {
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
    return PF_Err_NONE;
}

PF_Err UpdateParameterUi(
    PF_InData* in_data,
    PF_OutData*,
    PF_ParamDef* params[]) {
    for (std::uint32_t surface = 0; surface < kSurfaceCount; ++surface) {
        const PF_Handle handle =
            params[SurfaceLatticeParam(surface)]->u.arb_d.value;
        if (!handle) {
            continue;
        }
        const auto* lattice =
            static_cast<const LatticeData*>(PF_LOCK_HANDLE(handle));
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
        event_extra->e_type != PF_Event_DRAG) {
        return PF_Err_NONE;
    }

    const A_long width = params[kParamInput]->u.ld.width > 0
                             ? params[kParamInput]->u.ld.width
                             : in_data->width;
    const A_long height = params[kParamInput]->u.ld.height > 0
                              ? params[kParamInput]->u.ld.height
                              : in_data->height;
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

        const DRAWBOT_ColorRGBA grid_color{
            0.12F, 0.78F, 1.0F, 0.82F};
        const DRAWBOT_ColorRGBA point_color{
            0.93F, 0.98F, 1.0F, 1.0F};
        const DRAWBOT_ColorRGBA selected_point_color{
            1.0F, 0.86F, 0.20F, 1.0F};
        const DRAWBOT_ColorRGBA controlled_point_color{
            1.0F, 0.63F, 0.12F, 1.0F};
        for (std::uint32_t surface_index = 0;
             surface_index < scene.surface_count;
             ++surface_index) {
            const SurfaceData& surface =
                scene.surfaces[surface_index];
            if (surface.enabled == 0 ||
                !IsValidLattice(surface.lattice)) {
                continue;
            }
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
                1.0F);
            drawbot.surface_suiteP->StrokePath(
                drawing_surface,
                pen,
                grid);

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
                    const float half = selected ? 5.0F : 3.5F;
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
            const DRAWBOT_ColorRGBA axis_colors[3] = {
                {0.95F, 0.28F, 0.28F, 0.95F},  // X
                {0.30F, 0.85F, 0.35F, 0.95F},  // Y
                {0.30F, 0.55F, 1.0F, 0.95F},   // Z
            };
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
                const float stroke =
                    g_selection.axis_drag == axes[axis_index] ? 3.0F
                                                              : 2.0F;
                DRAWBOT_PenP axis_pen(
                    drawbot.supplier_suiteP,
                    supplier,
                    &axis_colors[axis_index],
                    stroke);
                drawbot.surface_suiteP->StrokePath(
                    drawing_surface,
                    axis_pen,
                    axis_path);
                DRAWBOT_RectF32 tip_rect{
                    static_cast<float>(tip.x - 3.0),
                    static_cast<float>(tip.y - 3.0),
                    6.0F,
                    6.0F};
                suites.SurfaceSuiteCurrent()->PaintRect(
                    drawing_surface,
                    &axis_colors[axis_index],
                    &tip_rect);
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

        // Cmd/Ctrl-drag starts a same-surface marquee (Foldspace-style).
        if (command) {
            g_selection.marquee_active = true;
            g_selection.marquee_additive = shift;
            g_selection.marquee_start = mouse;
            g_selection.marquee_end = mouse;
            if (!shift) {
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
            const TranslateAxis axis = HitTestTranslateAxes(
                in_data,
                event_extra,
                scene,
                camera,
                centroid,
                mouse);
            if (axis != TranslateAxis::None) {
                g_selection.axis_drag = axis;
                g_selection.dragging = true;
                if (CaptureDragSnapshot(
                        in_data,
                        params,
                        g_selection.points.front().surface,
                        mouse,
                        centroid)) {
                    BeginCompDrag(event_extra);
                } else {
                    g_selection.dragging = false;
                    g_selection.axis_drag = TranslateAxis::None;
                }
                event_extra->evt_out_flags =
                    static_cast<PF_EventOutFlags>(
                        PF_EO_HANDLED_EVENT | PF_EO_ALWAYS_UPDATE);
                return PF_Err_NONE;
            }
        }

        constexpr double kPointHitRadiusSquared = 100.0;
        constexpr double kLineHitRadiusSquared = 64.0;
        double closest = kPointHitRadiusSquared;
        LatticePointRef point_hit{};
        bool found_point = false;
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
                    const std::size_t point_index = LatticePointIndex(
                        surface.lattice.divisions_x,
                        row,
                        column);
                    if (null_overrides.IsControlled(
                            surface_index,
                            point_index)) {
                        continue;
                    }
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
                    const double dx = mouse.x - point.x;
                    const double dy = mouse.y - point.y;
                    const double distance = dx * dx + dy * dy;
                    if (distance <= closest) {
                        closest = distance;
                        point_hit = {surface_index, row, column};
                        found_point = true;
                    }
                }
            }
        }

        if (found_point) {
            if (shift) {
                ToggleSelection(point_hit);
            } else if (!SelectionContains(point_hit)) {
                SetSelection(point_hit);
            } else {
                g_selection.primary = point_hit;
            }
            if (!g_selection.points.empty()) {
                Point3 point_centroid{};
                ComputeSelectionCentroid(scene, point_centroid);
                g_selection.dragging = CaptureDragSnapshot(
                    in_data,
                    params,
                    g_selection.primary.surface,
                    mouse,
                    point_centroid);
                if (g_selection.dragging) {
                    BeginCompDrag(event_extra);
                }
                event_extra->evt_out_flags =
                    static_cast<PF_EventOutFlags>(
                        PF_EO_HANDLED_EVENT | PF_EO_ALWAYS_UPDATE);
            } else {
                event_extra->evt_out_flags = PF_EO_HANDLED_EVENT;
            }
            return PF_Err_NONE;
        }

        LatticeLineRef line_hit{};
        if (HitTestLatticeLine(
                in_data,
                event_extra,
                scene,
                camera,
                mouse,
                kLineHitRadiusSquared,
                line_hit)) {
            const std::vector<LatticePointRef> line_points =
                CollectFreeLinePoints(
                    scene,
                    null_overrides,
                    line_hit);
            if (!line_points.empty()) {
                if (shift) {
                    MergePointsIntoSelection(line_points);
                } else {
                    SetSelectionPoints(line_points);
                }
                Point3 line_centroid{};
                ComputeSelectionCentroid(scene, line_centroid);
                g_selection.dragging = CaptureDragSnapshot(
                    in_data,
                    params,
                    g_selection.primary.surface,
                    mouse,
                    line_centroid);
                if (g_selection.dragging) {
                    BeginCompDrag(event_extra);
                }
                event_extra->evt_out_flags =
                    static_cast<PF_EventOutFlags>(
                        PF_EO_HANDLED_EVENT | PF_EO_ALWAYS_UPDATE);
                return PF_Err_NONE;
            }
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

    // 1) Seed origin on the first DRAG sample. DO_CLICK and the first DRAG
    // often disagree by hundreds of pixels in AE; treating that as motion
    // wrote multi-thousand cage deltas and looked like a total collapse.
    if (!g_selection.drag_origin_seeded) {
        g_selection.mouse_down = mouse;
        g_selection.last_mouse = mouse;
        g_selection.drag_origin_seeded = true;
        event_extra->evt_out_flags = static_cast<PF_EventOutFlags>(
            PF_EO_HANDLED_EVENT | PF_EO_ALWAYS_UPDATE);
        EndCompDragIfFinished(event_extra);
        return PF_Err_NONE;
    }

    // 2) Reject mid-drag teleports; keep last good sample.
    const double step_x = mouse.x - g_selection.last_mouse.x;
    const double step_y = mouse.y - g_selection.last_mouse.y;
    const double step = std::sqrt(step_x * step_x + step_y * step_y);
    if (step > kMaxDragStepPixels) {
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

    // Evaluate Jacobian against the pre-drag snapshot so the mapping is stable.
    SurfaceData surface = scene.surfaces[g_selection.primary.surface];
    surface.lattice = g_selection.drag_snapshot;
    if (!IsValidLattice(surface.lattice)) {
        EndCompDragIfFinished(event_extra);
        return PF_Err_NONE;
    }

    if (g_selection.axis_drag != TranslateAxis::None) {
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
        apply_x = static_cast<float>(
            std::clamp(unit.x * axis_delta, -kMaxCageDelta, kMaxCageDelta));
        apply_y = static_cast<float>(
            std::clamp(unit.y * axis_delta, -kMaxCageDelta, kMaxCageDelta));
        apply_z = static_cast<float>(
            std::clamp(unit.z * axis_delta, -kMaxCageDelta, kMaxCageDelta));
        g_selection.selection_centroid = {
            g_selection.centroid_down.x + apply_x,
            g_selection.centroid_down.y + apply_y,
            g_selection.centroid_down.z + apply_z};
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
        // Reject shallow screen Jacobians (near-zero projected cage axes).
        const double jx_len = std::sqrt(jxx * jxx + jyx * jyx);
        const double jy_len = std::sqrt(jxy * jxy + jyy * jyy);
        const bool depth_drag =
            (event_extra->u.do_click.modifiers &
             PF_Mod_OPT_ALT_KEY) != 0;
        if (!depth_drag &&
            (jx_len < kMinAxisPixelsPerUnit ||
             jy_len < kMinAxisPixelsPerUnit)) {
            g_selection.last_mouse = mouse;
            return PF_Err_NONE;
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
        const double determinant = jxx * jyy - jxy * jyx;
        if (!depth_drag && std::abs(determinant) <= 1.0e-8) {
            return PF_Err_NONE;
        }
        const double delta_x = depth_drag
                                   ? 0.0
                                   : (screen_x * jyy -
                                      screen_y * jxy) / determinant;
        const double delta_y = depth_drag
                                   ? 0.0
                                   : (jxx * screen_y -
                                      jyx * screen_x) / determinant;
        apply_x = static_cast<float>(
            std::clamp(delta_x, -kMaxCageDelta, kMaxCageDelta));
        apply_y = static_cast<float>(
            std::clamp(delta_y, -kMaxCageDelta, kMaxCageDelta));
        apply_z = static_cast<float>(
            std::clamp(delta_z, -kMaxCageDelta, kMaxCageDelta));
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
        lattice->points[point_index].x =
            g_selection.drag_snapshot.points[point_index].x + apply_x;
        lattice->points[point_index].y =
            g_selection.drag_snapshot.points[point_index].y + apply_y;
        lattice->points[point_index].z =
            g_selection.drag_snapshot.points[point_index].z + apply_z;
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
