// Lock-free hand-over of prepared state between the UI thread and the audio
// thread (spec §3.5). Plain C++ so it can be unit tested.
#pragma once
#include <atomic>

namespace codex {

// The UI thread builds a new T and offers it. The audio thread picks it up at
// a musical boundary and hands the old one back. Memory is only ever
// allocated and freed on the UI thread.
template <typename T>
struct StateSwap {
	std::atomic<T*> incoming{nullptr}; // offered by UI, not yet taken
	std::atomic<T*> outgoing{nullptr}; // retired by audio, waiting to be freed

	~StateSwap() {
		delete incoming.exchange(nullptr);
		delete outgoing.exchange(nullptr);
	}

	// UI thread. Takes ownership of state. An offer that was never picked up is replaced.
	void offer(T* state) {
		delete incoming.exchange(state);
		collect();
	}
	// UI thread. Frees whatever the audio thread retired. Call now and then (e.g. in a widget's step()).
	void collect() { delete outgoing.exchange(nullptr); }

	bool hasOffer() const { return incoming.load(std::memory_order_relaxed) != nullptr; }

	// Audio thread, at a step or bar boundary. `current` is the pointer the
	// audio thread is using. Returns the pointer to use from now on.
	// Never allocates, frees or blocks.
	T* take(T* current) {
		if (!hasOffer())
			return current;
		// Only swap when the retire slot is empty, so nothing is ever leaked or freed here.
		if (outgoing.load(std::memory_order_relaxed) != nullptr)
			return current;
		T* next = incoming.exchange(nullptr);
		if (!next)
			return current;
		outgoing.store(current);
		return next;
	}
};

// A tiny lock the audio thread only ever *tries* to take. If the UI thread is
// reading, the audio thread skips its copy this time and tries again later.
struct TryLock {
	std::atomic_flag flag = ATOMIC_FLAG_INIT;
	bool tryLock() { return !flag.test_and_set(std::memory_order_acquire); }
	void lock() {
		while (flag.test_and_set(std::memory_order_acquire)) {
		}
	}
	void unlock() { flag.clear(std::memory_order_release); }
};

} // namespace codex
