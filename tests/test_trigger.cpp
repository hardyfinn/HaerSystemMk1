#include "doctest.h"
#include "common/Trigger.hpp"
#include "common/StateSwap.hpp"

using namespace codex;

TEST_CASE("SchmittIn: rises at 1 V, re-arms at 0.1 V") {
	SchmittIn in;
	CHECK_FALSE(in.process(0, 0.5f));
	CHECK(in.process(0, 1.f));
	CHECK_FALSE(in.process(0, 10.f)); // still high: no second trigger
	CHECK_FALSE(in.process(0, 0.5f)); // not low enough to re-arm
	CHECK_FALSE(in.process(0, 5.f));
	CHECK_FALSE(in.process(0, 0.05f)); // re-armed
	CHECK(in.process(0, 5.f));
	CHECK(in.process(15, 2.f)); // channels are independent
}

TEST_CASE("TriggerOut: a 10 V pulse of 1 ms") {
	TriggerOut out;
	const float st = 1.f / 48000.f;
	out.fire(3);
	int high = 0;
	for (int i = 0; i < 200; i++)
		if (out.process(3, st) > 5.f)
			high++;
	CHECK(high >= 48);
	CHECK(high <= 49);
	CHECK(out.process(0, st) == 0.f);
}

TEST_CASE("StateSwap: the audio side picks up an offer and hands the old state back") {
	static int alive = 0;
	struct S {
		int v;
		explicit S(int v) : v(v) { alive++; }
		~S() { alive--; }
	};
	{
		StateSwap<S> swap;
		S* current = new S(1);
		CHECK(swap.take(current) == current); // nothing offered
		swap.offer(new S(2));
		S* next = swap.take(current);
		CHECK(next->v == 2);
		current = next;
		swap.offer(new S(3)); // also frees the retired S(1)
		CHECK(alive == 2);
		swap.offer(new S(4)); // replaces the offer that was never taken
		CHECK(alive == 2);
		current = swap.take(current);
		CHECK(current->v == 4);
		swap.collect();
		CHECK(alive == 1);
		delete current;
	}
	CHECK(alive == 0);
}

TEST_CASE("TryLock") {
	TryLock l;
	CHECK(l.tryLock());
	CHECK_FALSE(l.tryLock());
	l.unlock();
	CHECK(l.tryLock());
	l.unlock();
}
