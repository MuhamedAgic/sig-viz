#pragma once

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <format>
#include <ranges>
#include <span>
#include <vector>

#include <imgui.h>

#include "../core/signal_data.hpp"

namespace detail {

	// A rectangle on screen plus the time range / gain it displays.
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

	// Rounds to 1, 2, 5 x 10^n so grid labels look sensible at any zoom.
	[[nodiscard]] inline double nice_step(double raw) {
		const double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
		const double n = raw / magnitude;
		return magnitude * (n < 1.5 ? 1.0 : n < 3.5 ? 2.0 : n < 7.5 ? 5.0 : 10.0);
	}

	inline void draw_grid(ImDrawList& dl, const Canvas& c, bool time_labels) {
		constexpr ImU32 grid_col = IM_COL32(255, 255, 255, 28);
		constexpr ImU32 axis_col = IM_COL32(255, 255, 255, 80);
		constexpr ImU32 text_col = IM_COL32(255, 255, 255, 130);

		for (const float level : {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f}) {
			const float y = c.y_at(level);
			dl.AddLine({ c.min.x, y }, { c.max.x, y }, level == 0.0f ? axis_col : grid_col);
		}

		const double step = nice_step((c.t_end - c.t_begin) / 6.0);
		for (double k = std::ceil(c.t_begin / step); k * step < c.t_end; k += 1.0) {
			const double t = k * step;
			const float  x = c.x_at(t);
			dl.AddLine({ x, c.min.y }, { x, c.max.y }, grid_col);

			if (time_labels) {
				const auto label = std::format("{:.4g}s", t);
				dl.AddText({ x + 4.0f, c.max.y - ImGui::GetTextLineHeight() - 2.0f }, text_col, label.c_str());
			}
		}
	}

	// Darkens the parts of the canvas that lie before 0 s or after the signal's end.
	inline void shade_outside_signal(ImDrawList& dl, const Canvas& c, double duration) {
		constexpr ImU32 shade = IM_COL32(0, 0, 0, 110);
		const float x_begin = std::clamp(c.x_at(0.0), c.min.x, c.max.x);
		const float x_end = std::clamp(c.x_at(duration), c.min.x, c.max.x);
		dl.AddRectFilled(c.min, { x_begin, c.max.y }, shade);
		dl.AddRectFilled({ x_end, c.min.y }, c.max, shade);
	}

	inline void draw_waveform(ImDrawList& dl, const Canvas& c, const sig::Signal& s, ImU32 color) {
		const auto   n = static_cast<std::ptrdiff_t>(s.samples.size());
		const double rate = s.sample_rate;
		const double first = c.t_begin * rate;                          // fractional sample index at left edge
		const double per_px = (c.t_end - c.t_begin) * rate / c.width();  // samples per pixel

		dl.PushClipRect(c.min, c.max, true);

		if (per_px <= 1.0) {
			// Zoomed in: connect the individual samples.
			const auto lo = std::max<std::ptrdiff_t>(static_cast<std::ptrdiff_t>(std::floor(first)), 0);
			const auto hi = std::min<std::ptrdiff_t>(
				static_cast<std::ptrdiff_t>(std::ceil(first + c.width() * per_px)), n - 1);

			if (hi > lo) {
				const auto points =
					std::views::iota(lo, hi + 1)
					| std::views::transform([&](std::ptrdiff_t i) {
					return ImVec2{ c.x_at(static_cast<double>(i) / rate),
								  c.y_at(s.samples[static_cast<std::size_t>(i)]) };
						})
					| std::ranges::to<std::vector>();
				dl.AddPolyline(points.data(), static_cast<int>(points.size()), color, ImDrawFlags_None, 1.5f);
			}
		}
		else {
			// Zoomed out: one min/max line per pixel column, so no peaks are skipped.
			const std::span<const float> all{ s.samples };
			for (int px = 0; px < static_cast<int>(c.width()); ++px) {
				const auto lo = std::max<std::ptrdiff_t>(static_cast<std::ptrdiff_t>(std::floor(first + px * per_px)), 0);
				const auto hi = std::min<std::ptrdiff_t>(static_cast<std::ptrdiff_t>(std::ceil(first + (px + 1) * per_px)), n);
				if (lo >= hi) continue;

				const auto [lowest, highest] = std::ranges::minmax(
					all.subspan(static_cast<std::size_t>(lo), static_cast<std::size_t>(hi - lo)));
				const float x = c.min.x + static_cast<float>(px) + 0.5f;
				dl.AddLine({ x, c.y_at(highest) }, { x, c.y_at(lowest) + 1.0f }, color);
			}
		}

		dl.PopClipRect();
	}

	inline void draw_panel(ImDrawList& dl, const Canvas& c, const sig::Signal& s, bool time_labels) {
		dl.AddRectFilled(c.min, c.max, IM_COL32(18, 20, 26, 255));
		shade_outside_signal(dl, c, s.duration());
		draw_grid(dl, c, time_labels);
		draw_waveform(dl, c, s, IM_COL32(80, 200, 255, 255));
		dl.AddRect(c.min, c.max, IM_COL32(255, 255, 255, 60));
	}

} // namespace detail


class SignalViewer {
public:
	SignalViewer() : signals_{ sig::make_example_signals() } { select(0); }

	void draw() {
		ImGui::SetNextWindowSize({ 1000.0f, 700.0f }, ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Signal Viewer")) {
			ImGui::End();
			return;
		}

		draw_picker();

		// Fetch *after* the picker, since it may have changed the selection.
		const sig::Signal& signal = current();
		const float        duration = signal.duration();

		playback_.advance(ImGui::GetIO().DeltaTime, duration);

		ImGui::TextWrapped("%s", signal.description.c_str());
		ImGui::TextDisabled("%s", std::format("{:.0f} Hz sample rate, {} samples, {:.2f} s",
			signal.sample_rate, signal.samples.size(), duration).c_str());
		ImGui::Spacing();

		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
			&& ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
			playback_.toggle(duration);
		}

		draw_transport(duration);

		// Anything that can seek reports whether it is being dragged, so playback
		// pauses itself while the user is scrubbing.
		bool scrubbing = false;

		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::SliderFloat("##position", &playback_.position, 0.0f, duration, "%.3f s");
		scrubbing |= ImGui::IsItemActive();

		ImGui::SeparatorText("Overview (click / drag to seek)");
		scrubbing |= draw_overview(signal);

		ImGui::SeparatorText("Scope");
		scrubbing |= draw_scope(signal);

		playback_.scrubbing = scrubbing;

		ImGui::End();
	}

private:
	static constexpr float min_view_seconds = 0.0005f;
	static constexpr float max_view_seconds = 10.0f;

	struct Playback {
		float position = 0.0f;  // seconds
		float speed = 1.0f;
		bool  playing = false;
		bool  loop = true;
		bool  scrubbing = false;

		void advance(float dt, float duration) {
			if (!playing || scrubbing) return;

			position += dt * speed;
			if (position < duration) return;

			if (loop) {
				position = std::fmod(position, duration);
			}
			else {
				position = duration;
				playing = false;
			}
		}

		void toggle(float duration) {
			if (!playing && position >= duration) position = 0.0f;  // replay from the start
			playing = !playing;
		}
	};

	[[nodiscard]] const sig::Signal& current() const { return signals_[selected_]; }

	void select(std::size_t index) {
		selected_ = index;
		playback_.position = 0.0f;
		view_seconds_ = signals_[index].default_view_seconds;
	}

	void draw_picker() {
		if (!ImGui::BeginCombo("Signal", current().name.c_str())) return;

		for (std::size_t i = 0; i < signals_.size(); ++i) {
			const bool is_selected = (i == selected_);
			if (ImGui::Selectable(signals_[i].name.c_str(), is_selected) && !is_selected) select(i);
			if (is_selected) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	void draw_transport(float duration) {
		const float unit = ImGui::GetFontSize();

		if (ImGui::Button(playback_.playing ? "Pause" : "Play", { unit * 4.0f, 0.0f })) {
			playback_.toggle(duration);
		}
		ImGui::SameLine();
		if (ImGui::Button("Stop", { unit * 4.0f, 0.0f })) {
			playback_.playing = false;
			playback_.position = 0.0f;
		}
		ImGui::SameLine();
		ImGui::Checkbox("Loop", &playback_.loop);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(unit * 10.0f);
		ImGui::SliderFloat("Speed", &playback_.speed, 0.05f, 4.0f, "%.2fx", ImGuiSliderFlags_Logarithmic);
	}

	// Whole signal at a glance. Returns true while the user drags in it.
	bool draw_overview(const sig::Signal& signal) {
		const float  duration = signal.duration();
		const ImVec2 size{ ImGui::GetContentRegionAvail().x, ImGui::GetTextLineHeight() * 5.0f };
		const ImVec2 p0 = ImGui::GetCursorScreenPos();

		ImGui::InvisibleButton("##overview", size);
		const bool dragging = ImGui::IsItemActive();
		if (dragging) {
			const float u = std::clamp((ImGui::GetIO().MousePos.x - p0.x) / size.x, 0.0f, 1.0f);
			playback_.position = u * duration;
		}

		const detail::Canvas canvas{
			.min = p0,
			.max = {p0.x + size.x, p0.y + size.y},
			.t_begin = 0.0,
			.t_end = duration,
			.gain = 1.0f,
		};

		ImDrawList& dl = *ImGui::GetWindowDrawList();
		detail::draw_panel(dl, canvas, signal, false);

		// Highlight the part of the signal the scope is currently showing.
		const double half = view_seconds_ * 0.5;
		const float  x0 = std::clamp(canvas.x_at(playback_.position - half), canvas.min.x, canvas.max.x);
		const float  x1 = std::clamp(canvas.x_at(playback_.position + half), canvas.min.x, canvas.max.x);
		dl.AddRectFilled({ x0, canvas.min.y }, { std::max(x1, x0 + 2.0f), canvas.max.y }, IM_COL32(255, 255, 255, 35));

		const float xp = canvas.x_at(playback_.position);
		dl.AddLine({ xp, canvas.min.y }, { xp, canvas.max.y }, IM_COL32(255, 200, 0, 255), 2.0f);

		return dragging;
	}

	// Zoomed view centred on the playhead. Returns true while the user drags in it.
	bool draw_scope(const sig::Signal& signal) {
		const float unit = ImGui::GetFontSize();
		const float duration = signal.duration();

		ImGui::SetNextItemWidth(unit * 12.0f);
		ImGui::SliderFloat("Window", &view_seconds_, min_view_seconds, max_view_seconds, "%.4f s",
			ImGuiSliderFlags_Logarithmic);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(unit * 8.0f);
		ImGui::SliderFloat("Gain", &gain_, 0.1f, 5.0f, "%.2fx", ImGuiSliderFlags_Logarithmic);
		ImGui::TextDisabled("Drag the scope to scrub, mouse wheel to zoom, Space to play/pause.");

		const ImVec2 avail = ImGui::GetContentRegionAvail();
		const ImVec2 size{ avail.x, std::max(avail.y, 150.0f) };
		const ImVec2 p0 = ImGui::GetCursorScreenPos();

		ImGui::InvisibleButton("##scope", size);
		const bool    dragging = ImGui::IsItemActive();
		const ImGuiIO& io = ImGui::GetIO();

		if (dragging) {
			const float delta = io.MouseDelta.x / size.x * view_seconds_;
			playback_.position = std::clamp(playback_.position - delta, 0.0f, duration);
		}
		if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f) {
			view_seconds_ = std::clamp(view_seconds_ * std::pow(0.85f, io.MouseWheel),
				min_view_seconds, max_view_seconds);
		}

		const double half = view_seconds_ * 0.5;
		const detail::Canvas canvas{
			.min = p0,
			.max = {p0.x + size.x, p0.y + size.y},
			.t_begin = playback_.position - half,
			.t_end = playback_.position + half,
			.gain = gain_,
		};

		ImDrawList& dl = *ImGui::GetWindowDrawList();
		detail::draw_panel(dl, canvas, signal, true);

		const float xp = canvas.x_at(playback_.position);
		dl.AddLine({ xp, canvas.min.y }, { xp, canvas.max.y }, IM_COL32(255, 200, 0, 255), 2.0f);

		return dragging;
	}

	std::vector<sig::Signal> signals_;
	std::size_t              selected_ = 0;
	Playback                 playback_;
	float                    view_seconds_ = 0.02f;
	float                    gain_ = 1.0f;
};