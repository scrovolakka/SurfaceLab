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
constexpr A_short kSurfaceLabVersionMinor = 2;
constexpr A_short kSurfaceLabVersionPatch = 3;
constexpr const char* kSurfaceLabVersionString = "1.2.3";

constexpr std::uint32_t kSurfaceCount = 8;
constexpr PF_ParamIndex kSurfaceParameterStride = 18;
constexpr PF_ParamIndex kRigBridgeCoordinateCount = 17 * 3;

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
        kRigBridgeCoordinateCount,
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

constexpr PF_ParamIndex SurfaceLatticeParam(std::uint32_t surface) {
    return SurfaceParam(surface, kSurfaceLatticeOffset);
}

constexpr PF_ParamIndex RigPointParam(std::uint32_t column) {
    return kParamRigPointsStart +
           static_cast<PF_ParamIndex>(column * 3U);
}

constexpr PF_ParamIndex RigPointCoordinateParam(
    std::uint32_t column,
    std::uint32_t axis) {
    return RigPointParam(column) + static_cast<PF_ParamIndex>(axis);
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
    kDiskRigPointsStart = 720,
    kDiskRigBridgeEnd = 799,
    kDiskAboutStart = 800,
    kDiskAboutVersion,
    kDiskAboutEnd = 809
};

constexpr A_long SurfaceDiskId(
    std::uint32_t surface,
    SurfaceParamOffset offset) {
    return kDiskSurfaceParametersStart +
           static_cast<A_long>(surface) * 32 +
           static_cast<A_long>(offset);
}

extern "C" {

DllExport PF_Err EffectMain(
    PF_Cmd cmd,
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_ParamDef* params[],
    PF_LayerDef* output,
    void* extra);

}
