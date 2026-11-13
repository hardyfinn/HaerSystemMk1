// Shared colours (spec §1.4). Keep in step with res/make_panels.py.
#pragma once
#include <rack.hpp>

namespace codex {
namespace theme {

inline NVGcolor panel() { return nvgRGB(0x1d, 0x1d, 0x1f); }
inline NVGcolor text() { return nvgRGB(0xd8, 0xd8, 0xd8); }
inline NVGcolor dim() { return nvgRGB(0x7a, 0x7a, 0x80); }
inline NVGcolor screen() { return nvgRGB(0x0b, 0x0b, 0x0d); }
inline NVGcolor grid() { return nvgRGB(0x2c, 0x2c, 0x30); }
// One accent colour per layer.
inline NVGcolor time() { return nvgRGB(0xe8, 0xa3, 0x3c); }   // amber: WARP, MUTATE, CONTOUR, CHAIN, FLOW, TUNE MAP
inline NVGcolor sound() { return nvgRGB(0x3c, 0xc8, 0xb4); }  // teal: STRIKE, FMX, SPLICE, DEPTH
inline NVGcolor system() { return nvgRGB(0x9b, 0x7b, 0xe8); } // violet: VAULT, STEER, EAR

} // namespace theme
} // namespace codex
