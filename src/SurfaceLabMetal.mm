#include "SurfaceLabMetal.h"

#include "AEFX_SuiteHelper.h"
#include "AE_EffectCBSuites.h"
#include "AE_EffectPixelFormat.h"
#include "AE_EffectGPUSuites.h"
#include "SurfaceLabInternal.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <algorithm>
#include <cstdint>

namespace {

constexpr std::uint32_t kMetalProbeValue = 0x534c1501U;

constexpr char kMetalProbeSource[] = R"METAL(
#include <metal_stdlib>
using namespace metal;

kernel void SurfaceLabDeviceProbe(
    device uint* result [[buffer(0)]],
    uint thread_index [[thread_position_in_grid]]) {
    if (thread_index == 0) {
        result[0] = 0x534c1501u;
    }
}

struct SurfaceLabCopyParams {
    uint source_pitch;
    uint destination_pitch;
    uint source_width;
    uint source_height;
    uint destination_width;
    uint destination_height;
};

kernel void SurfaceLabDiagnosticCopy(
    device const float4* source [[buffer(0)]],
    device float4* destination [[buffer(1)]],
    constant SurfaceLabCopyParams& params [[buffer(2)]],
    uint2 pixel [[thread_position_in_grid]]) {
    if (pixel.x >= params.destination_width ||
        pixel.y >= params.destination_height) {
        return;
    }
    float4 value = float4(0.0);
    if (pixel.x < params.source_width &&
        pixel.y < params.source_height) {
        value = source[
            pixel.y * params.source_pitch + pixel.x];
    }
    // A visible marker proves the diagnostic GPU path was selected without
    // making channel order assumptions about AE's BGRA float world.
    if (pixel.x < 32 && pixel.y < 32) {
        value = mix(value, float4(0.0, 1.0, 0.0, 1.0), 0.75);
    }
    destination[
        pixel.y * params.destination_pitch + pixel.x] = value;
}
)METAL";

struct MetalDeviceData {
    id<MTLComputePipelineState> probe_pipeline{};
    id<MTLComputePipelineState> copy_pipeline{};
};

struct MetalCopyParams {
    std::uint32_t source_pitch{};
    std::uint32_t destination_pitch{};
    std::uint32_t source_width{};
    std::uint32_t source_height{};
    std::uint32_t destination_width{};
    std::uint32_t destination_height{};
};

static_assert(
    sizeof(MetalCopyParams) == sizeof(std::uint32_t) * 6U,
    "Metal copy parameter packing changed");

PF_Err MetalError(NSError* error) {
    return error ? PF_Err_INTERNAL_STRUCT_DAMAGED : PF_Err_NONE;
}

PF_Err RunDeviceProbe(
    id<MTLDevice> device,
    id<MTLCommandQueue> queue,
    id<MTLComputePipelineState> pipeline) {
    if (!device || !queue || !pipeline) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }
    id<MTLBuffer> result = [device
        newBufferWithLength:sizeof(std::uint32_t)
        options:MTLResourceStorageModeShared];
    if (!result) {
        return PF_Err_OUT_OF_MEMORY;
    }
    *static_cast<std::uint32_t*>([result contents]) = 0U;

    id<MTLCommandBuffer> command_buffer = [queue commandBuffer];
    id<MTLComputeCommandEncoder> encoder =
        [command_buffer computeCommandEncoder];
    if (!command_buffer || !encoder) {
        [result release];
        return PF_Err_INTERNAL_STRUCT_DAMAGED;
    }
    [encoder setComputePipelineState:pipeline];
    [encoder setBuffer:result offset:0 atIndex:0];
    [encoder
        dispatchThreadgroups:MTLSizeMake(1, 1, 1)
        threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
    [encoder endEncoding];
    [command_buffer commit];
    [command_buffer waitUntilCompleted];

    const bool completed =
        [command_buffer status] == MTLCommandBufferStatusCompleted;
    const bool correct =
        *static_cast<const std::uint32_t*>([result contents]) ==
        kMetalProbeValue;
    const PF_Err error =
        completed && correct
            ? PF_Err_NONE
            : PF_Err_INTERNAL_STRUCT_DAMAGED;
    [result release];
    return error;
}

}  // namespace

PF_Err SetupMetalDevice(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_GPUDeviceSetupExtra* extra) {
    if (!in_data || !out_data || !extra ||
        !extra->input || !extra->output) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }
    if (extra->input->what_gpu != PF_GPU_Framework_METAL) {
        return PF_Err_NONE;
    }

    AEFX_SuiteScoper<PF_GPUDeviceSuite1> gpu_suite(
        in_data,
        kPFGPUDeviceSuite,
        kPFGPUDeviceSuiteVersion1,
        out_data);
    PF_GPUDeviceInfo device_info{};
    PF_Err error = gpu_suite->GetDeviceInfo(
        in_data->effect_ref,
        extra->input->device_index,
        &device_info);
    if (error != PF_Err_NONE) {
        return error;
    }
    if (!device_info.compatibleB ||
        device_info.device_framework != PF_GPU_Framework_METAL ||
        !device_info.devicePV ||
        !device_info.command_queuePV) {
        return PF_Err_NONE;
    }

    @autoreleasepool {
        id<MTLDevice> device =
            static_cast<id<MTLDevice>>(device_info.devicePV);
        id<MTLCommandQueue> queue =
            static_cast<id<MTLCommandQueue>>(
                device_info.command_queuePV);
        NSString* source = [NSString
            stringWithUTF8String:kMetalProbeSource];
        NSError* metal_error = nil;
        id<MTLLibrary> library =
            [device newLibraryWithSource:source
                                 options:nil
                                   error:&metal_error];
        if (!library) {
            return MetalError(metal_error);
        }
        id<MTLFunction> probe_function = [library
            newFunctionWithName:@"SurfaceLabDeviceProbe"];
        id<MTLFunction> copy_function = [library
            newFunctionWithName:@"SurfaceLabDiagnosticCopy"];
        if (!probe_function || !copy_function) {
            [probe_function release];
            [copy_function release];
            [library release];
            return PF_Err_INTERNAL_STRUCT_DAMAGED;
        }
        id<MTLComputePipelineState> probe_pipeline =
            [device newComputePipelineStateWithFunction:probe_function
                                                   error:&metal_error];
        [probe_function release];
        if (!probe_pipeline) {
            [copy_function release];
            [library release];
            return MetalError(metal_error);
        }
        id<MTLComputePipelineState> copy_pipeline =
            [device newComputePipelineStateWithFunction:copy_function
                                                   error:&metal_error];
        [copy_function release];
        [library release];
        if (!copy_pipeline) {
            [probe_pipeline release];
            return MetalError(metal_error);
        }

        error = RunDeviceProbe(device, queue, probe_pipeline);
        if (error != PF_Err_NONE) {
            [copy_pipeline release];
            [probe_pipeline release];
            return error;
        }

        AEFX_SuiteScoper<PF_HandleSuite1> handle_suite(
            in_data,
            kPFHandleSuite,
            kPFHandleSuiteVersion1,
            out_data);
        PF_Handle handle =
            handle_suite->host_new_handle(sizeof(MetalDeviceData));
        if (!handle || !*handle) {
            if (handle) {
                handle_suite->host_dispose_handle(handle);
            }
            [copy_pipeline release];
            [probe_pipeline release];
            return PF_Err_OUT_OF_MEMORY;
        }
        auto* data =
            reinterpret_cast<MetalDeviceData*>(*handle);
        data->probe_pipeline = probe_pipeline;
        data->copy_pipeline = copy_pipeline;
        extra->output->gpu_data = handle;
        auto* global =
            reinterpret_cast<GlobalData*>(in_data->global_data);
        if (global) {
            global->metal_devices_ready.fetch_add(
                1U,
                std::memory_order_relaxed);
        }
        out_data->out_flags2 =
            PF_OutFlag2_SUPPORTS_GPU_RENDER_F32;
    }
    return PF_Err_NONE;
}

PF_Err SetdownMetalDevice(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_GPUDeviceSetdownExtra* extra) {
    if (!in_data || !out_data || !extra || !extra->input) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }
    if (extra->input->what_gpu != PF_GPU_Framework_METAL ||
        !extra->input->gpu_data) {
        return PF_Err_NONE;
    }

    @autoreleasepool {
        PF_Handle handle = reinterpret_cast<PF_Handle>(
            const_cast<void*>(extra->input->gpu_data));
        if (handle && *handle) {
            auto* data =
                reinterpret_cast<MetalDeviceData*>(*handle);
            [data->copy_pipeline release];
            data->copy_pipeline = nil;
            [data->probe_pipeline release];
            data->probe_pipeline = nil;
            auto* global =
                reinterpret_cast<GlobalData*>(
                    in_data->global_data);
            if (global &&
                global->metal_devices_ready.load(
                    std::memory_order_relaxed) > 0U) {
                global->metal_devices_ready.fetch_sub(
                    1U,
                    std::memory_order_relaxed);
            }
        }
        AEFX_SuiteScoper<PF_HandleSuite1> handle_suite(
            in_data,
            kPFHandleSuite,
            kPFHandleSuiteVersion1,
            out_data);
        if (handle) {
            handle_suite->host_dispose_handle(handle);
        }
    }
    return PF_Err_NONE;
}

bool IsMetalDeviceReady(const PF_InData* in_data) {
    const auto* global =
        in_data
            ? reinterpret_cast<const GlobalData*>(
                  in_data->global_data)
            : nullptr;
    return global &&
           global->metal_devices_ready.load(
               std::memory_order_relaxed) > 0U;
}

bool MetalDiagnosticCopyEnabled(
    const PF_PreRenderExtra* extra) {
#if SURFACELAB_METAL_DIAGNOSTIC_COPY
    (void)extra;
    // AE chooses whether it can supply a GPU render only after PreRender
    // advertises the possibility. Do not gate this flag on gpu_data here:
    // the SDK reference effects advertise unconditionally and validate the
    // concrete framework/device in PF_Cmd_SMART_RENDER_GPU instead.
    return true;
#else
    (void)extra;
    return false;
#endif
}

PF_Err RenderMetalDiagnosticCopy(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_SmartRenderExtra* extra) {
#if !SURFACELAB_METAL_DIAGNOSTIC_COPY
    (void)in_data;
    (void)out_data;
    (void)extra;
    return PF_Err_UNRECOGNIZED_PARAM_TYPE;
#else
    if (in_data) {
        auto* global =
            reinterpret_cast<GlobalData*>(in_data->global_data);
        if (global) {
            global->metal_gpu_render_calls.fetch_add(
                1U,
                std::memory_order_relaxed);
        }
    }
    if (!in_data || !out_data || !extra ||
        !extra->input || !extra->cb ||
        extra->input->what_gpu != PF_GPU_Framework_METAL ||
        !extra->input->gpu_data) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }

    PF_EffectWorld* source_world = nullptr;
    PF_EffectWorld* destination_world = nullptr;
    PF_Err error = extra->cb->checkout_layer_pixels(
        in_data->effect_ref,
        0,
        &source_world);
    if (error != PF_Err_NONE || !source_world) {
        return error != PF_Err_NONE
                   ? error
                   : PF_Err_BAD_CALLBACK_PARAM;
    }
    const auto checkin_source = [&]() {
        extra->cb->checkin_layer_pixels(
            in_data->effect_ref,
            0);
    };
    error = extra->cb->checkout_output(
        in_data->effect_ref,
        &destination_world);
    if (error != PF_Err_NONE || !destination_world) {
        checkin_source();
        return error != PF_Err_NONE
                   ? error
                   : PF_Err_BAD_CALLBACK_PARAM;
    }

    AEFX_SuiteScoper<PF_WorldSuite2> world_suite(
        in_data,
        kPFWorldSuite,
        kPFWorldSuiteVersion2,
        out_data);
    PF_PixelFormat source_format = PF_PixelFormat_INVALID;
    PF_PixelFormat destination_format = PF_PixelFormat_INVALID;
    error = world_suite->PF_GetPixelFormat(
        source_world,
        &source_format);
    if (error == PF_Err_NONE) {
        error = world_suite->PF_GetPixelFormat(
            destination_world,
            &destination_format);
    }
    if (error != PF_Err_NONE ||
        source_format != PF_PixelFormat_GPU_BGRA128 ||
        destination_format != PF_PixelFormat_GPU_BGRA128 ||
        source_world->rowbytes <= 0 ||
        destination_world->rowbytes <= 0 ||
        source_world->rowbytes % 16 != 0 ||
        destination_world->rowbytes % 16 != 0) {
        checkin_source();
        return error != PF_Err_NONE
                   ? error
                   : PF_Err_UNRECOGNIZED_PARAM_TYPE;
    }

    AEFX_SuiteScoper<PF_GPUDeviceSuite1> gpu_suite(
        in_data,
        kPFGPUDeviceSuite,
        kPFGPUDeviceSuiteVersion1,
        out_data);
    PF_GPUDeviceInfo device_info{};
    error = gpu_suite->GetDeviceInfo(
        in_data->effect_ref,
        extra->input->device_index,
        &device_info);
    void* source_memory = nullptr;
    void* destination_memory = nullptr;
    if (error == PF_Err_NONE) {
        error = gpu_suite->GetGPUWorldData(
            in_data->effect_ref,
            source_world,
            &source_memory);
    }
    if (error == PF_Err_NONE) {
        error = gpu_suite->GetGPUWorldData(
            in_data->effect_ref,
            destination_world,
            &destination_memory);
    }
    if (error != PF_Err_NONE ||
        !source_memory || !destination_memory ||
        !device_info.devicePV ||
        !device_info.command_queuePV) {
        checkin_source();
        return error != PF_Err_NONE
                   ? error
                   : PF_Err_BAD_CALLBACK_PARAM;
    }

    PF_Handle handle = reinterpret_cast<PF_Handle>(
        const_cast<void*>(extra->input->gpu_data));
    if (!handle || !*handle) {
        checkin_source();
        return PF_Err_BAD_CALLBACK_PARAM;
    }
    auto* data =
        reinterpret_cast<MetalDeviceData*>(*handle);
    if (!data->copy_pipeline) {
        checkin_source();
        return PF_Err_INTERNAL_STRUCT_DAMAGED;
    }

    @autoreleasepool {
        id<MTLDevice> device =
            static_cast<id<MTLDevice>>(device_info.devicePV);
        id<MTLCommandQueue> queue =
            static_cast<id<MTLCommandQueue>>(
                device_info.command_queuePV);
        MetalCopyParams params{
            static_cast<std::uint32_t>(
                source_world->rowbytes / 16),
            static_cast<std::uint32_t>(
                destination_world->rowbytes / 16),
            static_cast<std::uint32_t>(
                std::max<A_long>(0, source_world->width)),
            static_cast<std::uint32_t>(
                std::max<A_long>(0, source_world->height)),
            static_cast<std::uint32_t>(
                std::max<A_long>(0, destination_world->width)),
            static_cast<std::uint32_t>(
                std::max<A_long>(0, destination_world->height))};
        if (params.destination_width == 0U ||
            params.destination_height == 0U) {
            checkin_source();
            return PF_Err_NONE;
        }
        id<MTLBuffer> parameter_buffer = [device
            newBufferWithBytes:&params
                        length:sizeof(params)
                       options:MTLResourceStorageModeShared];
        id<MTLCommandBuffer> command_buffer =
            [queue commandBuffer];
        id<MTLComputeCommandEncoder> encoder =
            [command_buffer computeCommandEncoder];
        if (!parameter_buffer || !command_buffer || !encoder) {
            [parameter_buffer release];
            checkin_source();
            return PF_Err_OUT_OF_MEMORY;
        }
        [encoder setComputePipelineState:data->copy_pipeline];
        [encoder
            setBuffer:static_cast<id<MTLBuffer>>(source_memory)
            offset:0
            atIndex:0];
        [encoder
            setBuffer:static_cast<id<MTLBuffer>>(
                destination_memory)
            offset:0
            atIndex:1];
        [encoder setBuffer:parameter_buffer offset:0 atIndex:2];
        constexpr NSUInteger kThreadWidth = 8;
        constexpr NSUInteger kThreadHeight = 8;
        const MTLSize groups = MTLSizeMake(
            (params.destination_width + kThreadWidth - 1U) /
                kThreadWidth,
            (params.destination_height + kThreadHeight - 1U) /
                kThreadHeight,
            1);
        [encoder
            dispatchThreadgroups:groups
            threadsPerThreadgroup:
                MTLSizeMake(kThreadWidth, kThreadHeight, 1)];
        [encoder endEncoding];
        [command_buffer commit];
        [command_buffer waitUntilCompleted];
        const bool completed =
            [command_buffer status] ==
            MTLCommandBufferStatusCompleted;
        [parameter_buffer release];
        checkin_source();
        return completed
                   ? PF_Err_NONE
                   : PF_Err_INTERNAL_STRUCT_DAMAGED;
    }
#endif
}
