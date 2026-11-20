#include "digital_window.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <string>

#include <imgui.h>
#include <imgui_stdlib.h>
#include <implot.h>

#include "plots.hpp"
#include "tips.hpp"
#include "ui.hpp"
#include "core/transforms.hpp"
#include "core/units.hpp"

namespace digital = sig::digital;

const sig::Signal& DigitalWindow::decision_signal(const Workspace& ws) {
	if (scheme_ != digital::Scheme::FSK) {
		return ws.signal();
	}
	if (decision_track_ != ws.current || decision_scheme_ != scheme_ || decision_rate_ != timing_.symbol_rate ||
	    !decision_) {
		decision_track_ = ws.current;
		decision_scheme_ = scheme_;
		decision_rate_ = timing_.symbol_rate;
		decision_ = digital::decision_signal(ws.signal(), scheme_, timing_.symbol_rate);
	}
	return *decision_;
}

void DigitalWindow::draw(Workspace& ws) {
	if (!open) {
		return;
	}
	ImGui::SetNextWindowSize({ 700.0f, 600.0f }, ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(title, &open)) {
		ImGui::End();
		return;
	}

	draw_settings(ws);
	draw_identification(ws);

	const TimeRange                  range = ws.focus(symbols_ / timing_.symbol_rate);
	std::vector<std::complex<float>> symbols;
	if (follow_) {
		recovery_ = digital::recover(ws.signal(), scheme_, timing_.symbol_rate, range.begin, range.end);
		symbols = recovery_.symbols;
		ImGui::TextColored(
			{ 0.5f, 0.9f, 0.5f, 1.0f },
			"Receiver: carrier %s off 0 Hz, symbol clock drifted %.2f samples.",
			sig::format_hz(recovery_.frequency_offset_hz).c_str(),
			recovery_.timing_drift
		);
	}
	else {
		symbols = digital::sample_symbols(decision_signal(ws), timing_, range.begin, range.end);
	}

	if (scheme_ == digital::Scheme::QPSK && !ws.signal().is_iq()) {
		ui::error_text("QPSK needs an I/Q signal.");
	}

	// ---- views ----
	if (ImGui::BeginTabBar("##views")) {
		if (ImGui::BeginTabItem("Constellation")) {
			draw_constellation(ws, range, symbols);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Eye diagram")) {
			draw_eye(decision_signal(ws), range);
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

void DigitalWindow::draw_settings(Workspace& ws) {
	ImGui::SetNextItemWidth(ui::em(8.0f));
	ui::enum_combo("Scheme", scheme_, digital::all_schemes);
	ui::help(tips::scheme);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ui::em(7.0f));
	ImGui::InputDouble("Symbol rate", &timing_.symbol_rate, 0.0, 0.0, "%.1f");
	ui::help(tips::symbol_rate);
	timing_.symbol_rate = std::max(timing_.symbol_rate, 0.001);
	ImGui::SameLine();
	ImGui::BeginDisabled(follow_);
	ImGui::SetNextItemWidth(ui::em(8.0f));
	float offset = static_cast<float>(timing_.offset);
	if (ImGui::SliderFloat("Timing", &offset, 0.0f, 1.0f, "%.2f symbol")) {
		timing_.offset = offset;
	}
	ImGui::EndDisabled();
	ui::help(tips::timing);
	if (!ws.selection) {
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ui::em(6.0f));
		ImGui::SliderInt("Symbols", &symbols_, 10, 2000, "%d", ImGuiSliderFlags_Logarithmic);
	}

	ImGui::SetNextItemWidth(ui::em(9.0f));
	ui::enum_combo("Line coding", line_code_, digital::all_line_codes);
	ui::help(tips::line_coding);
	ImGui::SameLine();
	ImGui::Checkbox("Follow carrier and timing", &follow_);
	ui::help(tips::follow);
	ImGui::SameLine();
	if (ImGui::Button("What is this?")) {
		const TimeRange    range = ws.focus(std::min(1.0, static_cast<double>(ws.signal().duration())));
		const sig::Signal& signal = ws.signal();
		identified_ = sig::analysis::identify(
			signal,
			static_cast<std::size_t>(range.begin * signal.sample_rate),
			static_cast<std::size_t>(range.end * signal.sample_rate)
		);
	}
	ui::explain(tips::identify);
}

void DigitalWindow::draw_identification(Workspace& ws) {
	if (!identified_) {
		ui::hint(
			"Expects baseband (carrier at 0 Hz): use Transform > Frequency shift to bring a carrier down. "
			"Adjust Timing until the constellation dots are tight and the eye is open, or press \"What is this?\"."
		);
		return;
	}
	const auto& id = *identified_;
	ImGui::PushTextWrapPos(0.0f);
	if (id.digital) {
		ImGui::TextColored(
			{ 0.5f, 0.9f, 0.5f, 1.0f },
			"Looks like %s at %.0f baud, %s wide around %s.",
			std::string(digital::name(id.scheme)).c_str(),
			id.symbol_rate,
			sig::format_hz(id.bandwidth_hz).c_str(),
			sig::format_hz(id.center_hz + ws.signal().center_hz).c_str()
		);
	}
	ui::hint(id.reasons.c_str());
	ImGui::PopTextWrapPos();

	if (id.digital && ImGui::SmallButton("Use these settings")) {
		scheme_ = id.scheme;
		timing_.symbol_rate = std::round(id.symbol_rate);
		follow_ = true;
	}
	if (id.digital && id.center_hz != 0.0) {
		ImGui::SameLine();
		if (ImGui::SmallButton(std::format("Shift {} down to 0 Hz", sig::format_hz(id.center_hz)).c_str())) {
			auto shifted = sig::transform::shift_frequency(ws.signal(), -id.center_hz);
			if (shifted) {
				shifted->name = std::format("{} | to 0 Hz", ws.current->name());
				ws.add_and_select(std::move(*shifted));
			}
		}
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Hide")) {
		identified_.reset();
	}
}

void DigitalWindow::draw_constellation(
	const Workspace&                        ws,
	const TimeRange&                        range,
	const std::vector<std::complex<float>>& symbols
) {
	if (scheme_ == digital::Scheme::FSK) {
		ui::hint(
			"FSK has no constellation: it changes the frequency, not the phase. Look at the eye diagram "
			"(of the frequency over time) instead."
		);
		return;
	}
	const sig::Signal& signal = ws.signal();
	ImGui::Checkbox("Show every sample (not only symbol instants)", &all_samples_);
	ui::help(tips::constellation);

	const ImVec2 avail = ImGui::GetContentRegionAvail();
	const float  side = std::max(std::min(avail.x, avail.y), ui::em(10.0f));
	if (!ImPlot::BeginPlot("##constellation", { side, side }, ImPlotFlags_Equal)) {
		return;
	}

	ImPlot::SetupAxes("I", "Q");
	ImPlot::SetupAxesLimits(-1.3, 1.3, -1.3, 1.3, ImPlotCond_Once);

	if (all_samples_ && !follow_) {
		// Every sample in the range, thinned out to at most ~20000 points.
		const auto first = static_cast<std::size_t>(
			std::clamp(range.begin * signal.sample_rate, 0.0, static_cast<double>(signal.frames()))
		);
		const auto last = static_cast<std::size_t>(
			std::clamp(range.end * signal.sample_rate, 0.0, static_cast<double>(signal.frames()))
		);
		const std::size_t  step = std::max<std::size_t>(1, (last - std::min(first, last)) / 20000);
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
	for (const auto& z : symbols) {
		level = scheme_ == digital::Scheme::OOK ? std::max(level, std::abs(z))
		                                        : level + std::abs(z) / static_cast<float>(symbols.size());
	}
	if (level == 0.0f) {
		level = 1.0f;
	}

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
	ImGui::TextDisabled("What is this? (?)");
	ui::explain(tips::eye);
	const bool fsk = scheme_ == digital::Scheme::FSK;
	if (signal.is_iq() && !fsk) {
		ImGui::SameLine(0.0f, ui::em(2.0f));
		ImGui::RadioButton("I", &eye_channel_, 0);
		ImGui::SameLine();
		ImGui::RadioButton("Q", &eye_channel_, 1);
	}
	else {
		eye_channel_ = 0;
	}

	constexpr std::size_t    points = 96;
	const std::vector<float> traces = digital::eye_traces(
		signal,
		static_cast<std::size_t>(eye_channel_),
		timing_,
		range.begin,
		range.end,
		400,
		points
	);

	// All traces as one line, separated by NaN gaps (one plot item instead of hundreds).
	std::vector<float> xs, ys;
	float              largest = 0.0f;
	for (std::size_t start = 0; start < traces.size(); start += points) {
		for (std::size_t p = 0; p < points; ++p) {
			xs.push_back(-1.0f + 2.0f * static_cast<float>(p) / static_cast<float>(points - 1));
			ys.push_back(traces[start + p]);
			largest = std::max(largest, std::abs(traces[start + p]));
		}
		xs.push_back(std::numeric_limits<float>::quiet_NaN());
		ys.push_back(std::numeric_limits<float>::quiet_NaN());
	}

	if (!ImPlot::BeginPlot("##eye", { -1.0f, -1.0f }, ImPlotFlags_NoLegend)) {
		return;
	}
	ImPlot::SetupAxes(
		"Time (symbols, 0 = decision instant)",
		fsk                 ? "Frequency (Hz)"
		: eye_channel_ == 0 ? (signal.is_iq() ? "I" : "Value")
							: "Q"
	);
	const double y = fsk ? std::max(1.0, 1.2 * static_cast<double>(largest)) : 1.3;
	ImPlot::SetupAxesLimits(-1.0, 1.0, -y, y, fsk ? ImPlotCond_Always : ImPlotCond_Once);
	ImPlot::SetNextLineStyle({ 0.4f, 0.8f, 1.0f, 0.25f }, 1.0f);
	ImPlot::PlotLine("traces", xs.data(), ys.data(), static_cast<int>(xs.size()));
	plots::vertical_line(0.0, waveform::playhead_color, 1.0f);
	ImPlot::EndPlot();
}

void DigitalWindow::draw_bits(const std::vector<std::complex<float>>& symbols) {
	std::string bits = digital::decode(scheme_, symbols);
	std::string line = std::format("{} symbols -> {} bits", symbols.size(), bits.size());
	if (line_code_ == digital::LineCode::Manchester) {
		const auto decoded = digital::manchester_decode(bits);
		bits = decoded.bits;
		line += std::format(
			" -> {} data bits after Manchester ({} invalid pairs, shown as ?)",
			bits.size(),
			decoded.errors
		);
	}
	ImGui::TextUnformatted(line.c_str());

	// Sync word: where packets start.
	ImGui::SetNextItemWidth(ui::em(14.0f));
	ImGui::InputTextWithHint("Sync word", "e.g. 10110011 (empty: off)", &sync_);
	ui::help(tips::sync_word);
	const std::string sync = [&] {
		std::string clean;
		for (const char c : sync_) {
			if (c == '0' || c == '1') {
				clean += c;
			}
		}
		return clean;
	}();
	if (!sync.empty()) {
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ui::em(5.0f));
		ImGui::SliderInt("Errors allowed", &sync_errors_, 0, 4);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ui::em(5.0f));
		ImGui::SliderInt("Bytes", &packet_bytes_, 1, 64);
	}

	const auto matches = digital::find_sync(bits, sync, static_cast<std::size_t>(sync_errors_));
	if (!sync.empty()) {
		ImGui::Text("Found %zu time(s).", matches.size());
		if (!matches.empty() &&
		    ImGui::BeginTable(
				"##packets",
				4,
				ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
				{ 0.0f,
		          std::min(
					  ui::em(10.0f),
					  ImGui::GetTextLineHeightWithSpacing() * (static_cast<float>(matches.size()) + 1.5f)
				  ) }
			)) {
			ImGui::TableSetupColumn("At bit");
			ImGui::TableSetupColumn("Hex");
			ImGui::TableSetupColumn("Text");
			ImGui::TableSetupColumn("Note");
			ImGui::TableHeadersRow();
			for (const auto& m : matches) {
				std::string payload = bits.substr(
					std::min(bits.size(), m.position + sync.size()),
					static_cast<std::size_t>(packet_bytes_) * 8
				);
				if (m.inverted) {
					payload = digital::invert(payload);
				}
				ImGui::TableNextColumn();
				ImGui::Text("%zu", m.position);
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(digital::to_hex(payload).c_str());
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(digital::to_text(payload).c_str());
				ImGui::TableNextColumn();
				std::string note = m.inverted ? "inverted (bits flipped back)" : "";
				if (m.errors > 0) {
					note += std::format("{}{} bit error(s) in the sync", note.empty() ? "" : ", ", m.errors);
				}
				ImGui::TextDisabled("%s", note.c_str());
			}
			ImGui::EndTable();
		}
	}

	// All bits in groups of 8, with hex and text next to it.
	ImGui::BeginChild("##bits", { 0.0f, 0.0f }, true);
	std::string grouped;
	for (std::size_t i = 0; i < bits.size(); ++i) {
		if (i > 0 && i % 8 == 0) {
			grouped += ' ';
		}
		grouped += bits[i];
	}
	ImGui::TextWrapped("%s", grouped.c_str());
	ImGui::Spacing();
	ui::hint(("Hex:  " + digital::to_hex(bits)).c_str());
	ui::hint(("Text: " + digital::to_text(bits)).c_str());
	ImGui::EndChild();
}
