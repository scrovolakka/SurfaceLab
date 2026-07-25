#pragma once

#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_EffectUI.h"
#include "AE_Macros.h"
#include "AEConfig.h"
#include "entry.h"
#include "Param_Utils.h"

#include <cstddef>
#include <cstdint>

// Keep these in lockstep with CMake project VERSION and SurfaceLabPiPL.r.
constexpr A_short kSurfaceLabVersionMajor = 1;
constexpr A_short kSurfaceLabVersionMinor = 3;
constexpr A_short kSurfaceLabVersionPatch = 3;
constexpr const char* kSurfaceLabVersionString = "1.3.3";

constexpr std::uint32_t kSurfaceCount = 8;
constexpr PF_ParamIndex kSurfaceParameterStride = 23;
// The hidden script bridge uses one 3D Point per lattice column. Keeping XYZ
// in one parameter leaves room under AE's 255-parameter effect limit.
constexpr PF_ParamIndex kRigBridgePointCount = 17;

enum SurfaceParamOffset : PF_ParamIndex {
    kSurfaceTopicStartOffset = 0,
    kSurfaceSourceOffset,
    kSurfacePositionOffset,
    kSurfaceRotationXOffset,
    kSurfaceRotationYOffset,
    kSurfaceRotationZOffset,
    kSurfaceScaleXOffset,
    kSurfaceScaleYOffset,
    kSurfaceScaleZOffset,
    kSurfaceDivisionsXOffset,
    kSurfaceDivisionsYOffset,
    kSurfaceMeshQualityOffset,
    kSurfaceRollAngleOffset,
    kSurfaceRollTiltOffset,
    kSurfaceRollRadiusOffset,
    kSurfaceRollExpandOffset,
    kSurfaceLatticeOffset,
    kSurfaceBackSourceOffset,
    kSurfaceImageSizeOffset,
    kSurfaceSpecularOffset,
    kSurfaceRoughnessOffset,
    kSurfaceMetalnessOffset,
    kSurfaceTopicEndOffset
};

enum ParamIndex : PF_ParamIndex {
    kParamInput = 0,
    kParamSceneStart,
    kParamScenePosition,
    kParamSceneRotationX,
    kParamSceneRotationY,
    kParamSceneRotationZ,
    kParamSceneScaleX,
    kParamSceneScaleY,
    kParamSceneScaleZ,
    kParamSceneEnd,
    kParamSurfacesStart,
    kParamSurfaceParametersStart,
    kParamSurfacesEnd =
        kParamSurfaceParametersStart +
        kSurfaceCount * kSurfaceParameterStride,
    kParamRenderView,
    kParamRigBridgeStart,
    kParamRigSurface,
    kParamRigRow,
    kParamRigSurfaceId0,
    kParamRigSurfaceId1,
    kParamRigSurfaceId2,
    kParamRigSurfaceId3,
    kParamRigDivisionsX,
    kParamRigDivisionsY,
    kParamRigPointsStart,
    kParamRigPointsEnd =
        kParamRigPointsStart +
        kRigBridgePointCount,
    kParamRigBridgeEnd = kParamRigPointsEnd,
    kParamAboutStart,
    kParamAboutVersion,
    kParamAboutEnd,
    kParamCount
};

constexpr PF_ParamIndex SurfaceParam(
    std::uint32_t surface,
    SurfaceParamOffset offset) {
    return kParamSurfaceParametersStart +
           static_cast<PF_ParamIndex>(surface) * kSurfaceParameterStride +
           static_cast<PF_ParamIndex>(offset);
}

constexpr PF_ParamIndex SurfaceSourceParam(std::uint32_t surface) {
    return SurfaceParam(surface, kSurfaceSourceOffset);
}

constexpr PF_ParamIndex SurfaceBackSourceParam(std::uint32_t surface) {
    return SurfaceParam(surface, kSurfaceBackSourceOffset);
}

constexpr PF_ParamIndex SurfaceLatticeParam(std::uint32_t surface) {
    return SurfaceParam(surface, kSurfaceLatticeOffset);
}

constexpr PF_ParamIndex RigPointParam(std::uint32_t column) {
    return kParamRigPointsStart + static_cast<PF_ParamIndex>(column);
}

enum ParamDiskId : A_long {
    kDiskSceneStart = 100,
    kDiskScenePosition,
    kDiskSceneRotationX,
    kDiskSceneRotationY,
    kDiskSceneRotationZ,
    kDiskSceneScaleX,
    kDiskSceneScaleY,
    kDiskSceneScaleZ,
    kDiskSceneEnd,
    kDiskSurfacesStart = 200,
    kDiskSurfaceParametersStart = 300,
    kDiskSurfacesEnd = 500,
    kDiskRenderView = 600,
    kDiskRigBridgeStart = 700,
    kDiskRigSurface,
    kDiskRigRow,
    kDiskRigSurfaceId0,
    kDiskRigSurfaceId1,
    kDiskRigSurfaceId2,
    kDiskRigSurfaceId3,
    kDiskRigDivisionsX,
    kDiskRigDivisionsY,
    // v1.3.2 used 720...770 for separate XYZ float sliders. Allocate the
    // compact 3D Point bridge elsewhere so saved projects never see a disk-ID
    // type change.
    kDiskRigPointsStart = 780,
    kDiskRigBridgeEnd = 799,
    kDiskAboutStart = 800,
    kDiskAboutVersion,
    kDiskAboutEnd = 809
};

constexpr A_long SurfaceDiskId(
    std::uint32_t surface,
    SurfaceParamOffset offset) {
    // v1.2.x ended each surface topic at offset 17. Keep that persisted ID
    // stable and allocate the v1.3 material controls after it.
    const A_long persisted_offset =
        offset == kSurfaceTopicEndOffset
            ? 17
            : (offset >= kSurfaceBackSourceOffset
                   ? 18 + static_cast<A_long>(
                              offset - kSurfaceBackSourceOffset)
                   : static_cast<A_long>(offset));
    return kDiskSurfaceParametersStart +
           static_cast<A_long>(surface) * 32 +
           persisted_offset;
}

static_assert(kParamCount <= 255, "After Effects supports at most 255 params");

extern "C" {

DllExport PF_Err EffectMain(
    PF_Cmd cmd,
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_ParamDef* params[],
    PF_LayerDef* output,
    void* extra);

}
