#include "spectrum_window.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

#include <imgui.h>
#include <implot.h>

#include "plots.hpp"
#include "ui.hpp"
#include "core/units.hpp"

void SpectrumWindow::draw(const Workspace& ws) {
	if (!open) return;
	ImGui::SetNextWindowSize({ 700.0f, 500.0f }, ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(title, &open)) {
		ImGui::End();
		return;
	}

	const sig::Signal& signal = ws.signal();

	// ---- settings ----
	ImGui::SetNextItemWidth(ui::em(6.0f));
	if (ImGui::BeginCombo("Size", std::to_string(1 << size_log2_).c_str())) {
		for (int k = 8; k <= 16; ++k)
			if (ImGui::Selectable(std::to_string(1 << k).c_str(), k == size_log2_)) size_log2_ = k;
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ui::em(8.0f));
	ui::enum_combo("Window", window_, sig::fft::all_windows);
	ImGui::SetItemTooltip("Hann: sharp peaks.  Blackman: good all-rounder.  Flat top: exact levels.\n"
		"The window's leakage limits the SNR it can measure: about 55 / 70 / 90 dB.");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ui::em(11.0f));
	ui::enum_combo("Analyse", source_, std::array{ Source::Playhead, Source::Selection, Source::Whole });
	if (!signal.is_iq()) {
		ImGui::SameLine();
		if (ImGui::Checkbox("Log frequency", &log_frequency_)) refit_ = true;
	}

	// ---- what to analyse ----
	const std::size_t size = std::size_t{ 1 } << size_log2_;
	const auto frame_at = [&](double t) {
		return static_cast<std::size_t>(std::clamp(t * signal.sample_rate, 0.0, static_cast<double>(signal.frames())));
	};

	Inputs inputs{ .track = ws.current, .size = size, .window = window_ };
	const Source source = (source_ == Source::Selection && !ws.selection) ? Source::Playhead : source_;
	switch (source) {
	case Source::Playhead: {
		// One block centred on the playhead, slid inwards near the ends so it never reaches past
		// the signal: silence in the block would lower the level and smear the spectrum.
		const std::size_t centre = frame_at(ws.playback.position);
		const std::size_t latest_start = signal.frames() > size ? signal.frames() - size : 0;
		inputs.begin = std::min(centre > size / 2 ? centre - size / 2 : 0, latest_start);
		inputs.end = std::min(inputs.begin + size, signal.frames());
		break;
	}
	case Source::Selection:
		inputs.begin = frame_at(ws.selection->begin);
		inputs.end = frame_at(ws.selection->end);
		break;
	case Source::Whole:
		inputs.end = signal.frames();
		break;
	}
	if (source_ == Source::Selection && !ws.selection) {
		ImGui::SameLine();
		ImGui::TextDisabled("(nothing selected: shift + drag in the viewer)");
	}

	if (inputs.track != inputs_.track) refit_ = true;
	if (!(inputs == inputs_)) analyse(ws, inputs);

	// ---- results ----
	draw_plot(signal);
	draw_measurements(signal);

	ImGui::End();
}

void SpectrumWindow::analyse(const Workspace& ws, const Inputs& inputs) {
	const sig::Signal& signal = ws.signal();
	inputs_ = inputs;

	if (!analyzer_ || analyzer_->size() != inputs.size || analyzer_->window() != inputs.window)
		analyzer_.emplace(inputs.size, inputs.window);

	spectrum_ = analyzer_->average(signal, inputs.begin, inputs.end);
	db_.resize(spectrum_.power.size());
	std::ranges::transform(spectrum_.power, db_.begin(), [](float p) { return static_cast<float>(sig::to_db(p)); });

	// Levels over the same samples the spectrum saw.
	levels_ = sig::analysis::levels(signal, inputs.begin, inputs.end);
	analysed_frames_ = inputs.end - inputs.begin;
	tone_ = sig::analysis::tone_quality(spectrum_, signal.is_iq(), sig::fft::lobe_half_width(inputs.window));
}

void SpectrumWindow::draw_plot(const sig::Signal& signal) {
	const float table_height = ImGui::GetTextLineHeightWithSpacing() * 4.5f;
	const ImVec2 size{ -1.0f, std::max(ImGui::GetContentRegionAvail().y - table_height, ui::em(8.0f)) };
	if (!ImPlot::BeginPlot("##spectrum", size, ImPlotFlags_NoLegend)) return;

	const bool log_x = log_frequency_ && !signal.is_iq();
	const double half_rate = signal.sample_rate / 2.0;
	const ImPlotCond cond = refit_ ? ImPlotCond_Always : ImPlotCond_Once;

	ImPlot::SetupAxes("Frequency (Hz)", "dBFS");
	ImPlot::SetupAxisFormat(ImAxis_X1, plots::hz_formatter);
	if (log_x) ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
	ImPlot::SetupAxisLimits(ImAxis_X1, signal.is_iq() ? -half_rate : (log_x ? spectrum_.bin_hz : 0.0), half_rate, cond);
	ImPlot::SetupAxisLimits(ImAxis_Y1, -140.0, 10.0, cond);
	refit_ = false;

	const std::size_t first = log_x ? 1 : 0; // log axis: skip 0 Hz
	if (db_.size() > first)
		ImPlot::PlotLine("##power", db_.data() + first, static_cast<int>(db_.size() - first), spectrum_.bin_hz, spectrum_.hz(static_cast<double>(first)));

	if (tone_) {
		const double x = tone_->fundamental.hz, y = tone_->fundamental.db;
		ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 5.0f, { 1.0f, 0.8f, 0.0f, 1.0f });
		ImPlot::PlotScatter("##peak", &x, &y, 1);
		ImPlot::Annotation(x, y, { 1.0f, 0.8f, 0.0f, 1.0f }, { 10.0f, -10.0f }, true, "%s  %.1f dB", sig::format_hz(x).c_str(), y);
	}
	ImPlot::EndPlot();
}

void SpectrumWindow::draw_measurements(const sig::Signal& signal) const {
	if (!ImGui::BeginTable("##measurements", 4, ImGuiTableFlags_SizingStretchProp)) return;

	const auto row = [](const char* label_a, const std::string& value_a, const char* label_b, const std::string& value_b) {
		ImGui::TableNextRow();
		ImGui::TableNextColumn(); ImGui::TextDisabled("%s", label_a);
		ImGui::TableNextColumn(); ImGui::TextUnformatted(value_a.c_str());
		ImGui::TableNextColumn(); ImGui::TextDisabled("%s", label_b);
		ImGui::TableNextColumn(); ImGui::TextUnformatted(value_b.c_str());
	};
	const auto dbfs = [](double amplitude) { return std::format("{:.1f} dBFS", 20.0 * std::log10(std::max(amplitude, 1e-12))); };

	row("RMS", dbfs(levels_.rms), "Peak", dbfs(levels_.peak));
	row("DC", std::format("{:.4f}", levels_.dc), "Crest factor", std::format("{:.1f} dB", levels_.crest_db()));

	if (tone_) {
		std::string thd = "-";
		if (tone_->thd_db) thd = std::format("{:.1f} dB ({:.2g} %)", *tone_->thd_db, 100.0 * std::pow(10.0, *tone_->thd_db / 20.0));
		row("Strongest", std::format("{} at {:.1f} dBFS", sig::format_hz(tone_->fundamental.hz), tone_->fundamental.db),
			"SNR", std::format("{:.1f} dB", tone_->snr_db));
		row("THD", thd, "Analysed", std::format("{} samples ({})", analysed_frames_,
			sig::format_seconds(static_cast<double>(analysed_frames_) / signal.sample_rate)));
	}
	ImGui::EndTable();
}
