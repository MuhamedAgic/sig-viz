#pragma once

// Draws signals into an ImGui draw list: background, grid, one trace per channel, legend.

#include <span>

#include <imgui.h>

#include "core/peak_index.hpp"
#include "core/signal.hpp"

namespace waveform {

	// A rectangle on screen plus the time range and gain it shows.
	struct Canvas {
		ImVec2 min;
		ImVec2 max;
		double t_begin;
		double t_end;
		float  gain;

		[[nodiscard]] float width() const { return max.x - min.x; }

		[[nodiscard]] float x_at(double t) const {
			return min.x + static_cast<float>((t - t_begin) / (t_end - t_begin)) * width();
		}

		// value in [-1, 1] -> screen y (with 5 % headroom)
		[[nodiscard]] float y_at(float value) const {
			const float mid = (min.y + max.y) * 0.5f;
			const float half = (max.y - min.y) * 0.5f * 0.95f;
			return mid - value * gain * half;
		}
	};

	// The whole panel. `peaks` holds one index per channel of `signal` (it is where the samples are
	// read from, and keeps zoomed-out views fast).
	void draw_signal(ImDrawList& dl, const Canvas& canvas, const sig::Signal& signal,
		std::span<const sig::PeakIndex> peaks, bool time_labels);

	// Vertical marker line at time t.
	void draw_playhead(ImDrawList& dl, const Canvas& canvas, double t);

} // namespace waveform
