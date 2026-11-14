// CurveDisplay (spec §3.2): draws a curve on [0,1] -> [0,1] and lets the user
// drag its points. Used by WARP and SPLICE. The owner supplies callbacks, so
// the display holds no musical state of its own.
#pragma once
#include <rack.hpp>
#include <functional>
#include "common/Theme.hpp"

namespace codex {

struct CurveDisplay : rack::widget::OpaqueWidget {
	// The curve to draw. Called about 100 times per frame on the UI thread.
	std::function<float(float)> curve;
	// Playhead position 0..1, or a negative number to hide it.
	std::function<float()> playhead;
	// Draggable points. Leave numPoints unset for a display with no editing.
	std::function<int()> numPoints;
	std::function<rack::math::Vec(int)> getPoint;       // x and y in 0..1
	std::function<void(int, rack::math::Vec)> setPoint; // called while dragging
	std::function<bool()> editable;                     // points are shown and draggable only while true

	NVGcolor accent = theme::time();
	int dragIndex = -1;
	rack::math::Vec dragPos;

	bool canEdit() const { return numPoints && getPoint && setPoint && (!editable || editable()); }

	// 0..1 curve space to pixels (y runs upward) and back.
	rack::math::Vec toPx(rack::math::Vec p) const { return rack::math::Vec(p.x * box.size.x, (1.f - p.y) * box.size.y); }
	rack::math::Vec fromPx(rack::math::Vec p) const {
		return rack::math::Vec(rack::math::clamp(p.x / box.size.x, 0.f, 1.f), rack::math::clamp(1.f - p.y / box.size.y, 0.f, 1.f));
	}

	void draw(const DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 2.f);
		nvgFillColor(args.vg, theme::screen());
		nvgFill(args.vg);
		Widget::draw(args);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			NVGcontext* vg = args.vg;
			nvgSave(vg);
			nvgScissor(vg, 0, 0, box.size.x, box.size.y);
			// quarter grid and the straight "no curve" diagonal
			nvgBeginPath(vg);
			for (int i = 1; i < 4; i++) {
				nvgMoveTo(vg, box.size.x * i / 4.f, 0);
				nvgLineTo(vg, box.size.x * i / 4.f, box.size.y);
				nvgMoveTo(vg, 0, box.size.y * i / 4.f);
				nvgLineTo(vg, box.size.x, box.size.y * i / 4.f);
			}
			nvgMoveTo(vg, 0, box.size.y);
			nvgLineTo(vg, box.size.x, 0);
			nvgStrokeColor(vg, theme::grid());
			nvgStrokeWidth(vg, 0.6f);
			nvgStroke(vg);

			if (curve) {
				const int N = 96;
				nvgBeginPath(vg);
				for (int i = 0; i <= N; i++) {
					float x = (float)i / N;
					rack::math::Vec p = toPx(rack::math::Vec(x, rack::math::clamp(curve(x), 0.f, 1.f)));
					if (i == 0)
						nvgMoveTo(vg, p.x, p.y);
					else
						nvgLineTo(vg, p.x, p.y);
				}
				nvgStrokeColor(vg, accent);
				nvgStrokeWidth(vg, 1.4f);
				nvgStroke(vg);
			}
			if (playhead) {
				float ph = playhead();
				if (ph >= 0.f) {
					float x = rack::math::clamp(ph, 0.f, 1.f);
					float y = curve ? rack::math::clamp(curve(x), 0.f, 1.f) : x;
					rack::math::Vec p = toPx(rack::math::Vec(x, y));
					nvgBeginPath(vg);
					nvgMoveTo(vg, p.x, 0);
					nvgLineTo(vg, p.x, box.size.y);
					nvgStrokeColor(vg, nvgTransRGBAf(theme::text(), 0.35f));
					nvgStrokeWidth(vg, 0.8f);
					nvgStroke(vg);
					nvgBeginPath(vg);
					nvgCircle(vg, p.x, p.y, 2.2f);
					nvgFillColor(vg, theme::text());
					nvgFill(vg);
				}
			}
			if (canEdit()) {
				int n = numPoints();
				for (int i = 0; i < n; i++) {
					rack::math::Vec p = toPx(getPoint(i));
					nvgBeginPath(vg);
					nvgCircle(vg, p.x, p.y, i == dragIndex ? 3.6f : 2.8f);
					nvgFillColor(vg, theme::screen());
					nvgFill(vg);
					nvgStrokeColor(vg, accent);
					nvgStrokeWidth(vg, 1.2f);
					nvgStroke(vg);
				}
			}
			nvgRestore(vg);
		}
		Widget::drawLayer(args, layer);
	}

	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && canEdit()) {
			// grab the nearest point within reach
			int best = -1;
			float bestDist = 12.f;
			int n = numPoints();
			for (int i = 0; i < n; i++) {
				float d = toPx(getPoint(i)).minus(e.pos).norm();
				if (d < bestDist) {
					bestDist = d;
					best = i;
				}
			}
			dragIndex = best;
			dragPos = e.pos;
			if (best >= 0)
				e.consume(this);
			return;
		}
		OpaqueWidget::onButton(e);
	}

	void onDragMove(const DragMoveEvent& e) override {
		if (dragIndex < 0 || !canEdit())
			return;
		dragPos = dragPos.plus(e.mouseDelta.div(getAbsoluteZoom()));
		setPoint(dragIndex, fromPx(dragPos));
	}

	void onDragEnd(const DragEndEvent& e) override { dragIndex = -1; }
};

} // namespace codex
