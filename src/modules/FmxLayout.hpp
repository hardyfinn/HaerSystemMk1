// Written by res/make_panels.py. Do not edit: change the script and run it again.
// Positions are in millimetres from the panel's top left corner.
#pragma once

namespace layout_fmx {

inline constexpr float HP = 24;
inline constexpr float DISPLAY[2][2] = {{5.00f, 15.00f}, {40.00f, 40.00f}};
inline constexpr float RATIO[4][2] = {{72.00f, 25.50f}, {86.00f, 25.50f}, {100.00f, 25.50f}, {114.00f, 25.50f}};
inline constexpr float FINE[4][2] = {{72.00f, 36.00f}, {86.00f, 36.00f}, {100.00f, 36.00f}, {114.00f, 36.00f}};
inline constexpr float LEVEL[4][2] = {{72.00f, 46.50f}, {86.00f, 46.50f}, {100.00f, 46.50f}, {114.00f, 46.50f}};
inline constexpr float ENV[4][2] = {{72.00f, 57.00f}, {86.00f, 57.00f}, {100.00f, 57.00f}, {114.00f, 57.00f}};
inline constexpr float ALGO[2] = {11.00f, 77.00f};
inline constexpr float INDEX[2] = {27.65f, 77.00f};
inline constexpr float FEEDBACK[2] = {44.30f, 77.00f};
inline constexpr float TIME[2] = {60.95f, 77.00f};
inline constexpr float AMP_SHAPE[2] = {77.60f, 77.00f};
inline constexpr float INDEX_SHAPE[2] = {94.25f, 77.00f};
inline constexpr float SPREAD[2] = {110.90f, 77.00f};
inline constexpr float VOCT_IN[2] = {10.00f, 104.50f};
inline constexpr float TRIG_IN[2] = {21.32f, 104.50f};
inline constexpr float PHASE_IN[2] = {32.64f, 104.50f};
inline constexpr float VEL_IN[2] = {43.96f, 104.50f};
inline constexpr float INDEX_IN[2] = {55.28f, 104.50f};
inline constexpr float RATIO2_IN[2] = {66.60f, 104.50f};
inline constexpr float RATIO3_IN[2] = {77.92f, 104.50f};
inline constexpr float RATIO4_IN[2] = {89.24f, 104.50f};
inline constexpr float FEEDBACK_IN[2] = {100.56f, 104.50f};
inline constexpr float ALGO_IN[2] = {111.88f, 104.50f};
inline constexpr float L_OUT[2] = {11.00f, 119.50f};
inline constexpr float R_OUT[2] = {27.65f, 119.50f};
inline constexpr float POLY_OUT[2] = {44.30f, 119.50f};
inline constexpr float OP_OUT[4][2] = {{60.95f, 119.50f}, {77.60f, 119.50f}, {94.25f, 119.50f}, {110.90f, 119.50f}};

}
