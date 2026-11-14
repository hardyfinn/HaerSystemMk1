// Written by res/make_panels.py. Do not edit: change the script and run it again.
// Positions are in millimetres from the panel's top left corner.
#pragma once

namespace layout_tunemap {

inline constexpr float HP = 18;
inline constexpr float DISPLAY[2][2] = {{5.00f, 15.00f}, {47.00f, 47.00f}};
inline constexpr float FIELD_A[2] = {64.00f, 27.00f};
inline constexpr float FIELD_B[2] = {80.00f, 27.00f};
inline constexpr float MORPH[2] = {64.00f, 47.00f};
inline constexpr float HYST[2] = {80.00f, 47.00f};
inline constexpr float STRENGTH[2] = {72.00f, 67.00f};
inline constexpr float IN_IN[2] = {11.00f, 89.00f};
inline constexpr float ROOT_IN[2] = {28.00f, 89.00f};
inline constexpr float MORPH_IN[2] = {45.50f, 89.00f};
inline constexpr float FIELD_A_IN[2] = {63.00f, 89.00f};
inline constexpr float FIELD_B_IN[2] = {80.50f, 89.00f};
inline constexpr float AUDIO_IN[2] = {11.00f, 104.50f};
inline constexpr float LEARN_IN[2] = {28.00f, 104.50f};
inline constexpr float LEARN[2] = {39.00f, 104.50f};
inline constexpr float LEARN_LIGHT[2] = {46.00f, 104.50f};
inline constexpr float OUT_OUT[2] = {15.00f, 119.50f};
inline constexpr float CHANGED_OUT[2] = {45.70f, 119.50f};
inline constexpr float FIELD_OUT[2] = {76.40f, 119.50f};

}
