#include "waveform.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <string>

#include "core/signal.hpp"

namespace waveform {
	namespace {

		// Rounds to 1, 2, 5 x 10^n so grid labels look sensible at any zoom.
		[[nodiscard]] double nice_step(double raw) {
			const double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
			const double n = raw / magnitude;
			return magnitude * (n < 1.5 ? 1.0 : n < 3.5 ? 2.0 : n < 7.5 ? 5.0 : 10.0);
		}

		[[nodiscard]] std::string format_time(double t, double step) {
			if (step < 1e-3) {
				return std::format("{:.4g}us", t * 1e6);
			}
			if (step < 1.0) {
				return std::format("{:.4g}ms", t * 1e3);
			}
			return std::format("{:.4g}s", t);
		}

		void draw_trace(ImDrawList& dl, const Canvas& c, const sig::PeakIndex& peaks, double rate, ImU32 color) {
			const auto   samples = peaks.samples();
			const double first = c.t_begin * rate;                          // fractional sample index at the left edge
			const double per_px = (c.t_end - c.t_begin) * rate / c.width(); // samples per pixel

			// Fractional sample position -> valid index (or one past the end).
			const auto index = [&](double position) {
				return static_cast<std::size_t>(std::clamp(position, 0.0, static_cast<double>(samples.size())));
			};

			if (per_px <= 1.0) {
				// Zoomed in: connect the individual samples.
				const std::size_t begin = index(std::floor(first));
				const std::size_t end = index(std::ceil(first + c.width() * per_px) + 1.0);
				if (end - begin < 2) {
					return;
				}

				for (std::size_t i = begin; i < end; ++i) {
					dl.PathLineTo({ c.x_at(static_cast<double>(i) / rate), c.y_at(samples[i]) });
				}
				dl.PathStroke(color, ImDrawFlags_None, 1.5f);
			}
			else {
				// Zoomed out: one min-to-max line per pixel column, so no peak is ever skipped.
				for (int px = 0; px < static_cast<int>(c.width()); ++px) {
					const sig::MinMax range = peaks.range(
						index(std::floor(first + px * per_px)),
						index(std::ceil(first + (px + 1) * per_px))
					);
					if (range.empty()) {
						continue;
					}

					const float x = c.min.x + static_cast<float>(px) + 0.5f;
					dl.AddLine({ x, c.y_at(range.max) }, { x, c.y_at(range.min) + 1.0f }, color);
				}
			}
		}

	} // namespace

	void draw_background(ImDrawList& dl, const Canvas& c, double duration, bool time_labels) {
		constexpr ImU32 grid_col = IM_COL32(255, 255, 255, 28);
		constexpr ImU32 axis_col = IM_COL32(255, 255, 255, 80);
		constexpr ImU32 text_col = IM_COL32(255, 255, 255, 130);
		constexpr ImU32 shade = IM_COL32(0, 0, 0, 110);

		dl.AddRectFilled(c.min, c.max, IM_COL32(18, 20, 26, 255));

		// Darken what lies before 0 s or after the end.
		const float x_begin = std::clamp(c.x_at(0.0), c.min.x, c.max.x);
		const float x_end = std::clamp(c.x_at(duration), c.min.x, c.max.x);
		dl.AddRectFilled(c.min, { x_begin, c.max.y }, shade);
		dl.AddRectFilled({ x_end, c.min.y }, c.max, shade);

		for (const float level : { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f }) {
			const float y = c.y_at(level);
			if (y < c.min.y || y > c.max.y) {
				continue;
			}
			dl.AddLine({ c.min.x, y }, { c.max.x, y }, level == 0.0f ? axis_col : grid_col);
		}

		const double step = nice_step((c.t_end - c.t_begin) / 6.0);
		for (double k = std::ceil(c.t_begin / step); k * step < c.t_end; k += 1.0) {
			const double t = k * step + 0.0; // "+ 0.0" turns -0.0 into 0.0
			const float  x = c.x_at(t);
			dl.AddLine({ x, c.min.y }, { x, c.max.y }, grid_col);

			if (time_labels) {
				const std::string label = format_time(t, step);
				dl.AddText({ x + 4.0f, c.max.y - ImGui::GetTextLineHeight() - 2.0f }, text_col, label.c_str());
			}
		}
	}

	void draw_traces(ImDrawList& dl, const Canvas& canvas, const Track& track, const Colors& colors) {
		dl.PushClipRect(canvas.min, canvas.max, true);
		const auto peaks = track.peaks();
		for (std::size_t ch = 0; ch < peaks.size(); ++ch) {
			draw_trace(dl, canvas, peaks[ch], track.signal().sample_rate, colors[ch % colors.size()]);
		}
		dl.PopClipRect();
	}

	void draw_track(ImDrawList& dl, const Canvas& canvas, const Track& track, bool time_labels) {
		const sig::Signal& signal = track.signal();
		draw_background(dl, canvas, signal.duration(), time_labels);
		draw_traces(dl, canvas, track, colors_a);

		if (signal.is_iq()) {
			draw_label(dl, canvas, 0, "I", colors_a[0]);
			dl.AddText({ canvas.min.x + 6.0f + ImGui::CalcTextSize("I ").x, canvas.min.y + 4.0f }, colors_a[1], "Q");
		}
		draw_border(dl, canvas);
	}

	void draw_label(ImDrawList& dl, const Canvas& canvas, int line, const char* text, ImU32 color) {
		const float y = canvas.min.y + 4.0f + static_cast<float>(line) * ImGui::GetTextLineHeight();
		dl.AddText({ canvas.min.x + 6.0f, y }, color, text);
	}

	void draw_border(ImDrawList& dl, const Canvas& canvas) {
		dl.AddRect(canvas.min, canvas.max, IM_COL32(255, 255, 255, 60));
	}

	void draw_playhead(ImDrawList& dl, const Canvas& canvas, double t) {
		const float x = canvas.x_at(t);
		if (x < canvas.min.x || x > canvas.max.x) {
			return;
		}
		dl.AddLine({ x, canvas.min.y }, { x, canvas.max.y }, playhead_color, 2.0f);
	}

	void draw_selection(ImDrawList& dl, const Canvas& canvas, double begin, double end) {
		const float x0 = std::clamp(canvas.x_at(begin), canvas.min.x, canvas.max.x);
		const float x1 = std::clamp(canvas.x_at(end), canvas.min.x, canvas.max.x);
		if (x1 <= x0) {
			return;
		}
		dl.AddRectFilled({ x0, canvas.min.y }, { x1, canvas.max.y }, IM_COL32(120, 160, 255, 45));
		dl.AddLine({ x0, canvas.min.y }, { x0, canvas.max.y }, IM_COL32(120, 160, 255, 160));
		dl.AddLine({ x1, canvas.min.y }, { x1, canvas.max.y }, IM_COL32(120, 160, 255, 160));
	}

	void draw_cursor(ImDrawList& dl, const Canvas& canvas, double t, const char* label) {
		const float x = canvas.x_at(t);
		if (x < canvas.min.x || x > canvas.max.x) {
			return;
		}
		dl.AddLine({ x, canvas.min.y }, { x, canvas.max.y }, cursor_color, 1.5f);
		dl.AddText({ x + 3.0f, canvas.min.y + 2.0f }, cursor_color, label);
	}

} // namespace waveform
