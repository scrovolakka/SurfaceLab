#include "AEConfig.h"
#include "AE_EffectVers.h"

#ifndef AE_OS_WIN
    #include "AE_General.r"
#endif

resource 'PiPL' (16000) {
    {
        Kind { AEEffect },
        Name { "SurfaceLab" },
        Category { "SurfaceLab" },
#ifdef AE_OS_WIN
    #if defined(AE_PROC_INTELx64)
        CodeWin64X86 { "EffectMain" },
    #elif defined(AE_PROC_ARM64)
        CodeWinARM64 { "EffectMain" },
    #endif
#elif defined(AE_OS_MAC)
        CodeMacIntel64 { "EffectMain" },
        CodeMacARM64 { "EffectMain" },
#endif
        AE_PiPL_Version { 2, 0 },
        AE_Effect_Spec_Version { PF_PLUG_IN_VERSION, PF_PLUG_IN_SUBVERS },
        /* PF_VERSION packs subvers/bugvers into 4-bit fields, so the AE
           version saturates at 0.15.15 and later releases bump the build
           field: bundle 0.15.16 == PF_VERSION(0, 15, 15, DEVELOP, 2). */
        AE_Effect_Version { 522242 },
        AE_Effect_Info_Flags { 0 },
        AE_Effect_Global_OutFlags { 0x06008200 },
        AE_Effect_Global_OutFlags_2 { 0x00201406 },
        AE_Reserved_Info { 0 },
        AE_Effect_Match_Name { "XPK SurfaceLab" },
        AE_Effect_Support_URL { "https://github.com/scrovolakka/SurfaceLab" }
    }
};
