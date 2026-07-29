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
constexpr A_short kSurfaceLabVersionMinor = 7;
constexpr A_short kSurfaceLabVersionPatch = 1;
constexpr const char* kSurfaceLabVersionString = "1.7.1";

constexpr std::uint32_t kSurfaceCount = 8;
constexpr PF_ParamIndex kSurfaceParameterStride = 26;
// The hidden script bridge uses one 3D Point per lattice column. Request and
// metadata are packed into two more Point3D streams to keep the internal
// transport compact without touching authored Surface streams.
constexpr PF_ParamIndex kRigBridgePointCount = 17;
constexpr PF_ParamIndex kPointAnimationSlotCount = 32;
constexpr PF_ParamIndex kPointAnimationSlotStride = 2;

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
    kSurfaceThicknessOffset,
    kSurfaceImageTransformOffset,
    kSurfaceImageScaleOffset,
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
    kParamAntialiasing,
    kParamEditMode,
    kParamTransformTool,
    kParamTransformSpace,
    kParamRigRequest,
    kParamRigMetadata,
    kParamRigPointsStart,
    kParamRigPointsEnd =
        kParamRigPointsStart +
        kRigBridgePointCount,
    kParamPointAnimationStart = kParamRigPointsEnd,
    kParamPointAnimationExpose = kParamPointAnimationStart + 1,
    kParamPointAnimationClear,
    kParamPointAnimationSlotsStart,
    kParamPointAnimationSlotsEnd =
        kParamPointAnimationSlotsStart +
        kPointAnimationSlotCount * kPointAnimationSlotStride,
    kParamPointAnimationEnd = kParamPointAnimationSlotsEnd,
    kParamCreateNullRig,
    kParamAboutVersion,
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

constexpr PF_ParamIndex PointAnimationMetadataParam(std::uint32_t slot) {
    return kParamPointAnimationSlotsStart +
           static_cast<PF_ParamIndex>(slot) * kPointAnimationSlotStride;
}

constexpr PF_ParamIndex PointAnimationValueParam(std::uint32_t slot) {
    return PointAnimationMetadataParam(slot) + 1;
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
    kDiskAntialiasing,
    kDiskEditMode,
    kDiskTransformTool,
    kDiskTransformSpace,
    // 700...708 and 799 are the retired v1.4.3 Bridge topic, selectors,
    // metadata, and end topic streams. Never reuse those persisted IDs.
    // v1.3.2 used 720...770 for separate XYZ float sliders. Allocate the
    // compact 3D Point bridge elsewhere so saved projects never see a disk-ID
    // type change.
    kDiskRigPointsStart = 780,
    // About v1.4.x used topic IDs 800 and 809. Keep the button's persisted ID
    // while dropping only its decorative topic wrapper.
    kDiskAboutVersion = 801,
    // v1.5 compact Bridge streams use fresh IDs.
    kDiskRigRequest = 810,
    kDiskRigMetadata,
    kDiskCreateNullRig,
    kDiskPointAnimationStart = 820,
    kDiskPointAnimationExpose,
    kDiskPointAnimationClear,
    kDiskPointAnimationSlotsStart = 830,
    kDiskPointAnimationEnd =
        kDiskPointAnimationSlotsStart +
        kPointAnimationSlotCount * kPointAnimationSlotStride
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

static_assert(kParamCount == 314, "Update the documented parameter layout");
// SurfaceLabCreateNullRig.jsx addresses the hidden bridge by AE effect
// property index. Keep these assertions beside the parameter layout so a
// future UI addition cannot silently desynchronise the script.
static_assert(kParamRigRequest == 225, "Update Null Rig bridge indices");
static_assert(kParamRigMetadata == 226, "Update Null Rig bridge indices");
static_assert(kParamRigPointsStart == 227, "Update Null Rig bridge indices");
static_assert(kParamPointAnimationStart == 244, "Update point animation index");
static_assert(kParamCreateNullRig == 312, "Update Null Rig button index");
static_assert(kParamAboutVersion == 313, "Update About parameter index");

extern "C" {

DllExport PF_Err EffectMain(
    PF_Cmd cmd,
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_ParamDef* params[],
    PF_LayerDef* output,
    void* extra);

}
