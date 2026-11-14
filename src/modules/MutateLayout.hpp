// Written by res/make_panels.py. Do not edit: change the script and run it again.
// Positions are in millimetres from the panel's top left corner.
#pragma once

namespace layout_mutate {

inline constexpr float HP = 38;
inline constexpr float DISPLAY[2][2] = {{5.00f, 15.00f}, {183.04f, 36.00f}};
inline constexpr float STEPS[2] = {11.50f, 68.00f};
inline constexpr float DENSITY[2] = {26.95f, 68.00f};
inline constexpr float SEED[2] = {42.40f, 68.00f};
inline constexpr float GENERATE[2] = {57.85f, 68.00f};
inline constexpr float EVERY[2] = {73.30f, 68.00f};
inline constexpr float AMOUNT[2] = {88.75f, 68.00f};
inline constexpr float DRIFT[2] = {104.20f, 68.00f};
inline constexpr float HOME[2] = {119.65f, 68.00f};
inline constexpr float HOME_SET[2] = {135.10f, 68.00f};
inline constexpr float CURVE[2] = {150.55f, 68.00f};
inline constexpr float DECAY[2] = {166.00f, 68.00f};
inline constexpr float SWING[2] = {181.45f, 68.00f};
inline constexpr float RULE[7][2] = {{11.50f, 86.00f}, {26.95f, 86.00f}, {42.40f, 86.00f}, {57.85f, 86.00f}, {73.30f, 86.00f}, {88.75f, 86.00f}, {104.20f, 86.00f}};
inline constexpr float KEEP[2] = {127.00f, 86.00f};
inline constexpr float RECALL[2] = {142.50f, 86.00f};
inline constexpr float SLOT_LIGHTS[8][2] = {{156.00f, 86.00f}, {160.00f, 86.00f}, {164.00f, 86.00f}, {168.00f, 86.00f}, {172.00f, 86.00f}, {176.00f, 86.00f}, {180.00f, 86.00f}, {184.00f, 86.00f}};
inline constexpr float PHASE_IN[2] = {9.50f, 104.50f};
inline constexpr float CLOCK_IN[2] = {24.00f, 104.50f};
inline constexpr float RESET_IN[2] = {38.50f, 104.50f};
inline constexpr float GENERATE_IN[2] = {53.00f, 104.50f};
inline constexpr float MUTATE_IN[2] = {67.50f, 104.50f};
inline constexpr float HOME_IN[2] = {82.00f, 104.50f};
inline constexpr float DENSITY_IN[2] = {96.50f, 104.50f};
inline constexpr float AMOUNT_IN[2] = {111.00f, 104.50f};
inline constexpr float DRIFT_IN[2] = {125.50f, 104.50f};
inline constexpr float MICRO_IN[2] = {140.00f, 104.50f};
inline constexpr float SEED_IN[2] = {154.50f, 104.50f};
inline constexpr float KEEP_IN[2] = {169.00f, 104.50f};
inline constexpr float RECALL_IN[2] = {183.50f, 104.50f};
inline constexpr float GATE_OUT[8][2] = {{9.50f, 119.50f}, {24.00f, 119.50f}, {38.50f, 119.50f}, {53.00f, 119.50f}, {67.50f, 119.50f}, {82.00f, 119.50f}, {96.50f, 119.50f}, {111.00f, 119.50f}};
inline constexpr float GATES_OUT[2] = {125.50f, 119.50f};
inline constexpr float VEL_OUT[2] = {140.00f, 119.50f};
inline constexpr float LOCK_A_OUT[2] = {154.50f, 119.50f};
inline constexpr float LOCK_B_OUT[2] = {169.00f, 119.50f};
inline constexpr float ACCENT_OUT[2] = {183.50f, 119.50f};

}
