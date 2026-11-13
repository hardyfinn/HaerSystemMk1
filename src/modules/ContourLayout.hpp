// Written by res/make_panels.py. Do not edit: change the script and run it again.
// Positions are in millimetres from the panel's top left corner.
#pragma once

namespace layout_contour {

inline constexpr float HP = 28;
inline constexpr float DISPLAY[2][2] = {{5.00f, 15.00f}, {70.00f, 40.00f}};
inline constexpr float AMP[4][2] = {{99.00f, 27.00f}, {111.00f, 27.00f}, {123.00f, 27.00f}, {135.00f, 27.00f}};
inline constexpr float RATIO[4][2] = {{99.00f, 38.00f}, {111.00f, 38.00f}, {123.00f, 38.00f}, {135.00f, 38.00f}};
inline constexpr float THETA[4][2] = {{99.00f, 49.00f}, {111.00f, 49.00f}, {123.00f, 49.00f}, {135.00f, 49.00f}};
inline constexpr float MODE[2] = {8.50f, 72.00f};
inline constexpr float RATE[2] = {31.00f, 72.00f};
inline constexpr float MORPH[2] = {47.50f, 72.00f};
inline constexpr float THRESH[2] = {64.00f, 72.00f};
inline constexpr float BUNCH[2] = {80.50f, 72.00f};
inline constexpr float HYST[2] = {97.00f, 72.00f};
inline constexpr float CHANS[2] = {112.00f, 72.00f};
inline constexpr float DIR[2] = {124.50f, 72.00f};
inline constexpr float SPREAD[2] = {14.00f, 89.00f};
inline constexpr float COINC[2] = {31.00f, 89.00f};
inline constexpr float WINDOW[2] = {48.00f, 89.00f};
inline constexpr float SEED[2] = {65.00f, 89.00f};
inline constexpr float KEEP[2] = {82.00f, 89.00f};
inline constexpr float RECALL[2] = {96.00f, 89.00f};
inline constexpr float SLOT_LIGHTS[8][2] = {{109.00f, 89.00f}, {113.00f, 89.00f}, {117.00f, 89.00f}, {121.00f, 89.00f}, {125.00f, 89.00f}, {129.00f, 89.00f}, {133.00f, 89.00f}, {137.00f, 89.00f}};
inline constexpr float PHASE_IN[2] = {10.00f, 104.50f};
inline constexpr float EXT_IN[2] = {25.28f, 104.50f};
inline constexpr float RATE_IN[2] = {40.56f, 104.50f};
inline constexpr float MORPH_IN[2] = {55.84f, 104.50f};
inline constexpr float DENS_IN[2] = {71.12f, 104.50f};
inline constexpr float SPREAD_IN[2] = {86.40f, 104.50f};
inline constexpr float SEED_IN[2] = {101.68f, 104.50f};
inline constexpr float KEEP_IN[2] = {116.96f, 104.50f};
inline constexpr float RECALL_IN[2] = {132.24f, 104.50f};
inline constexpr float TRIG_OUT[2] = {12.00f, 119.50f};
inline constexpr float VEL_OUT[2] = {31.70f, 119.50f};
inline constexpr float CURVE_OUT[2] = {51.40f, 119.50f};
inline constexpr float SLOPE_OUT[2] = {71.10f, 119.50f};
inline constexpr float COINC_OUT[2] = {90.80f, 119.50f};
inline constexpr float COUNT_OUT[2] = {110.50f, 119.50f};
inline constexpr float PHASORS_OUT[2] = {130.20f, 119.50f};

}
