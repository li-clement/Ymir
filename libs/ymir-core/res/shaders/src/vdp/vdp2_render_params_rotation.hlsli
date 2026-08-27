#ifndef YMIR_VDP_VDP2_RENDER_PARAMS_ROTATION_HLSLI
#define YMIR_VDP_VDP2_RENDER_PARAMS_ROTATION_HLSLI

#include "vdp2_render_params_window.hlsli"

// See C++ code for documentation on the fields

struct RotRegs {
    bool coeffTableEnable;
    bool coeffTableCRAM;
    uint coeffDataSize;
    uint coeffDataMode;
    bool coeffDataAccessA0;
    bool coeffDataAccessA1;
    bool coeffDataAccessB0;
    bool coeffDataAccessB1;
    bool coeffDataPerDot;
    bool coeffUseLineColorData;
};

struct RotParamState {
    int2 screenCoords;
    uint2 spriteCoords;
    uint coeffData;
};

#endif
