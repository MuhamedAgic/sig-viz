#include "digital_window.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

#include <imgui.h>
#include <implot.h>

#include "plots.hpp"
#include "ui.hpp"
#include "core/units.hpp"

namespace digital = sig::digital;

void DigitalWindow::draw(const Workspace& ws) {
	if (!open) return;
	ImGui::SetNextWindowSize({ 700.0f, 600.0f }, ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(title, &open)) {
		ImGui::End();
		return;
	}

	const sig::Signal& signal = ws.signal();

	// ---- settings ----
	ImGui::SetNextItemWidth(ui::em(8.0f));
	ui::enum_combo("Scheme", scheme_, digital::all_schemes);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ui::em(7.0f));
	ImGui::InputDouble("Symbol rate", &timing_.symbol_rate, 0.0, 0.0, "%.1f");
	timing_.symbol_rate = std::max(timing_.symbol_rate, 0.001);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ui::em(8.0f));
	float offset = static_cast<float>(timing_.offset);
	if (ImGui::SliderFloat("Timing", &offset, 0.0f, 1.0f, "%.2f symbol")) timing_.offset = offset;
	if (!ws.selection) {
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ui::em(6.0f));
		ImGui::SliderInt("Symbols", &symbols_, 10, 2000, "%d", ImGuiSliderFlags_Logarithmic);
	}

	const TimeRange range = ws.focus(symbols_ / timing_.symbol_rate);
	const auto symbols = digital::sample_symbols(signal, timing_, range.begin, range.end);

	ui::hint("Expects baseband (carrier at 0 Hz): use Transform > Frequency shift to bring a carrier down. "
		"Adjust Timing until the constellation dots are tight and the eye is open.");
	if (scheme_ == digital::Scheme::QPSK && !signal.is_iq()) ui::error_text("QPSK needs an I/Q signal.");

	// ---- views ----
	if (ImGui::BeginTabBar("##views")) {
		if (ImGui::BeginTabItem("Constellation")) {
			draw_constellation(ws, range, symbols);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Eye diagram")) {
			draw_eye(signal, range);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Bits")) {
			draw_bits(symbols);
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	ImGui::End();
}

void DigitalWindow::draw_constellation(const Workspace& ws, const TimeRange& range, const std::vector<std::complex<float>>& symbols) {
	const sig::Signal& signal = ws.signal();
	ImGui::Checkbox("Show every sample (not only symbol instants)", &all_samples_);

	const ImVec2 avail = ImGui::GetContentRegionAvail();
	const float side = std::max(std::min(avail.x, avail.y), ui::em(10.0f));
	if (!ImPlot::BeginPlot("##constellation", { side, side }, ImPlotFlags_Equal)) return;

	ImPlot::SetupAxes("I", "Q");
	ImPlot::SetupAxesLimits(-1.3, 1.3, -1.3, 1.3, ImPlotCond_Once);

	if (all_samples_) {
		// Every sample in the range, thinned out to at most ~20000 points.
		const auto first = static_cast<std::size_t>(std::clamp(range.begin * signal.sample_rate, 0.0, static_cast<double>(signal.frames())));
		const auto last = static_cast<std::size_t>(std::clamp(range.end * signal.sample_rate, 0.0, static_cast<double>(signal.frames())));
		const std::size_t step = std::max<std::size_t>(1, (last - std::min(first, last)) / 20000);
		std::vector<float> xs, ys;
		for (std::size_t i = first; i < last; i += step) {
			const auto z = signal.complex_at(i);
			xs.push_back(z.real());
			ys.push_back(z.imag());
		}
		ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 1.5f, { 0.4f, 0.7f, 1.0f, 0.35f }, 0.0f);
		ImPlot::PlotScatter("samples", xs.data(), ys.data(), static_cast<int>(xs.size()));
	}

	// The ideal symbols, as targets, scaled to the signal's level: the average symbol magnitude
	// (for OOK the "on" level, i.e. the strongest symbol).
	float level = 0.0f;
	for (const auto& z : symbols)
		level = scheme_ == digital::Scheme::OOK ? std::max(level, std::abs(z)) : level + std::abs(z) / static_cast<float>(symbols.size());
	if (level == 0.0f) level = 1.0f;

	std::vector<float> ideal_x, ideal_y;
	for (int bits = 0; bits < (scheme_ == digital::Scheme::QPSK ? 4 : 2); ++bits) {
		const auto z = digital::symbol(scheme_, bits & 1, bits & 2);
		ideal_x.push_back(static_cast<float>(z.real()) * level);
		ideal_y.push_back(static_cast<float>(z.imag()) * level);
	}
	ImPlot::SetNextMarkerStyle(ImPlotMarker_Cross, 10.0f, {}, 2.0f, { 1.0f, 1.0f, 1.0f, 0.6f });
	ImPlot::PlotScatter("ideal", ideal_x.data(), ideal_y.data(), static_cast<int>(ideal_x.size()));

	std::vector<float> xs, ys;
	for (const auto& z : symbols) {
		xs.push_back(z.real());
		ys.push_back(z.imag());
	}
	ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 3.5f, { 1.0f, 0.8f, 0.0f, 0.9f }, 0.0f);
	ImPlot::PlotScatter("symbols", xs.data(), ys.data(), static_cast<int>(xs.size()));

	ImPlot::EndPlot();
}

void DigitalWindow::draw_eye(const sig::Signal& signal, const TimeRange& range) {
	if (signal.is_iq()) {
		ImGui::RadioButton("I", &eye_channel_, 0);
		ImGui::SameLine();
		ImGui::RadioButton("Q", &eye_channel_, 1);
	}
	else {
		eye_channel_ = 0;
	}

	constexpr std::size_t points = 96;
	const std::vector<float> traces = digital::eye_traces(signal, static_cast<std::size_t>(eye_channel_), timing_, range.begin, range.end, 400, points);

	// All traces as one line, separated by NaN gaps (one plot item instead of hundreds).
	std::vector<float> xs, ys;
	for (std::size_t start = 0; start < traces.size(); start += points) {
		for (std::size_t p = 0; p < points; ++p) {
			xs.push_back(-1.0f + 2.0f * static_cast<float>(p) / static_cast<float>(points - 1));
			ys.push_back(traces[start + p]);
		}
		xs.push_back(std::numeric_limits<float>::quiet_NaN());
		ys.push_back(std::numeric_limits<float>::quiet_NaN());
	}

	if (!ImPlot::BeginPlot("##eye", { -1.0f, -1.0f }, ImPlotFlags_NoLegend)) return;
	ImPlot::SetupAxes("Time (symbols, 0 = decision instant)", eye_channel_ == 0 ? (signal.is_iq() ? "I" : "Value") : "Q");
	ImPlot::SetupAxesLimits(-1.0, 1.0, -1.3, 1.3, ImPlotCond_Once);
	ImPlot::SetNextLineStyle({ 0.4f, 0.8f, 1.0f, 0.25f }, 1.0f);
	ImPlot::PlotLine("traces", xs.data(), ys.data(), static_cast<int>(xs.size()));
	plots::vertical_line(0.0, waveform::playhead_color, 1.0f);
	ImPlot::EndPlot();
}

void DigitalWindow::draw_bits(const std::vector<std::complex<float>>& symbols) const {
	const std::string bits = digital::decode(scheme_, symbols);
	ImGui::Text("%zu symbols -> %zu bits", symbols.size(), bits.size());

	// Groups of 8 for readability.
	std::string grouped;
	for (std::size_t i = 0; i < bits.size(); ++i) {
		if (i > 0 && i % 8 == 0) grouped += ' ';
		grouped += bits[i];
	}
	ImGui::BeginChild("##bits", { 0.0f, 0.0f }, true);
	ImGui::TextWrapped("%s", grouped.c_str());
	ImGui::EndChild();
}
