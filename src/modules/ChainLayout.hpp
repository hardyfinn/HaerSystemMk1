// Written by res/make_panels.py. Do not edit: change the script and run it again.
// Positions are in millimetres from the panel's top left corner.
#pragma once

namespace layout_chain {

inline constexpr float HP = 34;
inline constexpr float DISPLAY[2][2] = {{5.00f, 15.00f}, {80.00f, 40.00f}};
inline constexpr float CHARACTER[4][2] = {{118.00f, 27.00f}, {132.00f, 27.00f}, {146.00f, 27.00f}, {160.00f, 27.00f}};
inline constexpr float RANGE[4][2] = {{118.00f, 38.00f}, {132.00f, 38.00f}, {146.00f, 38.00f}, {160.00f, 38.00f}};
inline constexpr float CENTER[4][2] = {{118.00f, 49.00f}, {132.00f, 49.00f}, {146.00f, 49.00f}, {160.00f, 49.00f}};
inline constexpr float SCALE[2] = {12.00f, 72.00f};
inline constexpr float ROOT[2] = {30.00f, 72.00f};
inline constexpr float ORDER[2] = {46.00f, 72.00f};
inline constexpr float GLIDE[2] = {80.00f, 72.00f};
inline constexpr float SEED[2] = {97.00f, 72.00f};
inline constexpr float KEEP[2] = {113.00f, 72.00f};
inline constexpr float RECALL[2] = {127.00f, 72.00f};
inline constexpr float SLOT_LIGHTS[8][2] = {{139.00f, 72.00f}, {143.00f, 72.00f}, {147.00f, 72.00f}, {151.00f, 72.00f}, {155.00f, 72.00f}, {159.00f, 72.00f}, {163.00f, 72.00f}, {167.00f, 72.00f}};
inline constexpr float RULE_SCALE[2] = {12.00f, 89.00f};
inline constexpr float RULE_PARALLEL[2] = {30.00f, 89.00f};
inline constexpr float RULE_INTERVAL[2] = {48.00f, 89.00f};
inline constexpr float RULE_CONTRARY[2] = {66.00f, 89.00f};
inline constexpr float RULE_LEAP[2] = {84.00f, 89.00f};
inline constexpr float LEAP[2] = {102.00f, 89.00f};
inline constexpr float RULE_HOME[2] = {120.00f, 89.00f};
inline constexpr float HOME_STEPS[2] = {138.00f, 89.00f};
inline constexpr float STEP_IN[4][2] = {{10.00f, 104.50f}, {22.73f, 104.50f}, {35.46f, 104.50f}, {48.19f, 104.50f}};
inline constexpr float SCALE_IN[2] = {60.92f, 104.50f};
inline constexpr float ROOT_IN[2] = {73.65f, 104.50f};
inline constexpr float LEARN_IN[2] = {86.38f, 104.50f};
inline constexpr float LEARN_PITCH_IN[2] = {99.11f, 104.50f};
inline constexpr float CHARACTER_IN[2] = {111.84f, 104.50f};
inline constexpr float RESET_IN[2] = {124.57f, 104.50f};
inline constexpr float SEED_IN[2] = {137.30f, 104.50f};
inline constexpr float KEEP_IN[2] = {150.03f, 104.50f};
inline constexpr float RECALL_IN[2] = {162.76f, 104.50f};
inline constexpr float PITCH_OUT[4][2] = {{11.00f, 119.50f}, {27.75f, 119.50f}, {44.50f, 119.50f}, {61.25f, 119.50f}};
inline constexpr float GATE_OUT[4][2] = {{94.75f, 119.50f}, {111.50f, 119.50f}, {128.25f, 119.50f}, {145.00f, 119.50f}};
inline constexpr float POLY_OUT[2] = {78.00f, 119.50f};
inline constexpr float VIOLATION_OUT[2] = {161.75f, 119.50f};

}
