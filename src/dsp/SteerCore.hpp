// STEER (spec §13): the parts that need no Rack: response curves for macro
// mappings, the version tree and its walk, and recorded macro gestures.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "common/Rng.hpp"

namespace codex {

enum SteerCurve { STEER_LINEAR = 0, STEER_EXP, STEER_LOG, STEER_S, STEER_STEP, STEER_CURVES };
// Response curve for a mapping or a morph: x and the result are 0..1, with 0 -> 0 and 1 -> 1.
// steps is used by STEER_STEP only (2..32 levels).
float steerCurve(int type, float x, int steps = 4);
const char* steerCurveName(int type);

struct SteerParamValue {
	int64_t module = -1;
	int param = 0;
	float value = 0.f;
	bool snap = false; // switches and stepped knobs: these jump half way through a morph
};

struct SteerState {
	int64_t module = -1;
	std::string json; // a generator's captured state, as JSON text
};

struct SteerNode {
	int id = 0;
	int parent = -1; // -1 = a root
	std::string name;
	std::vector<SteerParamValue> params;
	std::vector<SteerState> states;
};

struct SteerPlace {
	int id = 0;
	int depth = 0; // steps from its root
	float row = 0.f;
};

struct SteerTree {
	std::vector<SteerNode> nodes;
	int nextId = 1;
	int current = -1, target = -1;

	void clear();
	// A new empty node under `parent` (-1 = a new root). Returns its id.
	int add(int parent);
	SteerNode* find(int id);
	const SteerNode* find(int id) const;
	// False (and nothing changes) if that would make a loop.
	bool setParent(int id, int parent);
	// Takes a node out; its children move up to its parent.
	void remove(int id);
	// Parent first, then children in the order they were made.
	std::vector<int> neighbours(int id) const;
	// Where to draw each node: children to the right of their parent, leaves on rows of their own.
	std::vector<SteerPlace> layout() const;

	std::string toJson() const;
	bool fromJson(const std::string& text);
};

bool operator==(const SteerTree& a, const SteerTree& b);

// One turn of WALK: with chance `prob` move from `from` to one of its neighbours, else stay.
// Returns the node to be at next. Always draws the same count of numbers from rng.
int steerWalk(const SteerTree& tree, int from, float prob, Rng& rng);

// A recorded movement of one macro, against bar position.
struct SteerGesture {
	static constexpr int MAX = 8192;
	std::vector<double> pos;
	std::vector<float> val;
	int count = 0;
	double bars = 0.0; // 0 = nothing recorded

	SteerGesture();
	void clear();
	// While recording. p is bars since recording started. Points closer than 1/384 bar are merged.
	void add(double p, float v);
	// Recording ended after `length` bars: the gesture loops over that, rounded up to whole bars (1..16).
	void finish(double length);
	bool empty() const { return count == 0 || bars <= 0.0; }
	// The value at bar position p (wrapped into the gesture).
	float valueAt(double p) const;
	// LOOSE: a smoothly wandering shift of the play position, up to ±1/16 bar, fixed by the seed.
	static double loosen(double p, float loose, int seed);
};

} // namespace codex
