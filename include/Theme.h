#pragma once
#include "Common.h"

struct Palette {
    D2D1_COLOR_F bg;
    D2D1_COLOR_F panel;
    D2D1_COLOR_F panelAlt;
    D2D1_COLOR_F text;
    D2D1_COLOR_F subText;
    D2D1_COLOR_F accent;
    D2D1_COLOR_F accentSoft;
    D2D1_COLOR_F danger;
    D2D1_COLOR_F border;
    D2D1_COLOR_F cpu;
    D2D1_COLOR_F mem;
    D2D1_COLOR_F disk;
    D2D1_COLOR_F net;
};

class Theme {
public:
    static const Palette& Current();
    static float Dp(float px, float dpi);
};
