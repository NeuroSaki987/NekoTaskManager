#include "Theme.h"

const Palette& Theme::Current()
{
    static Palette p {
        D2D1::ColorF(0x08101A, 0.20f),
        D2D1::ColorF(0xF7FBFF, 0.78f),
        D2D1::ColorF(0xEEF5FF, 0.68f),
        D2D1::ColorF(0x08111C, 1.00f),
        D2D1::ColorF(0x334155, 1.00f),
        D2D1::ColorF(0x2F80ED, 1.00f),
        D2D1::ColorF(0xD9EBFF, 0.88f),
        D2D1::ColorF(0xD9534F, 1.00f),
        D2D1::ColorF(0xBCD0E8, 0.92f),
        D2D1::ColorF(0x0EA5E9, 1.00f),
        D2D1::ColorF(0x6366F1, 1.00f),
        D2D1::ColorF(0xF59E0B, 1.00f),
        D2D1::ColorF(0x10B981, 1.00f)
    };
    return p;
}

float Theme::Dp(float px, float dpi)
{
    return px * dpi / 96.0f;
}
