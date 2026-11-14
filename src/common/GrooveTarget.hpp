// How EAR hands a learned groove to WARP (spec §13B, "send groove to WARP").
// WARP implements this; EAR finds it with dynamic_cast on the UI thread.
#pragma once
#include <string>

namespace codex {

struct GrooveTarget {
	virtual ~GrooveTarget() {}
	// UI thread. Points between (0,0) and (1,1), not including those two; at most 14.
	virtual void receiveGroove(const float* xs, const float* ys, int count) = 0;
	virtual std::string grooveLabel() = 0;
};

} // namespace codex
