#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "ui.hpp"
#include "waveform.hpp"
#include "workspace.hpp"
#include "core/analysis.hpp"

// Two signals on a shared time axis, so they scroll and zoom together: stacked or on top of each
// other, with their difference A - B underneath. B can be shifted in time, by hand or automatically
// (cross-correlation finds the shift where they match best).
class CompareWindow {
public:
	static constexpr const char* title = "Compare";
	bool                         open = true;

	void draw(Workspace& ws);

private:
	enum class Layout { Stacked, Overlay };

	friend constexpr std::string_view name(Layout layout) {
		return layout == Layout::Stacked ? "Stacked" : "On top of each other";
	}

	void draw_controls(Workspace& ws);
	void update_difference();
	void draw_panels(Workspace& ws);
	void handle_mouse(const ui::Area& area, const waveform::Canvas& canvas, double& position, double duration);

	[[nodiscard]] bool same_rate() const {
		return a_->signal().sample_rate == b_->signal().sample_rate;
	}
	[[nodiscard]] std::ptrdiff_t lag() const; // offset_ in samples

	TrackPtr    a_, b_;
	double      offset_ = 0.0;   // seconds B is shifted by (positive = later)
	bool        follow_ = true;  // scroll with the viewer's playhead
	double      position_ = 0.0; // own position when not following
	float       window_seconds_ = 0.02f;
	float       gain_ = 1.0f;
	Layout      layout_ = Layout::Stacked;
	bool        show_difference_ = true;
	std::string message_;

	// A - B and how similar they are, for the current A, B and shift.
	TrackPtr                  difference_;
	TrackPtr                  difference_a_, difference_b_;
	std::ptrdiff_t            difference_lag_ = 0;
	sig::analysis::Similarity similarity_;
};
