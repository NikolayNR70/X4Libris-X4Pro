#pragma once
#include <cstdint>

// Defined once in LogoX4Libris.cpp; shared by boot and sleep screens.
extern const uint8_t LogoX4Libris[];

class GfxRenderer;
void drawX4LibrisLogo(GfxRenderer& renderer, int x, int y);
inline constexpr int LogoX4LibrisWidth = 440;
inline constexpr int LogoX4LibrisHeight = 161;
