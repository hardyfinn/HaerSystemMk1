#include "doctest.h"
#include "dsp/SteerCore.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

using namespace codex;

TEST_CASE("STEER: every curve rises all the way and hits 0 and 1 at the ends") {
	for (int type = 0; type < STEER_CURVES; type++) {
		CAPTURE(type);
		CHECK(steerCurve(type, 0.f, 5) == doctest::Approx(0.f).epsilon(1e-6));
		CHECK(steerCurve(type, 1.f, 5) == doctest::Approx(1.f).epsilon(1e-6));
		float last = -1.f;
		bool rising = true, inside = true;
		for (int i = 0; i <= 1000; i++) {
			float y = steerCurve(type, (float)i / 1000.f, 5);
			rising = rising && y >= last;
			inside = inside && y >= 0.f && y <= 1.f;
			last = y;
		}
		CHECK(rising);
		CHECK(inside);
	}
	// the shapes are what their names say
	CHECK(steerCurve(STEER_EXP, 0.5f) < 0.3f);
	CHECK(steerCurve(STEER_LOG, 0.5f) > 0.7f);
	CHECK(steerCurve(STEER_S, 0.5f) == doctest::Approx(0.5f));
	CHECK(steerCurve(STEER_S, 0.25f) < 0.25f);
	// STEP with 5 steps gives exactly 5 levels
	int levels = 0;
	float prev = -1.f;
	for (int i = 0; i <= 1000; i++) {
		float y = steerCurve(STEER_STEP, (float)i / 1000.f, 5);
		if (y != prev)
			levels++;
		prev = y;
	}
	CHECK(levels == 5);
	// out of range input is held at the ends
	CHECK(steerCurve(STEER_LINEAR, -2.f) == 0.f);
	CHECK(steerCurve(STEER_LINEAR, 3.f) == 1.f);
}

static SteerTree sampleTree() {
	SteerTree t;
	int a = t.add(-1);
	int b = t.add(a);
	int c = t.add(a);
	int d = t.add(b);
	t.find(a)->name = "start \"here\"";
	t.find(b)->params = {{12345678901234LL, 3, 0.25f, false}, {7, 0, 1.f, true}, {7, 1, -3.3333333f, false}};
	t.find(c)->params = {{7, 0, 0.f, true}};
	t.find(d)->states.push_back({42, "{\"pattern\":[1,2,{\"x\":\"a,b]\"}],\"seed\":17}"});
	t.find(d)->states.push_back({43, ""});
	t.current = b;
	t.target = d;
	return t;
}

TEST_CASE("STEER: tree nodes can be added, moved and removed") {
	SteerTree t = sampleTree();
	CHECK(t.nodes.size() == 4);
	CHECK(t.find(2)->parent == 1);
	CHECK(t.neighbours(1) == std::vector<int>{2, 3});
	CHECK(t.neighbours(2) == std::vector<int>{1, 4});
	CHECK(t.neighbours(4) == std::vector<int>{2});
	// set a parent
	CHECK(t.setParent(4, 3));
	CHECK(t.find(4)->parent == 3);
	CHECK(t.neighbours(3) == std::vector<int>{1, 4});
	// a loop is refused
	CHECK(!t.setParent(1, 4));
	CHECK(!t.setParent(3, 3));
	CHECK(!t.setParent(3, 99));
	CHECK(t.find(1)->parent == -1);
	// removing a node hands its children to its parent
	t.remove(3);
	CHECK(t.find(3) == nullptr);
	CHECK(t.find(4)->parent == 1);
	CHECK(t.target == 4);
	t.remove(2);
	CHECK(t.current == 1);
	// layout: every node has a place, children one step to the right of their parent
	SteerTree u = sampleTree();
	std::vector<SteerPlace> places = u.layout();
	CHECK(places.size() == 4);
	for (const SteerPlace& p : places) {
		const SteerNode* n = u.find(p.id);
		REQUIRE(n);
		if (n->parent == -1)
			CHECK(p.depth == 0);
		else
			for (const SteerPlace& q : places)
				if (q.id == n->parent)
					CHECK(p.depth == q.depth + 1);
	}
}

TEST_CASE("STEER: the tree survives being written as JSON and read back") {
	SteerTree t = sampleTree();
	std::string text = t.toJson();
	SteerTree back;
	REQUIRE(back.fromJson(text));
	CHECK(back == t);
	CHECK(back.toJson() == text);
	CHECK(back.find(4)->states[0].json == "{\"pattern\":[1,2,{\"x\":\"a,b]\"}],\"seed\":17}");
	// a new node made afterwards does not reuse an id
	CHECK(back.add(4) == 5);
	// an empty tree
	SteerTree empty, e2;
	REQUIRE(e2.fromJson(empty.toJson()));
	CHECK(e2 == empty);
	// broken text is refused and changes nothing
	CHECK(!back.fromJson("{\"nodes\":[{\"id\":1,"));
	CHECK(back.nodes.size() == 5);
}

TEST_CASE("STEER: a walk with the same seed takes the same path, and only along the tree's edges") {
	SteerTree t = sampleTree();
	auto walk = [&](int seed, float prob) {
		Rng rng((uint64_t)seed);
		std::vector<int> path;
		int at = 1;
		for (int i = 0; i < 200; i++) {
			int next = steerWalk(t, at, prob, rng);
			if (next != at) {
				std::vector<int> near = t.neighbours(at);
				CHECK(std::find(near.begin(), near.end(), next) != near.end());
			}
			at = next;
			path.push_back(at);
		}
		return path;
	};
	std::vector<int> a = walk(5, 0.5f);
	CHECK(walk(5, 0.5f) == a);
	CHECK(walk(6, 0.5f) != a);
	// it does move, and it visits every node
	for (int id = 1; id <= 4; id++)
		CHECK(std::find(a.begin(), a.end(), id) != a.end());
	// WALK 0 never moves; WALK 1 always does
	std::vector<int> still = walk(5, 0.f);
	CHECK(std::count(still.begin(), still.end(), 1) == 200);
	std::vector<int> busy = walk(5, 1.f);
	for (size_t i = 1; i < busy.size(); i++)
		CHECK(busy[i] != busy[i - 1]);
}

TEST_CASE("STEER: a recorded gesture plays back what was recorded, and loops") {
	SteerGesture g;
	CHECK(g.empty());
	// a rise over 1.5 bars, 1000 points a bar
	for (int i = 0; i <= 1500; i++)
		g.add((double)i / 1000.0, (float)i / 1500.f);
	g.finish(1.5);
	CHECK(g.bars == 2.0);
	CHECK(g.count <= 1.5 * 384 + 2);
	CHECK(g.valueAt(0.0) == doctest::Approx(0.f).epsilon(0.01));
	CHECK(g.valueAt(0.75) == doctest::Approx(0.5f).epsilon(0.01));
	CHECK(g.valueAt(1.5) == doctest::Approx(1.f).epsilon(0.01));
	CHECK(g.valueAt(1.9) == doctest::Approx(1.f).epsilon(0.01)); // held to the end of the bar
	CHECK(g.valueAt(2.75) == doctest::Approx(0.5f).epsilon(0.01)); // second time round
	// more points than fit are ignored without harm
	SteerGesture big;
	for (int i = 0; i < 20000; i++)
		big.add((double)i / 300.0, 0.5f);
	CHECK(big.count == SteerGesture::MAX);
	big.finish(66.0);
	CHECK(big.bars == 16.0);
	big.clear();
	CHECK(big.empty());
}

TEST_CASE("STEER: LOOSE shifts the play position by at most 1/16 bar, smoothly, the same way for the same seed") {
	double worst = 0.0, jump = 0.0, last = 0.0;
	bool differs = false;
	for (int i = 0; i < 40000; i++) {
		double p = (double)i / 10000.0;
		double q = SteerGesture::loosen(p, 1.f, 3);
		CHECK(q == SteerGesture::loosen(p, 1.f, 3));
		differs = differs || q != SteerGesture::loosen(p, 1.f, 4);
		worst = std::fmax(worst, std::fabs(q - p));
		if (i)
			jump = std::fmax(jump, std::fabs((q - p) - last));
		last = q - p;
		CHECK(SteerGesture::loosen(p, 0.f, 3) == p);
	}
	CHECK(worst <= 1.0 / 16.0);
	CHECK(worst > 0.02);
	CHECK(jump < 0.001);
	CHECK(differs);
}
