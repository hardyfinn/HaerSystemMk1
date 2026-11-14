// The KEEP button, RECALL knob and 8 slot lights shared by every generator
// (spec §3.5). The audio thread only raises flags; capturing and recalling
// happen on the UI thread, in the module widget's step().
#pragma once
#include <rack.hpp>
#include <atomic>
#include "common/KeepProtocol.hpp"

namespace codex {

struct KeepHost {
	KeepSlots slots; // UI thread only
	std::atomic<bool> keepPending{false};
	std::atomic<int> recallPending{0}; // slot 1..8 waiting to be recalled, 0 = none
	std::atomic<int> fullMask{0};      // bit i set = slot i+1 holds something
	std::atomic<int> lastKept{-1};
	rack::dsp::SchmittTrigger keepIn, recallIn;
	rack::dsp::BooleanTrigger keepButton;

	// Audio thread. recallKnob is 0..8, where 0 means "stay live".
	void process(bool keepButtonDown, float keepVolts, float recallKnob, float recallVolts) {
		bool pressed = keepButton.process(keepButtonDown);
		bool triggered = keepIn.process(keepVolts, 0.1f, 1.f);
		if (pressed || triggered)
			keepPending = true;
		if (recallIn.process(recallVolts, 0.1f, 1.f)) {
			int slot = (int)std::lround(recallKnob);
			if (slot >= 1 && slot <= KeepSlots::SLOTS)
				recallPending = slot;
		}
	}
	// Audio thread: brightness for slot light i.
	float light(int i) const { return (fullMask.load(std::memory_order_relaxed) >> i) & 1 ? 1.f : 0.f; }

	void refreshMask() {
		int m = 0;
		for (int i = 0; i < KeepSlots::SLOTS; i++)
			if (slots.isFull(i))
				m |= 1 << i;
		fullMask = m;
	}
	// UI thread, every frame.
	void step(Keepable* owner) {
		if (keepPending.exchange(false)) {
			lastKept = slots.keep(owner->captureState());
			refreshMask();
		}
		int r = recallPending.exchange(0);
		if (r >= 1 && slots.isFull(r - 1))
			owner->requestRestore(slots.copyOf(r - 1));
	}
	void recallNow(Keepable* owner, int slot) {
		if (slots.isFull(slot))
			owner->requestRestore(slots.copyOf(slot));
	}
	void clearAll() {
		slots.clear();
		refreshMask();
	}
	json_t* toJson() const { return slots.toJson(); }
	void fromJson(json_t* j) {
		slots.fromJson(j);
		refreshMask();
	}

	// Right-click menu entries shared by all generators.
	void appendMenu(rack::ui::Menu* menu, Keepable* owner) {
		menu->addChild(new rack::ui::MenuSeparator);
		menu->addChild(rack::createMenuLabel("Keep slots"));
		menu->addChild(rack::createSubmenuItem("Recall a kept slot now", "", [=](rack::ui::Menu* sub) {
			for (int i = 0; i < KeepSlots::SLOTS; i++) {
				sub->addChild(rack::createMenuItem(rack::string::f("Slot %d", i + 1), slots.isFull(i) ? "" : "empty",
					[=]() { recallNow(owner, i); }, !slots.isFull(i)));
			}
		}));
		menu->addChild(rack::createMenuItem("Empty all 8 slots", "", [=]() { clearAll(); }));
	}
};

} // namespace codex
