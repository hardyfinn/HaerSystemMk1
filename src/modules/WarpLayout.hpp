// Written by res/make_panels.py. Do not edit: change the script and run it again.
// Positions are in millimetres from the panel's top left corner.
#pragma once

namespace layout_warp {

inline constexpr float HP = 24;
inline constexpr float DISPLAY[2][2] = {{5.00f, 15.00f}, {50.80f, 40.00f}};
inline constexpr float RATIO[4][2] = {{76.00f, 26.00f}, {76.00f, 35.00f}, {76.00f, 44.00f}, {76.00f, 53.00f}};
inline constexpr float OFFSET[4][2] = {{94.00f, 26.00f}, {94.00f, 35.00f}, {94.00f, 44.00f}, {94.00f, 53.00f}};
inline constexpr float FOLLOW[4][2] = {{112.00f, 26.00f}, {112.00f, 35.00f}, {112.00f, 44.00f}, {112.00f, 53.00f}};
inline constexpr float BPM[2] = {10.00f, 72.00f};
inline constexpr float BEATS[2] = {26.99f, 72.00f};
inline constexpr float DRIFT[2] = {43.98f, 72.00f};
inline constexpr float PERIOD[2] = {60.97f, 72.00f};
inline constexpr float SHAPE[2] = {77.96f, 72.00f};
inline constexpr float DEPTH[2] = {94.95f, 72.00f};
inline constexpr float SEED[2] = {111.94f, 72.00f};
inline constexpr float RUN[2] = {10.00f, 89.00f};
inline constexpr float RESET[2] = {24.56f, 89.00f};
inline constexpr float KEEP[2] = {39.12f, 89.00f};
inline constexpr float RECALL[2] = {53.68f, 89.00f};
inline constexpr float SLOT_LIGHTS[8][2] = {{68.00f, 89.00f}, {72.00f, 89.00f}, {76.00f, 89.00f}, {80.00f, 89.00f}, {84.00f, 89.00f}, {88.00f, 89.00f}, {92.00f, 89.00f}, {96.00f, 89.00f}};
inline constexpr float RUN_IN[2] = {10.00f, 104.50f};
inline constexpr float RESET_IN[2] = {24.56f, 104.50f};
inline constexpr float KEEP_IN[2] = {39.12f, 104.50f};
inline constexpr float RECALL_IN[2] = {53.68f, 104.50f};
inline constexpr float TEMPO_IN[2] = {70.00f, 104.50f};
inline constexpr float SHAPE_IN[2] = {84.50f, 104.50f};
inline constexpr float DEPTH_IN[2] = {99.00f, 104.50f};
inline constexpr float SEED_IN[2] = {113.50f, 104.50f};
inline constexpr float PHASE_OUT_1[2] = {10.00f, 119.50f};
inline constexpr float PHASE_OUT_2[2] = {24.56f, 119.50f};
inline constexpr float PHASE_OUT_3[2] = {39.12f, 119.50f};
inline constexpr float PHASE_OUT_4[2] = {53.68f, 119.50f};
inline constexpr float POLY_OUT[2] = {68.24f, 119.50f};
inline constexpr float BAR_OUT[2] = {82.80f, 119.50f};
inline constexpr float BEAT_OUT[2] = {97.36f, 119.50f};
inline constexpr float TEMPO_OUT[2] = {111.92f, 119.50f};

}
