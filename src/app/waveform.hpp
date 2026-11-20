#pragma once

// Draws signals into an ImGui draw list: background and grid, one trace per channel, and marks
// (playhead, selection, cursors). Fast for any signal length: zoomed out, it reads peak indices.

#include <array>
#include <span>

#include <imgui.h>

#include "library.hpp"
#include "core/peak_index.hpp"

namespace waveform {

	using Colors = std::array<ImU32, 2>; // per channel: real / I, Q

	inline constexpr Colors colors_a{ IM_COL32(80, 200, 255, 255), IM_COL32(255, 150, 60, 255) };   // blue / orange
	inline constexpr Colors colors_b{ IM_COL32(120, 230, 120, 255), IM_COL32(230, 110, 210, 255) }; // green / pink
	inline constexpr ImU32  playhead_color = IM_COL32(255, 200, 0, 255);
	inline constexpr ImU32  cursor_color = IM_COL32(255, 90, 90, 255);

	// A rectangle on screen plus the time range and gain it shows.
	struct Canvas {
		ImVec2 min;
		ImVec2 max;
		double t_begin;
		double t_end;
		float  gain;

		[[nodiscard]] float width() const {
			return max.x - min.x;
		}

		[[nodiscard]] float x_at(double t) const {
			return min.x + static_cast<float>((t - t_begin) / (t_end - t_begin)) * width();
		}

		[[nodiscard]] double t_at(float x) const {
			return t_begin + (x - min.x) / width() * (t_end - t_begin);
		}

		// value in [-1, 1] -> screen y (with 5 % headroom)
		[[nodiscard]] float y_at(float value) const {
			const float mid = (min.y + max.y) * 0.5f;
			const float half = (max.y - min.y) * 0.5f * 0.95f;
			return mid - value * gain * half;
		}

		// The same rectangle showing a signal that starts `offset` seconds later.
		[[nodiscard]] Canvas shifted(double offset) const {
			return { min, max, t_begin - offset, t_end - offset, gain };
		}
	};

	// Panel background, darkened outside [0, duration], with a grid (and time labels along the bottom).
	void draw_background(ImDrawList& dl, const Canvas& canvas, double duration, bool time_labels);

	// One trace per channel of the track.
	void draw_traces(ImDrawList& dl, const Canvas& canvas, const Track& track, const Colors& colors);

	// Background + traces + "I / Q" legend + border: a complete panel.
	void draw_track(ImDrawList& dl, const Canvas& canvas, const Track& track, bool time_labels);

	// A label in the top-left corner; `line` stacks several.
	void draw_label(ImDrawList& dl, const Canvas& canvas, int line, const char* text, ImU32 color);

	void draw_border(ImDrawList& dl, const Canvas& canvas);
	void draw_playhead(ImDrawList& dl, const Canvas& canvas, double t);
	void draw_selection(ImDrawList& dl, const Canvas& canvas, double begin, double end);
	void draw_cursor(ImDrawList& dl, const Canvas& canvas, double t, const char* label);

} // namespace waveform
