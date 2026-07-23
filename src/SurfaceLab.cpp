#include "SurfaceLab.h"
#include "AEGP_SuiteHandler.h"
#include "AEFX_SuiteHelper.h"
#include <adobesdk/DrawbotSuite.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <type_traits>
#include <vector>

#include "SurfaceLabInternal.h"
#include "SurfaceLabRender.h"
#include "SurfaceLabUI.h"

SceneData ResolveSceneForFrame(
    PF_InData* in_data,
    PF_ParamDef* params[],
    A_long input_width,
    A_long input_height) {
    SceneData scene{};
    bool has_active_scene = false;
    PF_Handle scene_handle = params[kParamSceneData]->u.arb_d.value;
    if (scene_handle) {
        const auto* scene_ptr =
            static_cast<const SceneData*>(PF_LOCK_HANDLE(scene_handle));
        if (scene_ptr) {
            scene = *scene_ptr;
            has_active_scene = IsValidScene(scene) && scene.active != 0;
            PF_UNLOCK_HANDLE(scene_handle);
        }
    }

    if (!has_active_scene) {
        InitializeScene(scene, input_width, input_height);
        CaptureLegacySurface(params, scene.surfaces[0]);
    } else {
        for (std::uint32_t index = 0; index < scene.surface_count; ++index) {
            SurfaceData& surface = scene.surfaces[index];
            if (scene.reserved[kAnimationBanksInitializedIndex] == 0 &&
                surface.animation_bank != 0) {
                continue;
            }
            auto view = BuildAnimationParameterView(params, surface.animation_bank);
            PF_ParamDef** surface_params = view.data();
            CaptureLegacySurface(surface_params, surface);
            ApplySizeAndPositionUi(surface_params, surface);
            CaptureScale(surface_params, surface);
            CaptureRotationOrigin(surface_params, surface);
            CaptureMaterial(surface_params, surface);
            CaptureThickness(surface_params, surface);
            CaptureDeform(surface_params, surface);
            if (scene.reserved[kAnimationStreamsInitializedIndex] != 0) {
                CaptureCornerCurl(surface_params, surface);
                CaptureEdgeTwist(surface_params, surface);
            }
        }
    }
    return scene;
}

void* SceneRefcon() {
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(kSceneMagic));
}

PF_Err CreateSceneHandle(
    PF_InData* in_data,
    PF_ArbitraryH* destination,
    double width,
    double height) {
    if (!destination) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }
    PF_Handle handle = PF_NEW_HANDLE(sizeof(SceneData));
    if (!handle) {
        return PF_Err_OUT_OF_MEMORY;
    }
    auto* scene = static_cast<SceneData*>(PF_LOCK_HANDLE(handle));
    if (!scene) {
        PF_DISPOSE_HANDLE(handle);
        return PF_Err_OUT_OF_MEMORY;
    }
    InitializeScene(*scene, width, height);
    PF_UNLOCK_HANDLE(handle);
    *destination = handle;
    return PF_Err_NONE;
}

PF_Err CopySceneHandle(
    PF_InData* in_data,
    PF_ArbitraryH source,
    PF_ArbitraryH* destination) {
    PF_Err error = CreateSceneHandle(in_data, destination, in_data->width, in_data->height);
    if (error != PF_Err_NONE || !source) {
        return error;
    }

    const auto* source_scene = static_cast<const SceneData*>(PF_LOCK_HANDLE(source));
    auto* destination_scene = static_cast<SceneData*>(PF_LOCK_HANDLE(*destination));
    if (!source_scene || !destination_scene) {
        if (source_scene) {
            PF_UNLOCK_HANDLE(source);
        }
        if (destination_scene) {
            PF_UNLOCK_HANDLE(*destination);
        }
        PF_DISPOSE_HANDLE(*destination);
        *destination = nullptr;
        return PF_Err_OUT_OF_MEMORY;
    }
    *destination_scene = *source_scene;
    PF_UNLOCK_HANDLE(*destination);
    PF_UNLOCK_HANDLE(source);
    return PF_Err_NONE;
}

PF_Err HandleArbitrary(PF_InData* in_data, PF_ArbParamsExtra* extra) {
    if (!extra) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }

    switch (extra->which_function) {
        case PF_Arbitrary_NEW_FUNC:
            if (extra->u.new_func_params.refconPV != SceneRefcon()) {
                return PF_Err_UNRECOGNIZED_PARAM_TYPE;
            }
            return CreateSceneHandle(
                in_data,
                extra->u.new_func_params.arbPH,
                in_data->width,
                in_data->height);

        case PF_Arbitrary_DISPOSE_FUNC:
            if (extra->u.dispose_func_params.arbH) {
                PF_DISPOSE_HANDLE(extra->u.dispose_func_params.arbH);
            }
            return PF_Err_NONE;

        case PF_Arbitrary_COPY_FUNC:
            return CopySceneHandle(
                in_data,
                extra->u.copy_func_params.src_arbH,
                extra->u.copy_func_params.dst_arbPH);

        case PF_Arbitrary_FLAT_SIZE_FUNC:
            *extra->u.flat_size_func_params.flat_data_sizePLu = sizeof(SceneData);
            return PF_Err_NONE;

        case PF_Arbitrary_FLATTEN_FUNC: {
            if (!extra->u.flatten_func_params.arbH ||
                !extra->u.flatten_func_params.flat_dataPV ||
                extra->u.flatten_func_params.buf_sizeLu < sizeof(SceneData)) {
                return PF_Err_BAD_CALLBACK_PARAM;
            }
            const auto* scene = static_cast<const SceneData*>(
                PF_LOCK_HANDLE(extra->u.flatten_func_params.arbH));
            if (!scene) {
                return PF_Err_OUT_OF_MEMORY;
            }
            std::memcpy(
                extra->u.flatten_func_params.flat_dataPV,
                scene,
                sizeof(SceneData));
            PF_UNLOCK_HANDLE(extra->u.flatten_func_params.arbH);
            return PF_Err_NONE;
        }

        case PF_Arbitrary_UNFLATTEN_FUNC: {
            if (!extra->u.unflatten_func_params.flat_dataPV) {
                return PF_Err_INTERNAL_STRUCT_DAMAGED;
            }
            SceneData flat_scene{};
            if (extra->u.unflatten_func_params.buf_sizeLu == sizeof(SceneData)) {
                std::memcpy(
                    &flat_scene,
                    extra->u.unflatten_func_params.flat_dataPV,
                    sizeof(SceneData));
                if (!IsValidScene(flat_scene)) {
                    return PF_Err_INTERNAL_STRUCT_DAMAGED;
                }
            } else if (
                extra->u.unflatten_func_params.buf_sizeLu == sizeof(SceneDataV12)) {
                SceneDataV12 old_scene{};
                std::memcpy(
                    &old_scene,
                    extra->u.unflatten_func_params.flat_dataPV,
                    sizeof(SceneDataV12));
                if (old_scene.magic != kSceneMagic ||
                    old_scene.schema_version != 12 ||
                    old_scene.surface_count < 1 ||
                    old_scene.surface_count > kMaximumSurfaces ||
                    old_scene.selected_surface >= old_scene.surface_count) {
                    return PF_Err_INTERNAL_STRUCT_DAMAGED;
                }
                MigrateSceneV12(old_scene, flat_scene);
            } else if (
                extra->u.unflatten_func_params.buf_sizeLu == sizeof(SceneDataV11)) {
                SceneDataV11 old_scene{};
                std::memcpy(
                    &old_scene,
                    extra->u.unflatten_func_params.flat_dataPV,
                    sizeof(SceneDataV11));
                if (old_scene.magic != kSceneMagic ||
                    (old_scene.schema_version != 10 &&
                     old_scene.schema_version != 11) ||
                    old_scene.surface_count < 1 ||
                    old_scene.surface_count > kMaximumSurfaces ||
                    old_scene.selected_surface >= old_scene.surface_count) {
                    return PF_Err_INTERNAL_STRUCT_DAMAGED;
                }
                if (old_scene.schema_version == 10) {
                    MigrateSceneV10(old_scene, flat_scene);
                } else {
                    MigrateSceneV11(old_scene, flat_scene);
                }
            } else if (
                extra->u.unflatten_func_params.buf_sizeLu == sizeof(SceneDataV9)) {
                SceneDataV9 old_scene{};
                std::memcpy(
                    &old_scene,
                    extra->u.unflatten_func_params.flat_dataPV,
                    sizeof(SceneDataV9));
                if (!IsValidSceneV9(old_scene)) {
                    return PF_Err_INTERNAL_STRUCT_DAMAGED;
                }
                MigrateSceneV9(old_scene, flat_scene);
            } else if (
                extra->u.unflatten_func_params.buf_sizeLu == sizeof(SceneDataV8)) {
                SceneDataV8 old_scene{};
                std::memcpy(
                    &old_scene,
                    extra->u.unflatten_func_params.flat_dataPV,
                    sizeof(SceneDataV8));
                if (!IsValidSceneV8(old_scene)) {
                    return PF_Err_INTERNAL_STRUCT_DAMAGED;
                }
                MigrateSceneV8(old_scene, flat_scene);
            } else if (
                extra->u.unflatten_func_params.buf_sizeLu == sizeof(SceneDataV7)) {
                SceneDataV7 old_scene{};
                std::memcpy(
                    &old_scene,
                    extra->u.unflatten_func_params.flat_dataPV,
                    sizeof(SceneDataV7));
                if (!IsValidSceneV7(old_scene)) {
                    return PF_Err_INTERNAL_STRUCT_DAMAGED;
                }
                MigrateSceneV7(old_scene, flat_scene);
            } else if (
                extra->u.unflatten_func_params.buf_sizeLu == sizeof(SceneDataV6)) {
                SceneDataV6 old_scene{};
                std::memcpy(
                    &old_scene,
                    extra->u.unflatten_func_params.flat_dataPV,
                    sizeof(SceneDataV6));
                if (!IsValidSceneV6(old_scene)) {
                    return PF_Err_INTERNAL_STRUCT_DAMAGED;
                }
                MigrateSceneV6(old_scene, flat_scene);
            } else if (
                extra->u.unflatten_func_params.buf_sizeLu == sizeof(SceneDataV5)) {
                std::uint32_t schema_version = 0;
                std::memcpy(
                    &schema_version,
                    static_cast<const std::uint8_t*>(
                        extra->u.unflatten_func_params.flat_dataPV) +
                        sizeof(std::uint32_t),
                    sizeof(schema_version));
                if (schema_version == 5) {
                    SceneDataV5 old_scene{};
                    std::memcpy(
                        &old_scene,
                        extra->u.unflatten_func_params.flat_dataPV,
                        sizeof(SceneDataV5));
                    if (!IsValidSceneV5(old_scene)) {
                        return PF_Err_INTERNAL_STRUCT_DAMAGED;
                    }
                    MigrateSceneV5(old_scene, flat_scene);
                } else if (schema_version == 4) {
                    SceneDataV4 old_scene{};
                    std::memcpy(
                        &old_scene,
                        extra->u.unflatten_func_params.flat_dataPV,
                        sizeof(SceneDataV4));
                    if (!IsValidSceneV4(old_scene)) {
                        return PF_Err_INTERNAL_STRUCT_DAMAGED;
                    }
                    MigrateSceneV4(old_scene, flat_scene);
                } else {
                    return PF_Err_INTERNAL_STRUCT_DAMAGED;
                }
            } else if (
                extra->u.unflatten_func_params.buf_sizeLu == sizeof(SceneDataV3)) {
                std::uint32_t schema_version = 0;
                std::memcpy(
                    &schema_version,
                    static_cast<const std::uint8_t*>(
                        extra->u.unflatten_func_params.flat_dataPV) +
                        sizeof(std::uint32_t),
                    sizeof(schema_version));
                if (schema_version == 3) {
                    SceneDataV3 old_scene{};
                    std::memcpy(
                        &old_scene,
                        extra->u.unflatten_func_params.flat_dataPV,
                        sizeof(SceneDataV3));
                    if (!IsValidSceneV3(old_scene)) {
                        return PF_Err_INTERNAL_STRUCT_DAMAGED;
                    }
                    MigrateSceneV3(old_scene, flat_scene);
                } else if (schema_version == 2) {
                    SceneDataV2 old_scene{};
                    std::memcpy(
                        &old_scene,
                        extra->u.unflatten_func_params.flat_dataPV,
                        sizeof(SceneDataV2));
                    if (!IsValidSceneV2(old_scene)) {
                        return PF_Err_INTERNAL_STRUCT_DAMAGED;
                    }
                    MigrateSceneV2(old_scene, flat_scene);
                } else {
                    return PF_Err_INTERNAL_STRUCT_DAMAGED;
                }
            } else if (extra->u.unflatten_func_params.buf_sizeLu == sizeof(SceneDataV1)) {
                SceneDataV1 old_scene{};
                std::memcpy(
                    &old_scene,
                    extra->u.unflatten_func_params.flat_dataPV,
                    sizeof(SceneDataV1));
                if (!IsValidSceneV1(old_scene)) {
                    return PF_Err_INTERNAL_STRUCT_DAMAGED;
                }
                MigrateSceneV1(old_scene, flat_scene);
            } else {
                return PF_Err_INTERNAL_STRUCT_DAMAGED;
            }
            if (flat_scene.reserved[kAnimationBanksInitializedIndex] == 0) {
                AssignLegacyAnimationBanks(flat_scene);
            }
            if (!IsValidScene(flat_scene)) {
                return PF_Err_INTERNAL_STRUCT_DAMAGED;
            }
            PF_Err error = CreateSceneHandle(
                in_data,
                extra->u.unflatten_func_params.arbPH,
                in_data->width,
                in_data->height);
            if (error != PF_Err_NONE) {
                return error;
            }
            auto* scene = static_cast<SceneData*>(
                PF_LOCK_HANDLE(*extra->u.unflatten_func_params.arbPH));
            if (!scene) {
                PF_DISPOSE_HANDLE(*extra->u.unflatten_func_params.arbPH);
                *extra->u.unflatten_func_params.arbPH = nullptr;
                return PF_Err_OUT_OF_MEMORY;
            }
            *scene = flat_scene;
            PF_UNLOCK_HANDLE(*extra->u.unflatten_func_params.arbPH);
            return PF_Err_NONE;
        }

        case PF_Arbitrary_INTERP_FUNC: {
            PF_ArbitraryH source = extra->u.interp_func_params.tF < 0.5
                                       ? extra->u.interp_func_params.left_arbH
                                       : extra->u.interp_func_params.right_arbH;
            return CopySceneHandle(
                in_data,
                source,
                extra->u.interp_func_params.interpPH);
        }

        case PF_Arbitrary_COMPARE_FUNC: {
            PF_ArbCompareResult result = PF_ArbCompare_NOT_EQUAL;
            PF_Handle first_handle = extra->u.compare_func_params.a_arbH;
            PF_Handle second_handle = extra->u.compare_func_params.b_arbH;
            if (first_handle && second_handle) {
                const auto* first = static_cast<const SceneData*>(PF_LOCK_HANDLE(first_handle));
                const auto* second = static_cast<const SceneData*>(PF_LOCK_HANDLE(second_handle));
                if (first && second && std::memcmp(first, second, sizeof(SceneData)) == 0) {
                    result = PF_ArbCompare_EQUAL;
                }
                if (second) {
                    PF_UNLOCK_HANDLE(second_handle);
                }
                if (first) {
                    PF_UNLOCK_HANDLE(first_handle);
                }
            } else if (!first_handle && !second_handle) {
                result = PF_ArbCompare_EQUAL;
            }
            *extra->u.compare_func_params.compareP = result;
            return PF_Err_NONE;
        }

        case PF_Arbitrary_PRINT_SIZE_FUNC:
            *extra->u.print_size_func_params.print_sizePLu = kScenePrintSize;
            return PF_Err_NONE;

        case PF_Arbitrary_PRINT_FUNC: {
            if (!extra->u.print_func_params.print_bufferPC ||
                extra->u.print_func_params.print_sizeLu == 0) {
                return PF_Err_BAD_CALLBACK_PARAM;
            }
            std::uint32_t count = 1;
            if (extra->u.print_func_params.arbH) {
                const auto* scene = static_cast<const SceneData*>(
                    PF_LOCK_HANDLE(extra->u.print_func_params.arbH));
                if (scene && IsValidScene(*scene)) {
                    count = scene->surface_count;
                }
                if (scene) {
                    PF_UNLOCK_HANDLE(extra->u.print_func_params.arbH);
                }
            }
            std::snprintf(
                extra->u.print_func_params.print_bufferPC,
                extra->u.print_func_params.print_sizeLu,
                "SurfaceLab scene v%u (%u surface%s)",
                kSceneSchemaVersion,
                count,
                count == 1 ? "" : "s");
            return PF_Err_NONE;
        }

        case PF_Arbitrary_SCAN_FUNC:
            return CreateSceneHandle(
                in_data,
                extra->u.scan_func_params.arbPH,
                in_data->width,
                in_data->height);
    }
    return PF_Err_UNRECOGNIZED_PARAM_TYPE;
}

PF_Err About(PF_OutData* out_data) {
    std::snprintf(
        out_data->return_msg,
        sizeof(out_data->return_msg),
        "SurfaceLab v%d.%d.%d\rBicubic surface prototype for After Effects.",
        static_cast<int>(kMajorVersion),
        static_cast<int>(kMinorVersion),
        static_cast<int>(kBugVersion));
    return PF_Err_NONE;
}

PF_Err GlobalSetup(PF_InData* in_data, PF_OutData* out_data) {
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    PF_Handle global_handle =
        suites.HandleSuite1()->host_new_handle(sizeof(GlobalData));
    if (!global_handle) {
        return PF_Err_OUT_OF_MEMORY;
    }
    auto* global = static_cast<GlobalData*>(
        suites.HandleSuite1()->host_lock_handle(global_handle));
    if (!global) {
        suites.HandleSuite1()->host_dispose_handle(global_handle);
        return PF_Err_OUT_OF_MEMORY;
    }
    *global = {};
    const A_Err registration_error =
        suites.UtilitySuite3()->AEGP_RegisterWithAEGP(
            nullptr,
            "SurfaceLab",
            &global->plugin_id);
    suites.HandleSuite1()->host_unlock_handle(global_handle);
    if (registration_error != A_Err_NONE) {
        suites.HandleSuite1()->host_dispose_handle(global_handle);
        return static_cast<PF_Err>(registration_error);
    }
    out_data->global_data = global_handle;
    out_data->my_version = PF_VERSION(
        kMajorVersion,
        kMinorVersion,
        kBugVersion,
        PF_Stage_DEVELOP,
        kBuildVersion);
    out_data->out_flags =
        PF_OutFlag_DEEP_COLOR_AWARE | PF_OutFlag_I_EXPAND_BUFFER |
        PF_OutFlag_SEND_UPDATE_PARAMS_UI | PF_OutFlag_CUSTOM_UI;
    out_data->out_flags2 =
        PF_OutFlag2_I_USE_3D_CAMERA | PF_OutFlag2_I_USE_3D_LIGHTS;
    return PF_Err_NONE;
}

PF_Err GlobalSetdown(PF_InData* in_data) {
    if (in_data->global_data) {
        AEGP_SuiteHandler suites(in_data->pica_basicP);
        suites.HandleSuite1()->host_dispose_handle(in_data->global_data);
    }
    return PF_Err_NONE;
}

extern "C" DllExport PF_Err PluginDataEntryFunction2(
    PF_PluginDataPtr in_ptr,
    PF_PluginDataCB2 callback,
    SPBasicSuite*,
    const char*,
    const char*) {
    PF_Err result = PF_Err_INVALID_CALLBACK;
    PF_REGISTER_EFFECT_EXT2(
        in_ptr,
        callback,
        "SurfaceLab",
        "XPK SurfaceLab",
        "SurfaceLab",
        AE_RESERVED_INFO,
        "EffectMain",
        "https://example.com");
    return result;
}

PF_Err EffectMain(
    PF_Cmd cmd,
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_ParamDef* params[],
    PF_LayerDef* output,
    void* extra) {
    try {
        switch (cmd) {
            case PF_Cmd_ABOUT:
                return About(out_data);
            case PF_Cmd_GLOBAL_SETUP:
                return GlobalSetup(in_data, out_data);
            case PF_Cmd_GLOBAL_SETDOWN:
                return GlobalSetdown(in_data);
            case PF_Cmd_PARAMS_SETUP:
                return ParamsSetup(in_data, out_data);
            case PF_Cmd_FRAME_SETUP:
                return FrameSetup(in_data, out_data, params);
            case PF_Cmd_RENDER:
                return Render(in_data, params, output);
            case PF_Cmd_USER_CHANGED_PARAM:
                return UserChangedParamV15(
                    in_data,
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
            case PF_Cmd_ARBITRARY_CALLBACK:
                return HandleArbitrary(
                    in_data,
                    static_cast<PF_ArbParamsExtra*>(extra));
            default:
                return PF_Err_NONE;
        }
    } catch (PF_Err error) {
        return error;
    } catch (...) {
        return PF_Err_INTERNAL_STRUCT_DAMAGED;
    }
}
