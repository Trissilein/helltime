#pragma once

#include <d2d1.h>

namespace helltime::ui::design {

inline constexpr float Space2 = 2.0f;
inline constexpr float Space4 = 4.0f;
inline constexpr float Space6 = 6.0f;
inline constexpr float Space8 = 8.0f;
inline constexpr float Space10 = 10.0f;
inline constexpr float Space12 = 12.0f;

inline constexpr float Radius4 = 4.0f;
inline constexpr float Radius6 = 6.0f;
inline constexpr float Radius8 = 8.0f;
inline constexpr float Radius10 = 10.0f;

inline constexpr float Text11 = 11.0f;
inline constexpr float Text12 = 12.0f;
inline constexpr float Text13 = 13.0f;
inline constexpr float Text14 = 14.0f;
inline constexpr float Text16 = 16.0f;
inline constexpr float Text18 = 18.0f;
inline constexpr float CardCountdown = 20.0f;

inline constexpr wchar_t FontUi[] = L"Segoe UI";
inline constexpr wchar_t FontMono[] = L"Consolas";

inline constexpr D2D1_COLOR_F Background{0.043137f, 0.043137f, 0.043137f, 1.0f}; // #0b0b0b
inline constexpr D2D1_COLOR_F BackgroundSubtle{0.070588f, 0.070588f, 0.070588f, 1.0f}; // #121212
inline constexpr D2D1_COLOR_F Gold{0.831373f, 0.686275f, 0.215686f, 1.0f}; // #d4af37
inline constexpr D2D1_COLOR_F GoldDim{0.627451f, 0.517647f, 0.156863f, 1.0f}; // #a08428
inline constexpr D2D1_COLOR_F GoldBright{0.956863f, 0.815686f, 0.247059f, 1.0f}; // #f4d03f
inline constexpr D2D1_COLOR_F Burgundy{0.058824f, 0.019608f, 0.019608f, 1.0f}; // #0f0505
inline constexpr D2D1_COLOR_F BurgundyDark{0.019608f, 0.007843f, 0.007843f, 1.0f}; // #050202
inline constexpr D2D1_COLOR_F BurgundyBright{0.101961f, 0.031373f, 0.031373f, 1.0f}; // #1a0808

inline constexpr D2D1_COLOR_F TextPrimary{1.0f, 1.0f, 1.0f, 0.96f};
inline constexpr D2D1_COLOR_F TextSecondary{1.0f, 1.0f, 1.0f, 0.87f};
inline constexpr D2D1_COLOR_F TextTertiary{1.0f, 1.0f, 1.0f, 0.72f};
inline constexpr D2D1_COLOR_F TextMuted{1.0f, 1.0f, 1.0f, 0.62f};
inline constexpr D2D1_COLOR_F TextDisabled{1.0f, 1.0f, 1.0f, 0.45f};
inline constexpr D2D1_COLOR_F Panel{1.0f, 1.0f, 1.0f, 0.05f};
inline constexpr D2D1_COLOR_F Panel2{1.0f, 1.0f, 1.0f, 0.08f};
inline constexpr D2D1_COLOR_F Elevated1{1.0f, 1.0f, 1.0f, 0.04f};
inline constexpr D2D1_COLOR_F Elevated2{1.0f, 1.0f, 1.0f, 0.07f};
inline constexpr D2D1_COLOR_F Elevated3{1.0f, 1.0f, 1.0f, 0.10f};
inline constexpr D2D1_COLOR_F BorderSubtle{1.0f, 1.0f, 1.0f, 0.25f};
inline constexpr D2D1_COLOR_F Border{1.0f, 1.0f, 1.0f, 0.35f};
inline constexpr D2D1_COLOR_F BorderStrong{1.0f, 1.0f, 1.0f, 0.45f};

inline constexpr D2D1_COLOR_F Helltide{0.6f, 0.121569f, 0.121569f, 1.0f}; // #991f1f
inline constexpr D2D1_COLOR_F Legion{0.701961f, 0.141176f, 0.141176f, 1.0f}; // #b32424
inline constexpr D2D1_COLOR_F WorldBoss{0.478431f, 0.086275f, 0.086275f, 1.0f}; // #7a1616
inline constexpr D2D1_COLOR_F HelltideBright{0.701961f, 0.141176f, 0.141176f, 1.0f}; // #b32424
inline constexpr D2D1_COLOR_F LegionBright{0.8f, 0.160784f, 0.160784f, 1.0f}; // #cc2929
inline constexpr D2D1_COLOR_F WorldBossBright{0.560784f, 0.101961f, 0.101961f, 1.0f}; // #8f1a1a

constexpr D2D1_COLOR_F WithAlpha(D2D1_COLOR_F color, float alpha) noexcept {
    color.a = alpha;
    return color;
}

} // namespace helltime::ui::design
