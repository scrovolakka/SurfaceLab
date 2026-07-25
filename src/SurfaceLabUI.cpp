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
// drag memory model. A1 keeps selection on a single surface so multi-drag
// can share one screen→local Jacobian without cross-surface ambiguity.
struct LatticePointRef {
    std::uint32_t surface{};
    std::uint16_t row{};
    std::uint16_t column{};
};

struct GizmoSelectionState {
    std::vector<LatticePointRef> points{};
    bool dragging{};
    LatticePointRef primary{};
    Point2 last_mouse{};
};

GizmoSelectionState g_selection;

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
    g_selection.primary = {};
}

void SetSelection(const LatticePointRef& point) {
    g_selection.points.clear();
    g_selection.points.push_back(point);
    g_selection.primary = point;
}

void ToggleSelection(const LatticePointRef& point) {
    if (!g_selection.points.empty() &&
        g_selection.points.front().surface != point.surface) {
        // Cross-surface multi-select lands in a later slice.
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

    PF_ADD_TOPIC("Scene", kDiskSceneStart);
    PF_Err error = AddPoint3D(
        in_data,
        def,
        "Position",
        50.0,
        50.0,
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
            50.0,
            50.0,
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
        constexpr double kHitRadiusSquared = 100.0;
        double closest = kHitRadiusSquared;
        LatticePointRef hit{};
        bool found = false;
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
                        hit = {surface_index, row, column};
                        found = true;
                    }
                }
            }
        }

        const bool shift =
            (event_extra->u.do_click.modifiers & PF_Mod_SHIFT_KEY) != 0;
        g_selection.dragging = false;
        if (!found) {
            if (!shift && !g_selection.points.empty()) {
                ClearSelection();
                event_extra->evt_out_flags =
                    static_cast<PF_EventOutFlags>(
                        PF_EO_HANDLED_EVENT | PF_EO_ALWAYS_UPDATE);
            }
            return PF_Err_NONE;
        }

        if (shift) {
            ToggleSelection(hit);
        } else if (!SelectionContains(hit)) {
            SetSelection(hit);
        } else {
            g_selection.primary = hit;
        }

        if (!g_selection.points.empty()) {
            g_selection.dragging = true;
            g_selection.last_mouse = mouse;
            event_extra->evt_out_flags =
                static_cast<PF_EventOutFlags>(
                    PF_EO_HANDLED_EVENT | PF_EO_ALWAYS_UPDATE);
        } else {
            event_extra->evt_out_flags = PF_EO_HANDLED_EVENT;
        }
        return PF_Err_NONE;
    }

    if (!g_selection.dragging ||
        g_selection.points.empty() ||
        g_selection.primary.surface >= scene.surface_count) {
        return PF_Err_NONE;
    }
    SurfaceData surface = scene.surfaces[g_selection.primary.surface];
    const std::size_t primary_index = LatticePointIndex(
        surface.lattice.divisions_x,
        g_selection.primary.row,
        g_selection.primary.column);
    if (null_overrides.IsControlled(
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
    const double screen_x = mouse.x - g_selection.last_mouse.x;
    const double screen_y = mouse.y - g_selection.last_mouse.y;
    const bool depth_drag =
        (event_extra->u.do_click.modifiers &
         PF_Mod_OPT_ALT_KEY) != 0;
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
            delta_z = length_squared > 1.0e-8
                          ? (screen_x * jzx + screen_y * jzy) /
                                length_squared
                          : -screen_y;
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
    const float apply_x = static_cast<float>(
        std::clamp(delta_x, -2000.0, 2000.0));
    const float apply_y = static_cast<float>(
        std::clamp(delta_y, -2000.0, 2000.0));
    const float apply_z = static_cast<float>(
        std::clamp(delta_z, -2000.0, 2000.0));

    PF_Handle handle =
        params[SurfaceLatticeParam(g_selection.primary.surface)]
            ->u.arb_d.value;
    auto* lattice =
        static_cast<LatticeData*>(PF_LOCK_HANDLE(handle));
    if (!lattice) {
        return PF_Err_NONE;
    }
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
        lattice->points[point_index].x += apply_x;
        lattice->points[point_index].y += apply_y;
        lattice->points[point_index].z += apply_z;
    }
    PF_UNLOCK_HANDLE(handle);
    params[SurfaceLatticeParam(g_selection.primary.surface)]
        ->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
    g_selection.last_mouse = mouse;
    event_extra->evt_out_flags = static_cast<PF_EventOutFlags>(
        PF_EO_HANDLED_EVENT |
        PF_EO_ALWAYS_UPDATE |
        PF_EO_UPDATE_NOW);
    return PF_Err_NONE;
}
