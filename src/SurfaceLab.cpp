#include "SurfaceLab.h"

#include "AEGP_SuiteHandler.h"
#include "AEFX_SuiteHelper.h"
#include "SurfaceLabInternal.h"
#include "SurfaceLabMetal.h"
#include "SurfaceLabRender.h"
#include "SurfaceLabUI.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace {

std::uint32_t SurfaceFromRefcon(void* refcon) {
    const std::uintptr_t encoded =
        reinterpret_cast<std::uintptr_t>(refcon);
    return encoded > 0 && encoded <= kSurfaceCount
               ? static_cast<std::uint32_t>(encoded - 1)
               : 0;
}

PF_Err AllocateLattice(
    PF_InData* in_data,
    PF_ArbitraryH* destination) {
    if (!destination) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }
    PF_Handle handle = PF_NEW_HANDLE(sizeof(LatticeData));
    if (!handle) {
        return PF_Err_OUT_OF_MEMORY;
    }
    *destination = handle;
    return PF_Err_NONE;
}

PF_Err CopyLatticeHandle(
    PF_InData* in_data,
    PF_ArbitraryH source,
    PF_ArbitraryH* destination) {
    if (!source) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }
    PF_Err error = AllocateLattice(in_data, destination);
    if (error != PF_Err_NONE) {
        return error;
    }
    const auto* source_lattice =
        static_cast<const LatticeData*>(PF_LOCK_HANDLE(source));
    auto* destination_lattice =
        static_cast<LatticeData*>(PF_LOCK_HANDLE(*destination));
    if (!source_lattice || !destination_lattice) {
        if (source_lattice) {
            PF_UNLOCK_HANDLE(source);
        }
        if (destination_lattice) {
            PF_UNLOCK_HANDLE(*destination);
        }
        PF_DISPOSE_HANDLE(*destination);
        *destination = nullptr;
        return PF_Err_OUT_OF_MEMORY;
    }
    *destination_lattice = *source_lattice;
    PF_UNLOCK_HANDLE(*destination);
    PF_UNLOCK_HANDLE(source);
    return PF_Err_NONE;
}

PF_Err About(PF_InData* in_data, PF_OutData* out_data) {
    std::snprintf(
        out_data->return_msg,
        sizeof(out_data->return_msg),
        "SurfaceLab %s\r3D interpolating control-point lattice\r"
        "Metal device: %s",
        kSurfaceLabVersionString,
        IsMetalDeviceReady(in_data) ? "ready" : "CPU fallback");
    return PF_Err_NONE;
}

PF_Err GlobalSetup(PF_InData* in_data, PF_OutData* out_data) {
    out_data->my_version = PF_VERSION(
        kSurfaceLabVersionMajor,
        kSurfaceLabVersionMinor,
        kSurfaceLabVersionPatch,
        PF_Stage_DEVELOP,
        1);
    out_data->out_flags =
        PF_OutFlag_DEEP_COLOR_AWARE |
        PF_OutFlag_CUSTOM_UI |
        PF_OutFlag_I_USE_SHUTTER_ANGLE |
        PF_OutFlag_SEND_UPDATE_PARAMS_UI |
        PF_OutFlag_PIX_INDEPENDENT |
        PF_OutFlag_WIDE_TIME_INPUT |
        PF_OutFlag_USE_OUTPUT_EXTENT;
    out_data->out_flags2 =
        PF_OutFlag2_I_USE_3D_CAMERA |
        PF_OutFlag2_I_USE_3D_LIGHTS |
        PF_OutFlag2_FLOAT_COLOR_AWARE |
        PF_OutFlag2_SUPPORTS_SMART_RENDER |
        PF_OutFlag2_SUPPORTS_GPU_RENDER_F32 |
        PF_OutFlag2_I_MIX_GUID_DEPENDENCIES |
        PF_OutFlag2_SUPPORTS_THREADED_RENDERING;

    AEFX_SuiteScoper<AEGP_UtilitySuite6> utility_suite(
        in_data,
        kAEGPUtilitySuite,
        kAEGPUtilitySuiteVersion6,
        out_data);
    AEGP_PluginID plugin_id{};
    const A_Err error = utility_suite->AEGP_RegisterWithAEGP(
        nullptr,
        "SurfaceLab v1",
        &plugin_id);
    if (error != A_Err_NONE) {
        return PF_Err_INTERNAL_STRUCT_DAMAGED;
    }
    auto data = std::make_unique<GlobalData>();
    data->plugin_id = plugin_id;
    out_data->global_data = reinterpret_cast<PF_Handle>(data.release());
    return PF_Err_NONE;
}

PF_Err GlobalSetdown(PF_InData* in_data) {
    delete reinterpret_cast<GlobalData*>(in_data->global_data);
    return PF_Err_NONE;
}

}  // namespace

void* LatticeRefcon(std::uint32_t surface) {
    return reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(surface + 1));
}

PF_Err CreateLatticeHandle(
    PF_InData* in_data,
    PF_ArbitraryH* destination,
    double width,
    double height,
    std::uint32_t surface) {
    (void)width;
    (void)height;
    PF_Err error = AllocateLattice(in_data, destination);
    if (error != PF_Err_NONE) {
        return error;
    }
    auto* lattice =
        static_cast<LatticeData*>(PF_LOCK_HANDLE(*destination));
    if (!lattice) {
        PF_DISPOSE_HANDLE(*destination);
        *destination = nullptr;
        return PF_Err_OUT_OF_MEMORY;
    }
    const std::uint64_t id =
        (static_cast<std::uint64_t>(
             static_cast<std::uint32_t>(in_data ? in_data->current_time : 0))
         << 32U) |
        (static_cast<std::uint64_t>(surface) + 1U);
    // ParamsSetup does not expose the input layer dimensions reliably. Mark
    // every newly-created lattice for one-time initialization once AE supplies
    // a real input frame.
    InitializeLattice(
        *lattice,
        3,
        3,
        0.0,
        0.0,
        id);
    lattice->reserved |= kLatticeFlagNeedsInputSize;
    PF_UNLOCK_HANDLE(*destination);
    return PF_Err_NONE;
}

PF_Err HandleArbitrary(PF_InData* in_data, PF_ArbParamsExtra* extra) {
    if (!in_data || !extra) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }
    switch (extra->which_function) {
        case PF_Arbitrary_NEW_FUNC:
            return CreateLatticeHandle(
                in_data,
                extra->u.new_func_params.arbPH,
                in_data->width,
                in_data->height,
                SurfaceFromRefcon(
                    extra->u.new_func_params.refconPV));
        case PF_Arbitrary_DISPOSE_FUNC:
            if (extra->u.dispose_func_params.arbH) {
                PF_DISPOSE_HANDLE(extra->u.dispose_func_params.arbH);
            }
            return PF_Err_NONE;
        case PF_Arbitrary_COPY_FUNC:
            return CopyLatticeHandle(
                in_data,
                extra->u.copy_func_params.src_arbH,
                extra->u.copy_func_params.dst_arbPH);
        case PF_Arbitrary_FLAT_SIZE_FUNC: {
            const auto* lattice = static_cast<const LatticeData*>(
                PF_LOCK_HANDLE(extra->u.flat_size_func_params.arbH));
            if (!lattice) {
                return PF_Err_OUT_OF_MEMORY;
            }
            const std::vector<std::uint8_t> bytes =
                FlattenLattice(*lattice);
            PF_UNLOCK_HANDLE(extra->u.flat_size_func_params.arbH);
            if (bytes.empty()) {
                return PF_Err_INTERNAL_STRUCT_DAMAGED;
            }
            *extra->u.flat_size_func_params.flat_data_sizePLu =
                static_cast<A_u_long>(bytes.size());
            return PF_Err_NONE;
        }
        case PF_Arbitrary_FLATTEN_FUNC: {
            const auto* lattice = static_cast<const LatticeData*>(
                PF_LOCK_HANDLE(extra->u.flatten_func_params.arbH));
            if (!lattice) {
                return PF_Err_OUT_OF_MEMORY;
            }
            const std::vector<std::uint8_t> bytes =
                FlattenLattice(*lattice);
            PF_UNLOCK_HANDLE(extra->u.flatten_func_params.arbH);
            if (bytes.empty() ||
                bytes.size() > extra->u.flatten_func_params.buf_sizeLu) {
                return PF_Err_BAD_CALLBACK_PARAM;
            }
            std::memcpy(
                extra->u.flatten_func_params.flat_dataPV,
                bytes.data(),
                bytes.size());
            return PF_Err_NONE;
        }
        case PF_Arbitrary_UNFLATTEN_FUNC: {
            LatticeData lattice{};
            if (!UnflattenLattice(
                    extra->u.unflatten_func_params.flat_dataPV,
                    extra->u.unflatten_func_params.buf_sizeLu,
                    lattice)) {
                return PF_Err_INTERNAL_STRUCT_DAMAGED;
            }
            PF_Err error =
                AllocateLattice(
                    in_data,
                    extra->u.unflatten_func_params.arbPH);
            if (error != PF_Err_NONE) {
                return error;
            }
            auto* destination = static_cast<LatticeData*>(PF_LOCK_HANDLE(
                *extra->u.unflatten_func_params.arbPH));
            if (!destination) {
                PF_DISPOSE_HANDLE(
                    *extra->u.unflatten_func_params.arbPH);
                *extra->u.unflatten_func_params.arbPH = nullptr;
                return PF_Err_OUT_OF_MEMORY;
            }
            *destination = lattice;
            PF_UNLOCK_HANDLE(*extra->u.unflatten_func_params.arbPH);
            return PF_Err_NONE;
        }
        case PF_Arbitrary_INTERP_FUNC: {
            const auto* left = static_cast<const LatticeData*>(
                PF_LOCK_HANDLE(extra->u.interp_func_params.left_arbH));
            const auto* right = static_cast<const LatticeData*>(
                PF_LOCK_HANDLE(extra->u.interp_func_params.right_arbH));
            LatticeData interpolated{};
            const bool valid = left && right &&
                InterpolateLattice(
                    *left,
                    *right,
                    extra->u.interp_func_params.tF,
                    interpolated);
            if (right) {
                PF_UNLOCK_HANDLE(
                    extra->u.interp_func_params.right_arbH);
            }
            if (left) {
                PF_UNLOCK_HANDLE(
                    extra->u.interp_func_params.left_arbH);
            }
            if (!valid) {
                return PF_Err_INTERNAL_STRUCT_DAMAGED;
            }
            PF_Err error =
                AllocateLattice(
                    in_data,
                    extra->u.interp_func_params.interpPH);
            if (error != PF_Err_NONE) {
                return error;
            }
            auto* destination = static_cast<LatticeData*>(
                PF_LOCK_HANDLE(*extra->u.interp_func_params.interpPH));
            if (!destination) {
                return PF_Err_OUT_OF_MEMORY;
            }
            *destination = interpolated;
            PF_UNLOCK_HANDLE(*extra->u.interp_func_params.interpPH);
            return PF_Err_NONE;
        }
        case PF_Arbitrary_COMPARE_FUNC: {
            const auto* first = static_cast<const LatticeData*>(
                PF_LOCK_HANDLE(extra->u.compare_func_params.a_arbH));
            const auto* second = static_cast<const LatticeData*>(
                PF_LOCK_HANDLE(extra->u.compare_func_params.b_arbH));
            const bool equal =
                first && second && CompareLattices(*first, *second);
            if (second) {
                PF_UNLOCK_HANDLE(extra->u.compare_func_params.b_arbH);
            }
            if (first) {
                PF_UNLOCK_HANDLE(extra->u.compare_func_params.a_arbH);
            }
            *extra->u.compare_func_params.compareP =
                equal ? PF_ArbCompare_EQUAL : PF_ArbCompare_NOT_EQUAL;
            return PF_Err_NONE;
        }
        case PF_Arbitrary_PRINT_SIZE_FUNC:
            *extra->u.print_size_func_params.print_sizePLu = 32768;
            return PF_Err_NONE;
        case PF_Arbitrary_PRINT_FUNC: {
            const auto* lattice = static_cast<const LatticeData*>(
                PF_LOCK_HANDLE(extra->u.print_func_params.arbH));
            if (!lattice) {
                return PF_Err_OUT_OF_MEMORY;
            }
            char* buffer =
                extra->u.print_func_params.print_bufferPC;
            const std::size_t capacity =
                extra->u.print_func_params.print_sizeLu;
            std::size_t used = 0;
            const auto append = [&](const char* format, auto... values) {
                if (!buffer || used >= capacity) {
                    return;
                }
                const int written = std::snprintf(
                    buffer + used,
                    capacity - used,
                    format,
                    values...);
                if (written <= 0) {
                    return;
                }
                used = std::min(
                    capacity,
                    used + static_cast<std::size_t>(written));
            };
            append(
                "SurfaceLabV1|surface=%llu|dx=%u|dy=%u|points=",
                static_cast<unsigned long long>(lattice->surface_id),
                lattice->divisions_x,
                lattice->divisions_y);
            for (std::size_t index = 0;
                 index < lattice->point_count;
                 ++index) {
                const StoredPoint3& point = lattice->points[index];
                append(
                    "%s%.9g,%.9g,%.9g",
                    index == 0 ? "" : ";",
                    static_cast<double>(point.x),
                    static_cast<double>(point.y),
                    static_cast<double>(point.z));
            }
            PF_UNLOCK_HANDLE(extra->u.print_func_params.arbH);
            return PF_Err_NONE;
        }
        case PF_Arbitrary_SCAN_FUNC:
            return CreateLatticeHandle(
                in_data,
                extra->u.scan_func_params.arbPH,
                in_data->width,
                in_data->height,
                SurfaceFromRefcon(
                    extra->u.scan_func_params.refconPV));
    }
    return PF_Err_UNRECOGNIZED_PARAM_TYPE;
}

SceneData ResolveSceneForFrame(
    PF_InData* in_data,
    PF_ParamDef* params[],
    A_long input_width,
    A_long input_height) {
    SceneData scene{};
    InitializeScene(scene, input_width, input_height);
    scene.surface_count = kSurfaceCount;
    for (std::uint32_t index = 0; index < kSurfaceCount; ++index) {
        SurfaceData& surface = scene.surfaces[index];
        InitializeFlatSurface(
            surface,
            index + 1,
            input_width,
            input_height,
            true);
        surface.enabled =
            params[SurfaceSourceParam(index)]->u.ld.data ? 1U : 0U;
        // Surface 1 falls back to the input so applying the effect immediately
        // produces a visible result.
        if (index == 0) {
            surface.enabled = 1;
        }
        surface.source_slot = index;
        surface.back_source_slot =
            params[SurfaceBackSourceParam(index)]->u.ld.data
                ? index + 1U
                : 0U;
        surface.image_size_mode = static_cast<std::uint32_t>(
            std::clamp<A_long>(
                params[SurfaceParam(index, kSurfaceImageSizeOffset)]
                    ->u.pd.value,
                kImageSizeStretch,
                kImageSizeFit));
        surface.image_border_mode =
            surface.image_size_mode == kImageSizeFit
                ? kImageBorderTransparent
                : kImageBorderClamp;
        const PF_Point3DDef& image_transform =
            params[SurfaceParam(
                index,
                kSurfaceImageTransformOffset)]
                ->u.point3d_d;
        surface.image_position_x = static_cast<float>(
            std::isfinite(image_transform.x_value)
                ? std::clamp(
                      image_transform.x_value,
                      -10000.0,
                      10000.0)
                : 0.0);
        surface.image_position_y = static_cast<float>(
            std::isfinite(image_transform.y_value)
                ? std::clamp(
                      image_transform.y_value,
                      -10000.0,
                      10000.0)
                : 0.0);
        surface.image_rotation = static_cast<float>(
            std::isfinite(image_transform.z_value)
                ? std::clamp(
                      image_transform.z_value,
                      -100000.0,
                      100000.0)
                : 0.0);
        surface.image_scale = static_cast<float>(std::clamp(
            params[SurfaceParam(index, kSurfaceImageScaleOffset)]
                ->u.fs_d.value,
            1.0,
            1000.0));
        surface.opacity = 100.0F;
        surface.diffuse = 100.0F;
        surface.specular = static_cast<float>(std::clamp(
            params[SurfaceParam(index, kSurfaceSpecularOffset)]
                ->u.fs_d.value,
            0.0,
            100.0));
        surface.metalness = static_cast<float>(std::clamp(
            params[SurfaceParam(index, kSurfaceMetalnessOffset)]
                ->u.fs_d.value,
            0.0,
            100.0));
        const double roughness = std::clamp(
            params[SurfaceParam(index, kSurfaceRoughnessOffset)]
                ->u.fs_d.value,
            0.0,
            100.0) /
            100.0;
        // A perceptual roughness control mapped to the existing
        // Blinn-Phong exponent. Roughness 50 preserves the v1.2 look.
        surface.shininess = static_cast<float>(
            1.0 + std::pow(1.0 - roughness, 3.0) * 248.0);
        surface.thickness = static_cast<float>(std::clamp(
            params[SurfaceParam(index, kSurfaceThicknessOffset)]
                ->u.fs_d.value,
            0.0,
            1000.0));
        surface.transform_mode = 1;
        const PF_Point3DDef& position =
            params[SurfaceParam(index, kSurfacePositionOffset)]
                ->u.point3d_d;
        surface.position_x = static_cast<float>(position.x_value);
        surface.position_y = static_cast<float>(position.y_value);
        surface.position_z = static_cast<float>(position.z_value);
        surface.rotation_x = static_cast<float>(FIX_2_FLOAT(
            params[SurfaceParam(index, kSurfaceRotationXOffset)]
                ->u.ad.value));
        surface.rotation_y = static_cast<float>(FIX_2_FLOAT(
            params[SurfaceParam(index, kSurfaceRotationYOffset)]
                ->u.ad.value));
        surface.rotation_z = static_cast<float>(FIX_2_FLOAT(
            params[SurfaceParam(index, kSurfaceRotationZOffset)]
                ->u.ad.value));
        surface.scale_x = static_cast<float>(
            params[SurfaceParam(index, kSurfaceScaleXOffset)]
                ->u.fs_d.value);
        surface.scale_y = static_cast<float>(
            params[SurfaceParam(index, kSurfaceScaleYOffset)]
                ->u.fs_d.value);
        surface.scale_z = static_cast<float>(
            params[SurfaceParam(index, kSurfaceScaleZOffset)]
                ->u.fs_d.value);
        surface.mesh_quality = static_cast<std::uint16_t>(
            std::clamp<A_long>(
                params[SurfaceParam(index, kSurfaceMeshQualityOffset)]
                    ->u.sd.value,
                kMinimumMeshQuality,
                kMaximumMeshQuality));
        surface.roll_angle = static_cast<float>(FIX_2_FLOAT(
            params[SurfaceParam(index, kSurfaceRollAngleOffset)]
                ->u.ad.value));
        surface.roll_tilt = static_cast<float>(FIX_2_FLOAT(
            params[SurfaceParam(index, kSurfaceRollTiltOffset)]
                ->u.ad.value));
        surface.roll_radius = static_cast<float>(std::max(
            1.0,
            params[SurfaceParam(index, kSurfaceRollRadiusOffset)]
                ->u.fs_d.value));
        surface.roll_expand = static_cast<float>(std::max(
            0.0,
            params[SurfaceParam(index, kSurfaceRollExpandOffset)]
                ->u.fs_d.value));
        const PF_Handle handle =
            params[SurfaceLatticeParam(index)]->u.arb_d.value;
        if (handle) {
            const auto* lattice =
                static_cast<const LatticeData*>(PF_LOCK_HANDLE(handle));
            if (lattice && IsValidLattice(*lattice)) {
                surface.lattice = *lattice;
            }
            if (lattice) {
                PF_UNLOCK_HANDLE(handle);
            }
        }
        const bool initialize_from_input =
            NeedsInputSizedInitialization(surface.lattice);
        if (initialize_from_input) {
            const std::uint64_t surface_id = surface.lattice.surface_id;
            InitializeLattice(
                surface.lattice,
                static_cast<std::uint16_t>(
                    params[SurfaceParam(index, kSurfaceDivisionsXOffset)]
                        ->u.sd.value),
                static_cast<std::uint16_t>(
                    params[SurfaceParam(index, kSurfaceDivisionsYOffset)]
                        ->u.sd.value),
                input_width,
                input_height,
                surface_id);
        }
        surface.divisions_x = surface.lattice.divisions_x;
        surface.divisions_y = surface.lattice.divisions_y;
        UpdateDerivedTransform(surface);
        // AE has already resolved the Point3D default percentages into layer
        // pixels by render time. Preserve that authored value even while the
        // arbitrary lattice is receiving its one-time input-sized fallback;
        // otherwise script-created effects lose their Position (especially Z)
        // until UPDATE_PARAMS_UI happens to run.
        surface.position_x = static_cast<float>(position.x_value);
        surface.position_y = static_cast<float>(position.y_value);
        surface.position_z = static_cast<float>(position.z_value);
    }
    for (std::uint32_t slot = 0;
         slot < kPointAnimationSlotCount;
         ++slot) {
        const PF_Point3DDef& metadata =
            params[PointAnimationMetadataParam(slot)]->u.point3d_d;
        const A_long encoded_surface =
            static_cast<A_long>(std::llround(metadata.x_value));
        const A_long encoded_row =
            static_cast<A_long>(std::llround(metadata.y_value));
        const A_long encoded_column =
            static_cast<A_long>(std::llround(metadata.z_value));
        if (encoded_surface < 1 ||
            encoded_surface > static_cast<A_long>(kSurfaceCount) ||
            encoded_row < 1 ||
            encoded_row >
                static_cast<A_long>(kMaximumLatticeAxisPoints) ||
            encoded_column < 1 ||
            encoded_column >
                static_cast<A_long>(kMaximumLatticeAxisPoints)) {
            continue;
        }
        SurfaceData& surface =
            scene.surfaces[
                static_cast<std::uint32_t>(encoded_surface - 1)];
        const auto row =
            static_cast<std::uint16_t>(encoded_row - 1);
        const auto column =
            static_cast<std::uint16_t>(encoded_column - 1);
        if (row > surface.lattice.divisions_y ||
            column > surface.lattice.divisions_x) {
            continue;
        }
        const PF_Point3DDef& value =
            params[PointAnimationValueParam(slot)]->u.point3d_d;
        StoredPoint3& point =
            surface.lattice.points[LatticePointIndex(
                surface.lattice.divisions_x,
                row,
                column)];
        if (std::isfinite(value.x_value)) {
            point.x = static_cast<float>(value.x_value);
        }
        if (std::isfinite(value.y_value)) {
            point.y = static_cast<float>(value.y_value);
        }
        if (std::isfinite(value.z_value)) {
            point.z = static_cast<float>(value.z_value);
        }
    }
    return scene;
}

extern "C" DllExport PF_Err EffectMain(
    PF_Cmd cmd,
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_ParamDef* params[],
    PF_LayerDef* output,
    void* extra) {
    try {
        switch (cmd) {
            case PF_Cmd_ABOUT:
                return About(in_data, out_data);
            case PF_Cmd_GLOBAL_SETUP:
                return GlobalSetup(in_data, out_data);
            case PF_Cmd_GLOBAL_SETDOWN:
                return GlobalSetdown(in_data);
            case PF_Cmd_PARAMS_SETUP:
                return ParamsSetup(in_data, out_data);
            case PF_Cmd_GPU_DEVICE_SETUP:
                return SetupMetalDevice(
                    in_data,
                    out_data,
                    static_cast<PF_GPUDeviceSetupExtra*>(extra));
            case PF_Cmd_GPU_DEVICE_SETDOWN:
                return SetdownMetalDevice(
                    in_data,
                    out_data,
                    static_cast<PF_GPUDeviceSetdownExtra*>(extra));
            case PF_Cmd_FRAME_SETUP:
                return FrameSetup(in_data, out_data, params);
            case PF_Cmd_RENDER:
                return Render(in_data, params, output);
            case PF_Cmd_SMART_PRE_RENDER:
                return SmartPreRender(
                    in_data,
                    out_data,
                    static_cast<PF_PreRenderExtra*>(extra));
            case PF_Cmd_SMART_RENDER:
                return SmartRender(
                    in_data,
                    out_data,
                    static_cast<PF_SmartRenderExtra*>(extra));
            case PF_Cmd_SMART_RENDER_GPU:
                return RenderMetalDiagnosticCopy(
                    in_data,
                    out_data,
                    static_cast<PF_SmartRenderExtra*>(extra));
            case PF_Cmd_ARBITRARY_CALLBACK:
                return HandleArbitrary(
                    in_data,
                    static_cast<PF_ArbParamsExtra*>(extra));
            case PF_Cmd_USER_CHANGED_PARAM:
                return UserChangedParam(
                    in_data,
                    out_data,
                    params,
                    static_cast<const PF_UserChangedParamExtra*>(extra));
            case PF_Cmd_UPDATE_PARAMS_UI:
                return UpdateParameterUi(in_data, out_data, params);
            case PF_Cmd_EVENT:
                return HandleSurfaceGizmoEvent(
                    in_data,
                    out_data,
                    params,
                    static_cast<PF_EventExtra*>(extra));
            default:
                return PF_Err_NONE;
        }
    } catch (...) {
        return PF_Err_INTERNAL_STRUCT_DAMAGED;
    }
}
