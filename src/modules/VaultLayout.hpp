// Written by res/make_panels.py. Do not edit: change the script and run it again.
// Positions are in millimetres from the panel's top left corner.
#pragma once

namespace layout_vault {

inline constexpr float HP = 40;
inline constexpr float DISPLAY[2][2] = {{5.00f, 15.00f}, {80.00f, 44.00f}};
inline constexpr float TEMPO[2] = {13.00f, 72.00f};
inline constexpr float BARS[2] = {29.50f, 72.00f};
inline constexpr float KEEP[2] = {46.00f, 72.00f};
inline constexpr float SEED[2] = {79.00f, 72.00f};
inline constexpr float LANE[2] = {13.00f, 89.00f};
inline constexpr float CLIP[2] = {29.50f, 89.00f};
inline constexpr float THIN[2] = {46.00f, 89.00f};
inline constexpr float TRANSPOSE[2] = {62.50f, 89.00f};
inline constexpr float PHASE_IN[2] = {11.00f, 104.50f};
inline constexpr float KEEP_IN[2] = {24.60f, 104.50f};
inline constexpr float CLIP_IN[2] = {38.20f, 104.50f};
inline constexpr float THIN_IN[2] = {51.80f, 104.50f};
inline constexpr float TRANSPOSE_IN[2] = {65.40f, 104.50f};
inline constexpr float STOP_IN[2] = {79.00f, 104.50f};
inline constexpr float END_OUT[2] = {79.00f, 119.50f};
inline constexpr float GATE_IN[8][2] = {{108.00f, 31.00f}, {120.20f, 31.00f}, {132.40f, 31.00f}, {144.60f, 31.00f}, {156.80f, 31.00f}, {169.00f, 31.00f}, {181.20f, 31.00f}, {193.40f, 31.00f}};
inline constexpr float CV_IN[8][2] = {{108.00f, 45.00f}, {120.20f, 45.00f}, {132.40f, 45.00f}, {144.60f, 45.00f}, {156.80f, 45.00f}, {169.00f, 45.00f}, {181.20f, 45.00f}, {193.40f, 45.00f}};
inline constexpr float LAUNCH[8][2] = {{108.00f, 57.50f}, {120.20f, 57.50f}, {132.40f, 57.50f}, {144.60f, 57.50f}, {156.80f, 57.50f}, {169.00f, 57.50f}, {181.20f, 57.50f}, {193.40f, 57.50f}};
inline constexpr float LAUNCH_IN[8][2] = {{108.00f, 68.50f}, {120.20f, 68.50f}, {132.40f, 68.50f}, {144.60f, 68.50f}, {156.80f, 68.50f}, {169.00f, 68.50f}, {181.20f, 68.50f}, {193.40f, 68.50f}};
inline constexpr float PLAY_LIGHT[8][2] = {{108.00f, 82.00f}, {120.20f, 82.00f}, {132.40f, 82.00f}, {144.60f, 82.00f}, {156.80f, 82.00f}, {169.00f, 82.00f}, {181.20f, 82.00f}, {193.40f, 82.00f}};
inline constexpr float GATE_OUT[8][2] = {{108.00f, 105.50f}, {120.20f, 105.50f}, {132.40f, 105.50f}, {144.60f, 105.50f}, {156.80f, 105.50f}, {169.00f, 105.50f}, {181.20f, 105.50f}, {193.40f, 105.50f}};
inline constexpr float CV_OUT[8][2] = {{108.00f, 119.50f}, {120.20f, 119.50f}, {132.40f, 119.50f}, {144.60f, 119.50f}, {156.80f, 119.50f}, {169.00f, 119.50f}, {181.20f, 119.50f}, {193.40f, 119.50f}};

}
