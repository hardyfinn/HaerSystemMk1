// KEEP protocol (spec §3.5): how generators capture and restore their musical
// state, so that "generate first, keep, perform later" works.
// Needs Rack's JSON library, so it is not part of the unit tests; the
// thread-safe hand-over it relies on is in StateSwap.hpp and is tested.
#pragma once
#include <jansson.h>
#include <string>

namespace codex {

// Every generator (WARP, MUTATE, CONTOUR, CHAIN, FLOW) and VAULT is Keepable.
// STEER and VAULT find them with dynamic_cast<Keepable*>(module), on the UI thread.
struct Keepable {
	virtual ~Keepable() {}
	// Full musical state: patterns, matrices, seeds, positions.
	// UI thread only. The caller owns the returned JSON.
	virtual json_t* captureState() = 0;
	// UI thread only. Takes ownership of state. The module applies it at its next step or bar.
	virtual void requestRestore(json_t* state) = 0;
	// For menus and displays, e.g. "MUTATE #3".
	virtual std::string keepLabel() = 0;
};

// The 8 keep slots each generator owns. They are saved in the patch.
// All methods are for the UI thread.
struct KeepSlots {
	static const int SLOTS = 8;
	json_t* slot[SLOTS] = {};

	~KeepSlots() { clear(); }

	void clear() {
		for (int i = 0; i < SLOTS; i++) {
			if (slot[i])
				json_decref(slot[i]);
			slot[i] = nullptr;
		}
	}
	bool isFull(int i) const { return i >= 0 && i < SLOTS && slot[i]; }
	int firstFree() const {
		for (int i = 0; i < SLOTS; i++)
			if (!slot[i])
				return i;
		return -1;
	}
	// Stores state (takes ownership) in the first free slot. When all 8 are
	// full the oldest (slot 1) is dropped and the others move down.
	// Returns the slot used, 0..7.
	int keep(json_t* state) {
		int i = firstFree();
		if (i < 0) {
			json_decref(slot[0]);
			for (int k = 0; k < SLOTS - 1; k++)
				slot[k] = slot[k + 1];
			i = SLOTS - 1;
		}
		slot[i] = state;
		return i;
	}
	void erase(int i) {
		if (isFull(i)) {
			json_decref(slot[i]);
			slot[i] = nullptr;
		}
	}
	// A copy of slot i for requestRestore(), or NULL when the slot is empty.
	json_t* copyOf(int i) const { return isFull(i) ? json_deep_copy(slot[i]) : nullptr; }

	json_t* toJson() const {
		json_t* arr = json_array();
		for (int i = 0; i < SLOTS; i++)
			json_array_append_new(arr, slot[i] ? json_deep_copy(slot[i]) : json_null());
		return arr;
	}
	void fromJson(json_t* arr) {
		clear();
		if (!arr || !json_is_array(arr))
			return;
		for (int i = 0; i < SLOTS; i++) {
			json_t* s = json_array_get(arr, i);
			if (s && !json_is_null(s))
				slot[i] = json_deep_copy(s);
		}
	}
};

} // namespace codex
