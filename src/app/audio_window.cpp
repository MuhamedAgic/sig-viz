#include "audio_window.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <initializer_list>
#include <limits>
#include <string>
#include <utility>

#include <imgui.h>
#include <implot.h>

#include "plots.hpp"
#include "tips.hpp"
#include "ui.hpp"
#include "core/analysis.hpp"
#include "core/units.hpp"

namespace {

	constexpr std::size_t pitch_window = 4096; // samples per pitch measurement

	// A horizontal meter: `value` on a scale from `low` to `high`, with labelled reference marks.
	void meter(const char* label, double value, double low, double high, std::initializer_list<std::pair<double, const char*>> marks) {
		const float width = ImGui::GetContentRegionAvail().x - ui::em(9.0f);
		const ImVec2 min = ImGui::GetCursorScreenPos();
		const ImVec2 max{ min.x + width, min.y + ImGui::GetFrameHeight() };
		ImDrawList& dl = *ImGui::GetWindowDrawList();
		const auto x_at = [&](double v) { return min.x + static_cast<float>(std::clamp((v - low) / (high - low), 0.0, 1.0)) * width; };

		dl.AddRectFilled(min, max, IM_COL32(40, 40, 50, 255));
		const ImU32 colour = value > -9.0 ? IM_COL32(230, 90, 70, 255) : value > -16.0 ? IM_COL32(230, 200, 70, 255) : IM_COL32(90, 200, 120, 255);
		if (std::isfinite(value)) dl.AddRectFilled(min, { x_at(value), max.y }, colour);
		for (const auto& [at, text] : marks) {
			dl.AddLine({ x_at(at), min.y }, { x_at(at), max.y }, IM_COL32(255, 255, 255, 150));
			dl.AddText({ x_at(at) + 3.0f, min.y }, IM_COL32(255, 255, 255, 150), text);
		}
		ImGui::Dummy({ width, ImGui::GetFrameHeight() });
		ImGui::SameLine();
		ImGui::Text("%s %s", std::isfinite(value) ? std::format("{:.1f}", value).c_str() : "-", label);
	}

} // namespace

AudioWindow::Overview AudioWindow::analyse(const TrackPtr& track, std::stop_token stop) {
	const sig::Signal& signal = track->signal();
	Overview out{ .track = track };

	// Pitch every ~23 ms (more spread out for very long signals).
	const std::size_t hop = std::max<std::size_t>(pitch_window / 4, signal.frames() / 4000);
	for (std::size_t start = 0; start + pitch_window <= signal.frames(); start += hop) {
		if (stop.stop_requested()) return out;
		const auto p = sig::analysis::pitch(signal, start, start + pitch_window);
		out.pitch_times.push_back((static_cast<double>(start) + pitch_window / 2.0) / signal.sample_rate);
		out.pitch_hz.push_back(p && p->clarity > 0.8 ? p->hz : std::numeric_limits<double>::quiet_NaN());
	}

	out.loudness = sig::analysis::loudness(signal, 0, signal.frames());
	const auto levels = sig::analysis::levels(signal, 0, signal.frames());
	out.clipped = levels.clipped;
	out.peak = levels.peak;
	return out;
}

void AudioWindow::draw(Workspace& ws) {
	if (!open) return;
	ImGui::SetNextWindowSize({ 600.0f, 500.0f }, ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(title, &open)) {
		ImGui::End();
		return;
	}

	if (requested_ != ws.current) {
		requested_ = ws.current;
		overview_ = {};
		job_.start([track = ws.current](std::stop_token stop) { return std::optional{ analyse(track, stop) }; });
	}
	if (auto result = job_.take()) overview_ = std::move(*result);

	if (ws.signal().is_iq()) ui::hint("These measurements are made for sound. This is an I/Q signal, so they look at I only.");

	if (ImGui::BeginTabBar("##audio")) {
		if (ImGui::BeginTabItem("Tuner")) {
			draw_tuner(ws);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Loudness")) {
			draw_loudness(ws);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Levels")) {
			draw_levels(ws);
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	export_.draw();
	ImGui::End();
}

void AudioWindow::draw_tuner(Workspace& ws) {
	const sig::Signal& signal = ws.signal();
	const auto centre = static_cast<std::size_t>(std::clamp(ws.playback.position * signal.sample_rate, 0.0, static_cast<double>(signal.frames())));
	const std::size_t begin = centre > pitch_window / 2 ? std::min(centre - pitch_window / 2, signal.frames() - std::min(signal.frames(), pitch_window)) : 0;
	const auto p = sig::analysis::pitch(signal, begin, begin + pitch_window);

	ImGui::SetWindowFontScale(2.0f);
	if (p && p->clarity > 0.8) {
		const auto note = sig::analysis::nearest_note(p->hz);
		const ImVec4 colour = std::abs(note.cents) < 5.0 ? ImVec4{ 0.4f, 1.0f, 0.5f, 1.0f } : ImVec4{ 1.0f, 0.8f, 0.3f, 1.0f };
		ImGui::TextColored(colour, "%s  %+.0f cents", note.name.c_str(), note.cents);
		ImGui::SetWindowFontScale(1.0f);
		ImGui::Text("%s, clarity %.0f %%", sig::format_hz(p->hz).c_str(), p->clarity * 100.0);
		// How far off: a needle on a -50 .. +50 cents scale.
		const float width = ImGui::GetContentRegionAvail().x;
		const ImVec2 min = ImGui::GetCursorScreenPos();
		const float height = ImGui::GetFrameHeight();
		ImDrawList& dl = *ImGui::GetWindowDrawList();
		dl.AddRectFilled(min, { min.x + width, min.y + height }, IM_COL32(40, 40, 50, 255));
		const float mid = min.x + width / 2.0f;
		dl.AddRectFilled({ mid - width * 0.05f, min.y }, { mid + width * 0.05f, min.y + height }, IM_COL32(60, 120, 70, 255)); // in tune: +-5 cents
		const float needle = mid + static_cast<float>(note.cents / 50.0) * width / 2.0f;
		dl.AddLine({ needle, min.y }, { needle, min.y + height }, ImGui::ColorConvertFloat4ToU32(colour), 3.0f);
		ImGui::Dummy({ width, height });
	}
	else {
		ImGui::TextDisabled("no clear note");
		ImGui::SetWindowFontScale(1.0f);
		ImGui::TextDisabled("Noise, silence or several notes at once.");
		ImGui::Dummy({ 0.0f, ImGui::GetFrameHeight() });
	}
	ui::help(tips::pitch);

	ImGui::SameLine();
	export_.button("pitch.png");
	if (job_.busy()) {
		ImGui::SameLine();
		ui::busy("measuring the pitch everywhere");
	}
	if (!ImPlot::BeginPlot("##pitch", { -1.0f, -1.0f }, ImPlotFlags_NoLegend)) return;
	ImPlot::SetupAxes("Time (s)", "Pitch (Hz)");
	ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
	ImPlot::SetupAxisFormat(ImAxis_Y1, plots::hz_formatter);
	ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, signal.duration(), ImPlotCond_Once);
	ImPlot::SetupAxisLimits(ImAxis_Y1, 50.0, 2000.0, ImPlotCond_Once);
	if (overview_.track == ws.current && !overview_.pitch_hz.empty()) {
		ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 2.0f);
		ImPlot::PlotLine("pitch", overview_.pitch_times.data(), overview_.pitch_hz.data(), static_cast<int>(overview_.pitch_hz.size()));
	}
	plots::time_marks(ws);
	plots::hover_marks(ws, true, true);
	double position = ws.playback.position;
	if (ImPlot::DragLineX(0, &position, ImGui::ColorConvertU32ToFloat4(waveform::playhead_color), 2.0f))
		ws.playback.position = std::clamp(position, 0.0, static_cast<double>(signal.duration()));
	if (ImPlot::IsPlotHovered()) ws.next_hover.seconds = ImPlot::GetPlotMousePos().x;
	ImPlot::EndPlot();
	export_.remember_last_item();
}

void AudioWindow::draw_loudness(const Workspace& ws) {
	const sig::Signal& signal = ws.signal();
	const double rate = signal.sample_rate;
	const auto at = static_cast<std::size_t>(std::clamp(ws.playback.position * rate, 0.0, static_cast<double>(signal.frames())));
	// Momentary = the last 0.4 s before the playhead, short-term = the last 3 s.
	const auto window_before = [&](double seconds) {
		const auto length = static_cast<std::size_t>(seconds * rate);
		const std::size_t end = std::max(at, std::min(length, signal.frames()));
		return sig::analysis::loudness(signal, end - std::min(end, length), end);
	};
	const auto momentary = window_before(0.4).momentary_max;
	const auto short_term = window_before(3.0).short_term_max;
	const double nan = std::numeric_limits<double>::quiet_NaN();

	ImGui::SeparatorText("At the playhead");
	meter("LUFS momentary", momentary.value_or(nan), -60.0, 0.0, { { -23.0, "TV" }, { -14.0, "streaming" } });
	ui::help(tips::lufs);
	meter("LUFS short-term", short_term.value_or(nan), -60.0, 0.0, { { -23.0, "TV" }, { -14.0, "streaming" } });

	ImGui::SeparatorText("Whole signal");
	if (overview_.track != ws.current) {
		ui::busy("measuring");
		return;
	}
	const auto& l = overview_.loudness;
	meter("LUFS integrated", l.integrated_lufs.value_or(nan), -60.0, 0.0, { { -23.0, "TV" }, { -14.0, "streaming" } });
	ImGui::Text("Loudest moment: %s LUFS (momentary), %s LUFS (short-term)",
		l.momentary_max ? std::format("{:.1f}", *l.momentary_max).c_str() : "-",
		l.short_term_max ? std::format("{:.1f}", *l.short_term_max).c_str() : "-");
	ImGui::Text("Loudness range: %.1f LU", l.range_lu);
	ui::help("How much the loudness changes between quiet and loud parts. A few LU: very even (pop, speech on radio); "
		"15 LU or more: big contrasts (classical, film).");
	if (l.integrated_lufs) {
		ImGui::Text("To reach -14 LUFS (streaming), change the gain by %+.1f dB.", -14.0 - *l.integrated_lufs);
	}
}

void AudioWindow::draw_levels(const Workspace& ws) {
	const sig::Signal& signal = ws.signal();
	if (overview_.track == ws.current) {
		ImGui::Text("Peak: %.2f dBFS", 20.0 * std::log10(std::max(overview_.peak, 1e-12)));
		ImGui::SameLine(0.0f, ui::em(2.0f));
		if (overview_.clipped > 0)
			ImGui::TextColored({ 1.0f, 0.45f, 0.45f, 1.0f }, "%zu samples at full scale: clipped!", overview_.clipped);
		else
			ImGui::TextColored({ 0.5f, 0.9f, 0.5f, 1.0f }, "No clipping.");
		ui::help(tips::clipping);
	}

	// The values around the playhead (or in the selection), at most a second.
	const TimeRange range = ws.focus(1.0);
	const auto first = static_cast<std::size_t>(range.begin * signal.sample_rate);
	const auto last = std::min(signal.frames(), static_cast<std::size_t>(range.end * signal.sample_rate));
	std::vector<float> values(signal.channels[0].begin() + static_cast<std::ptrdiff_t>(std::min(first, last)),
		signal.channels[0].begin() + static_cast<std::ptrdiff_t>(last));

	ui::hint("How often each sample value occurs. Noise makes a bell curve, a sine piles up at its two extremes, "
		"and clipping shows as spikes at the very edges.");
	if (!ImPlot::BeginPlot("##histogram", { -1.0f, -1.0f }, ImPlotFlags_NoLegend)) return;
	ImPlot::SetupAxes("Sample value", "How often", 0, ImPlotAxisFlags_AutoFit);
	ImPlot::SetupAxisLimits(ImAxis_X1, -1.05, 1.05, ImPlotCond_Once);
	if (!values.empty()) ImPlot::PlotHistogram("values", values.data(), static_cast<int>(values.size()), 101, 1.0, ImPlotRange(-1.01, 1.01));
	ImPlot::EndPlot();
}
