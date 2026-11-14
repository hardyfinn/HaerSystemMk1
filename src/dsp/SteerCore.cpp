#include "dsp/SteerCore.hpp"
#include "common/MiniJson.hpp"
#include <algorithm>
#include <cmath>
#include <functional>

namespace codex {

float steerCurve(int type, float x, int steps) {
	x = x < 0.f ? 0.f : (x > 1.f ? 1.f : x);
	switch (type) {
		case STEER_EXP:
			// slow start, fast finish
			return (std::pow(16.f, x) - 1.f) / 15.f;
		case STEER_LOG:
			// fast start, slow finish (the mirror of the one above)
			return 1.f - (std::pow(16.f, 1.f - x) - 1.f) / 15.f;
		case STEER_S:
			return x * x * (3.f - 2.f * x);
		case STEER_STEP: {
			int n = steps < 2 ? 2 : (steps > 32 ? 32 : steps);
			float k = std::floor(x * (float)n);
			if (k > (float)(n - 1))
				k = (float)(n - 1);
			return k / (float)(n - 1);
		}
		default:
			return x;
	}
}

const char* steerCurveName(int type) {
	static const char* NAMES[STEER_CURVES] = {"Linear", "Exponential (slow start)", "Logarithmic (fast start)", "S-shaped", "Steps"};
	return type >= 0 && type < STEER_CURVES ? NAMES[type] : "";
}

// ---------------------------------------------------------------- tree

void SteerTree::clear() {
	nodes.clear();
	nextId = 1;
	current = target = -1;
}

SteerNode* SteerTree::find(int id) {
	for (SteerNode& n : nodes)
		if (n.id == id)
			return &n;
	return nullptr;
}

const SteerNode* SteerTree::find(int id) const {
	for (const SteerNode& n : nodes)
		if (n.id == id)
			return &n;
	return nullptr;
}

int SteerTree::add(int parent) {
	SteerNode n;
	n.id = nextId++;
	n.parent = find(parent) ? parent : -1;
	n.name = "v" + std::to_string(n.id);
	nodes.push_back(n);
	return n.id;
}

bool SteerTree::setParent(int id, int parent) {
	SteerNode* n = find(id);
	if (!n || id == parent)
		return false;
	if (parent != -1 && !find(parent))
		return false;
	// walking up from the new parent must not arrive back at this node
	int up = parent;
	for (size_t guard = 0; up != -1 && guard <= nodes.size(); guard++) {
		if (up == id)
			return false;
		const SteerNode* u = find(up);
		up = u ? u->parent : -1;
	}
	n->parent = parent;
	return true;
}

void SteerTree::remove(int id) {
	SteerNode* n = find(id);
	if (!n)
		return;
	int parent = n->parent;
	for (SteerNode& c : nodes)
		if (c.parent == id)
			c.parent = parent;
	nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [id](const SteerNode& x) { return x.id == id; }), nodes.end());
	if (current == id)
		current = parent;
	if (target == id)
		target = -1;
}

std::vector<int> SteerTree::neighbours(int id) const {
	std::vector<int> out;
	const SteerNode* n = find(id);
	if (!n)
		return out;
	if (n->parent != -1 && find(n->parent))
		out.push_back(n->parent);
	for (const SteerNode& c : nodes)
		if (c.parent == id)
			out.push_back(c.id);
	return out;
}

std::vector<SteerPlace> SteerTree::layout() const {
	std::vector<SteerPlace> out;
	float nextRow = 0.f;
	std::function<float(int, int)> place = [&](int id, int depth) -> float {
		std::vector<int> kids;
		for (const SteerNode& c : nodes)
			if (c.parent == id)
				kids.push_back(c.id);
		float row;
		if (kids.empty() || depth > 64)
			row = nextRow++;
		else {
			float first = 0.f, last = 0.f;
			for (size_t i = 0; i < kids.size(); i++) {
				float r = place(kids[i], depth + 1);
				if (i == 0)
					first = r;
				last = r;
			}
			row = 0.5f * (first + last);
		}
		SteerPlace p;
		p.id = id;
		p.depth = depth;
		p.row = row;
		out.push_back(p);
		return row;
	};
	for (const SteerNode& n : nodes)
		if (n.parent == -1 || !find(n.parent))
			place(n.id, 0);
	return out;
}

std::string SteerTree::toJson() const {
	using namespace minijson;
	std::string out = "{\"nextId\":" + std::to_string(nextId) + ",\"current\":" + std::to_string(current) + ",\"target\":" + std::to_string(target) + ",\"nodes\":[";
	for (size_t i = 0; i < nodes.size(); i++) {
		const SteerNode& n = nodes[i];
		if (i)
			out += ',';
		out += "{\"id\":" + std::to_string(n.id) + ",\"parent\":" + std::to_string(n.parent) + ",\"name\":";
		putString(out, n.name);
		// one parameter: [module id, parameter number, value, 1 if it is a switch or stepped knob]
		out += ",\"params\":[";
		for (size_t k = 0; k < n.params.size(); k++) {
			const SteerParamValue& v = n.params[k];
			if (k)
				out += ',';
			out += '[' + std::to_string((long long)v.module) + ',' + std::to_string(v.param) + ',';
			putNumber(out, v.value, 9);
			out += v.snap ? ",1]" : ",0]";
		}
		out += "],\"states\":[";
		for (size_t k = 0; k < n.states.size(); k++) {
			if (k)
				out += ',';
			out += "{\"module\":" + std::to_string((long long)n.states[k].module) + ",\"state\":" + (n.states[k].json.empty() ? std::string("null") : n.states[k].json) + '}';
		}
		out += "]}";
	}
	out += "]}";
	return out;
}

bool SteerTree::fromJson(const std::string& text) {
	minijson::Reader r(text);
	SteerTree t;
	if (!r.need('{'))
		return false;
	if (!r.eat('}')) {
		do {
			std::string key = r.string();
			if (!r.need(':'))
				return false;
			if (key == "nextId")
				t.nextId = (int)r.number();
			else if (key == "current")
				t.current = (int)r.number();
			else if (key == "target")
				t.target = (int)r.number();
			else if (key == "nodes") {
				r.need('[');
				bool firstNode = true;
				while (r.ok && !r.eat(']')) {
					if (!firstNode)
						r.need(',');
					firstNode = false;
					SteerNode n;
					r.need('{');
					if (!r.eat('}')) {
						do {
							std::string k = r.string();
							r.need(':');
							if (k == "id")
								n.id = (int)r.number();
							else if (k == "parent")
								n.parent = (int)r.number();
							else if (k == "name")
								n.name = r.string();
							else if (k == "params") {
								r.need('[');
								bool first = true;
								while (r.ok && !r.eat(']')) {
									if (!first)
										r.need(',');
									first = false;
									SteerParamValue v;
									r.need('[');
									v.module = (int64_t)r.number();
									r.need(',');
									v.param = (int)r.number();
									r.need(',');
									v.value = (float)r.number();
									r.need(',');
									v.snap = r.number() != 0.0;
									r.need(']');
									n.params.push_back(v);
								}
							}
							else if (k == "states") {
								r.need('[');
								bool first = true;
								while (r.ok && !r.eat(']')) {
									if (!first)
										r.need(',');
									first = false;
									SteerState s;
									r.need('{');
									if (!r.eat('}')) {
										do {
											std::string sk = r.string();
											r.need(':');
											if (sk == "module")
												s.module = (int64_t)r.number();
											else if (sk == "state") {
												s.json = r.raw();
												if (s.json == "null")
													s.json.clear();
											}
											else
												r.skip();
										} while (r.ok && r.eat(','));
										r.need('}');
									}
									n.states.push_back(s);
								}
							}
							else
								r.skip();
						} while (r.ok && r.eat(','));
						r.need('}');
					}
					if (!r.ok)
						return false;
					t.nodes.push_back(n);
				}
			}
			else
				r.skip();
		} while (r.ok && r.eat(','));
		if (!r.need('}'))
			return false;
	}
	if (!r.ok)
		return false;
	// tidy anything that does not hang together
	for (SteerNode& n : t.nodes) {
		t.nextId = std::max(t.nextId, n.id + 1);
		if (n.parent != -1 && (!t.find(n.parent) || n.parent == n.id))
			n.parent = -1;
	}
	if (!t.find(t.current))
		t.current = -1;
	if (!t.find(t.target))
		t.target = -1;
	*this = std::move(t);
	return true;
}

bool operator==(const SteerTree& a, const SteerTree& b) {
	if (a.nextId != b.nextId || a.current != b.current || a.target != b.target || a.nodes.size() != b.nodes.size())
		return false;
	for (size_t i = 0; i < a.nodes.size(); i++) {
		const SteerNode& x = a.nodes[i];
		const SteerNode& y = b.nodes[i];
		if (x.id != y.id || x.parent != y.parent || x.name != y.name || x.params.size() != y.params.size() || x.states.size() != y.states.size())
			return false;
		for (size_t k = 0; k < x.params.size(); k++)
			if (x.params[k].module != y.params[k].module || x.params[k].param != y.params[k].param || x.params[k].value != y.params[k].value || x.params[k].snap != y.params[k].snap)
				return false;
		for (size_t k = 0; k < x.states.size(); k++)
			if (x.states[k].module != y.states[k].module || x.states[k].json != y.states[k].json)
				return false;
	}
	return true;
}

int steerWalk(const SteerTree& tree, int from, float prob, Rng& rng) {
	float move = rng.uniform();
	float pick = rng.uniform();
	std::vector<int> near = tree.neighbours(from);
	if (near.empty() || !(move < prob))
		return from;
	size_t i = std::min((size_t)(pick * (float)near.size()), near.size() - 1);
	return near[i];
}

// ---------------------------------------------------------------- gestures

SteerGesture::SteerGesture() {
	pos.resize(MAX);
	val.resize(MAX);
}

void SteerGesture::clear() {
	count = 0;
	bars = 0.0;
}

void SteerGesture::add(double p, float v) {
	if (p < 0.0)
		return;
	if (count > 0 && p - pos[(size_t)count - 1] < 1.0 / 384.0) {
		val[(size_t)count - 1] = v; // too close to the last point: keep the newest value
		return;
	}
	if (count >= MAX)
		return;
	pos[(size_t)count] = p;
	val[(size_t)count] = v;
	count++;
}

void SteerGesture::finish(double length) {
	if (count == 0) {
		bars = 0.0;
		return;
	}
	bars = std::min(std::max(std::ceil(length - 1e-6), 1.0), 16.0);
}

float SteerGesture::valueAt(double p) const {
	if (empty())
		return 0.f;
	p -= bars * std::floor(p / bars);
	// the first point after p
	int lo = 0, hi = count;
	while (lo < hi) {
		int mid = (lo + hi) / 2;
		if (pos[(size_t)mid] <= p)
			lo = mid + 1;
		else
			hi = mid;
	}
	if (lo == 0)
		return val[0];
	if (lo >= count)
		return val[(size_t)count - 1];
	double a = pos[(size_t)lo - 1], b = pos[(size_t)lo];
	float f = b > a ? (float)((p - a) / (b - a)) : 0.f;
	return val[(size_t)lo - 1] + (val[(size_t)lo] - val[(size_t)lo - 1]) * f;
}

double SteerGesture::loosen(double p, float loose, int seed) {
	if (loose <= 0.f)
		return p;
	// a random offset for every 16th of a bar, joined up smoothly
	double x = p * 16.0;
	double cell = std::floor(x);
	float f = (float)(x - cell);
	auto offset = [seed](double c) {
		uint64_t h = ((uint64_t)(uint32_t)seed << 32) ^ (uint64_t)(int64_t)c;
		uint64_t z = splitmix64(h);
		return (float)(z >> 40) * (2.f / 16777216.f) - 1.f;
	};
	float s = f * f * (3.f - 2.f * f);
	float j = offset(cell) + (offset(cell + 1.0) - offset(cell)) * s;
	return p + (double)(loose * j) / 16.0;
}

} // namespace codex
