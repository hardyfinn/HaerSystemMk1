// Written by res/make_panels.py. Do not edit: change the script and run it again.
// Positions are in millimetres from the panel's top left corner.
#pragma once

namespace layout_depth {

inline constexpr float HP = 24;
inline constexpr float DIST[4][2] = {{30.00f, 31.00f}, {45.00f, 31.00f}, {60.00f, 31.00f}, {75.00f, 31.00f}};
inline constexpr float PAN[4][2] = {{30.00f, 48.00f}, {45.00f, 48.00f}, {60.00f, 48.00f}, {75.00f, 48.00f}};
inline constexpr float DIST_IN[4][2] = {{30.00f, 62.50f}, {45.00f, 62.50f}, {60.00f, 62.50f}, {75.00f, 62.50f}};
inline constexpr float L_IN[4][2] = {{30.00f, 76.00f}, {45.00f, 76.00f}, {60.00f, 76.00f}, {75.00f, 76.00f}};
inline constexpr float R_IN[4][2] = {{30.00f, 89.50f}, {45.00f, 89.50f}, {60.00f, 89.50f}, {75.00f, 89.50f}};
inline constexpr float SIZE[2] = {96.00f, 31.00f};
inline constexpr float DECAY[2] = {112.50f, 31.00f};
inline constexpr float DAMP[2] = {96.00f, 52.00f};
inline constexpr float MOD[2] = {112.50f, 52.00f};
inline constexpr float INSTABILITY[2] = {96.00f, 73.00f};
inline constexpr float MIX[2] = {112.50f, 73.00f};
inline constexpr float SIZE_IN[2] = {11.00f, 104.50f};
inline constexpr float DECAY_IN[2] = {25.50f, 104.50f};
inline constexpr float INSTABILITY_IN[2] = {40.00f, 104.50f};
inline constexpr float MOD_IN[2] = {54.50f, 104.50f};
inline constexpr float FREEZE_IN[2] = {69.00f, 104.50f};
inline constexpr float L_OUT[2] = {69.00f, 119.50f};
inline constexpr float R_OUT[2] = {83.50f, 119.50f};
inline constexpr float WET_L_OUT[2] = {98.00f, 119.50f};
inline constexpr float WET_R_OUT[2] = {112.50f, 119.50f};

}
