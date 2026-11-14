// Written by res/make_panels.py. Do not edit: change the script and run it again.
// Positions are in millimetres from the panel's top left corner.
#pragma once

namespace layout_flow {

inline constexpr float HP = 30;
inline constexpr float DISPLAY[2][2] = {{5.00f, 15.00f}, {52.00f, 52.00f}};
inline constexpr float X_IN[4][2] = {{76.00f, 28.50f}, {76.00f, 40.00f}, {76.00f, 51.50f}, {76.00f, 63.00f}};
inline constexpr float Y_IN[4][2] = {{89.60f, 28.50f}, {89.60f, 40.00f}, {89.60f, 51.50f}, {89.60f, 63.00f}};
inline constexpr float ANG_IN[4][2] = {{103.20f, 28.50f}, {103.20f, 40.00f}, {103.20f, 51.50f}, {103.20f, 63.00f}};
inline constexpr float FORCE_IN[4][2] = {{116.80f, 28.50f}, {116.80f, 40.00f}, {116.80f, 51.50f}, {116.80f, 63.00f}};
inline constexpr float DYE_IN[4][2] = {{130.40f, 28.50f}, {130.40f, 40.00f}, {130.40f, 51.50f}, {130.40f, 63.00f}};
inline constexpr float PUFF_IN[4][2] = {{144.00f, 28.50f}, {144.00f, 40.00f}, {144.00f, 51.50f}, {144.00f, 63.00f}};
inline constexpr float VISC[2] = {11.00f, 83.00f};
inline constexpr float DIFF[2] = {27.30f, 83.00f};
inline constexpr float VORT[2] = {43.60f, 83.00f};
inline constexpr float DECAY[2] = {59.90f, 83.00f};
inline constexpr float SPEED[2] = {76.20f, 83.00f};
inline constexpr float RATE[2] = {92.50f, 83.00f};
inline constexpr float GAIN[2] = {108.80f, 83.00f};
inline constexpr float THRESH[2] = {125.10f, 83.00f};
inline constexpr float SEED[2] = {141.40f, 83.00f};
inline constexpr float PROBE_X_IN[2] = {11.00f, 104.50f};
inline constexpr float PROBE_Y_IN[2] = {28.00f, 104.50f};
inline constexpr float KEEP_IN[2] = {47.00f, 104.50f};
inline constexpr float RECALL_IN[2] = {75.00f, 104.50f};
inline constexpr float KEEP[2] = {57.50f, 104.50f};
inline constexpr float RECALL[2] = {86.50f, 104.50f};
inline constexpr float SLOT_LIGHTS[8][2] = {{104.00f, 104.00f}, {108.00f, 104.00f}, {112.00f, 104.00f}, {116.00f, 104.00f}, {120.00f, 104.00f}, {124.00f, 104.00f}, {128.00f, 104.00f}, {132.00f, 104.00f}};
inline constexpr float DENS_OUT[2] = {14.00f, 119.50f};
inline constexpr float U_OUT[2] = {38.90f, 119.50f};
inline constexpr float V_OUT[2] = {63.80f, 119.50f};
inline constexpr float CURL_OUT[2] = {88.70f, 119.50f};
inline constexpr float ENERGY_OUT[2] = {113.60f, 119.50f};
inline constexpr float PEAK_OUT[2] = {138.50f, 119.50f};

}
