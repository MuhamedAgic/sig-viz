#include "spectrum_window.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

#include <imgui.h>
#include <implot.h>

#include "plots.hpp"
#include "tips.hpp"
#include "ui.hpp"
#include "core/transforms.hpp"
#include "core/units.hpp"

namespace {

	constexpr std::size_t max_markers = 4;
	constexpr ImVec4      marker_color{ 0.4f, 1.0f, 0.6f, 1.0f };
	constexpr ImVec4      peak_color{ 1.0f, 0.8f, 0.0f, 1.0f };

} // namespace

float SpectrumWindow::Trace::db_at(double hz) const {
	if (db.empty()) {
		return -300.0f;
	}
	const double bin = std::clamp((hz - first_hz) / bin_hz, 0.0, static_cast<double>(db.size() - 1));
	const auto   i = static_cast<std::size_t>(bin);
	if (i + 1 >= db.size()) {
		return db.back();
	}
	return std::lerp(db[i], db[i + 1], static_cast<float>(bin - static_cast<double>(i)));
}

void SpectrumWindow::draw(Workspace& ws) {
	if (!open) {
		return;
	}
	ImGui::SetNextWindowSize({ 700.0f, 500.0f }, ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(title, &open)) {
		ImGui::End();
		return;
	}

	const sig::Signal& signal = ws.signal();
	draw_settings(ws);

	// ---- what to analyse ----
	const std::size_t size = std::size_t{ 1 } << size_log2_;
	const auto        frame_at = [&](double t) {
		return static_cast<std::size_t>(std::clamp(t * signal.sample_rate, 0.0, static_cast<double>(signal.frames())));
	};

	Inputs       inputs{ .track = ws.current, .size = size, .window = window_ };
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
	case Source::Whole: inputs.end = signal.frames(); break;
	}

	if (!same_source(inputs.track, inputs_.track)) {
		refit_ = true;
		markers_.clear();
		frozen_.reset(); // a reference from another signal would be confusing
	}
	if (!(inputs == inputs_)) {
		analyse(ws, inputs);
	}

	// ---- results ----
	draw_plot(ws);
	draw_measurements(signal);
	draw_markers(ws);
	export_.draw();

	ImGui::End();
}

void SpectrumWindow::draw_settings(const Workspace& ws) {
	ImGui::SetNextItemWidth(ui::em(6.0f));
	if (ImGui::BeginCombo("Size", std::to_string(1 << size_log2_).c_str())) {
		for (int k = 8; k <= 16; ++k) {
			if (ImGui::Selectable(std::to_string(1 << k).c_str(), k == size_log2_)) {
				size_log2_ = k;
			}
		}
		ImGui::EndCombo();
	}
	ui::help(tips::fft_size);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ui::em(8.0f));
	ui::enum_combo("Window", window_, sig::fft::all_windows);
	ui::help(tips::window);
	ImGui::SetNextItemWidth(ui::em(11.0f));
	ui::enum_combo("Analyse", source_, std::array{ Source::Playhead, Source::Selection, Source::Whole });
	if (!ws.signal().is_iq()) {
		ImGui::SameLine();
		if (ImGui::Checkbox("Log frequency", &log_frequency_)) {
			refit_ = true;
		}
	}
	ui::help(tips::dbfs);
	if (source_ == Source::Selection && !ws.selection) {
		ImGui::SameLine();
		ImGui::TextDisabled("(nothing selected: shift + drag in the viewer)");
	}

	// The analyser tools.
	if (ImGui::Checkbox("Max hold", &max_hold_)) {
		max_trace_.db.clear();
	}
	ui::help(tips::max_hold);
	ImGui::SameLine();
	if (ImGui::SmallButton("Freeze trace")) {
		frozen_ = trace_;
		frozen_->label = std::format("frozen: {}", ws.current->name());
	}
	ui::explain(tips::freeze);
	if (frozen_) {
		ImGui::SameLine();
		if (ImGui::SmallButton("Clear frozen")) {
			frozen_.reset();
		}
	}
	ImGui::SameLine();
	export_.button("spectrum.png");
}

void SpectrumWindow::analyse(const Workspace& ws, const Inputs& inputs) {
	const sig::Signal& signal = ws.signal();
	const bool         same_axis =
		same_source(inputs.track, inputs_.track) && inputs.size == inputs_.size && inputs.window == inputs_.window;
	inputs_ = inputs;

	if (!analyzer_ || analyzer_->size() != inputs.size || analyzer_->window() != inputs.window) {
		analyzer_.emplace(inputs.size, inputs.window);
	}

	const sig::fft::Spectrum spectrum = analyzer_->average(signal, inputs.begin, inputs.end);
	trace_.first_hz = spectrum.first_hz + signal.center_hz;
	trace_.bin_hz = spectrum.bin_hz;
	trace_.db.resize(spectrum.power.size());
	std::ranges::transform(spectrum.power, trace_.db.begin(), [](float p) {
		return static_cast<float>(sig::to_db(p));
	});

	// Max hold: the highest value per bin since it was switched on (restarts when the bins change).
	if (!same_axis || max_trace_.db.size() != trace_.db.size()) {
		max_trace_.db.clear();
	}
	if (max_hold_) {
		if (max_trace_.db.empty()) {
			max_trace_ = trace_;
		}
		for (std::size_t k = 0; k < trace_.db.size(); ++k) {
			max_trace_.db[k] = std::max(max_trace_.db[k], trace_.db[k]);
		}
	}

	// Levels over the same samples the spectrum saw.
	levels_ = sig::analysis::levels(signal, inputs.begin, inputs.end);
	analysed_frames_ = inputs.end - inputs.begin;
	tone_ = sig::analysis::tone_quality(spectrum, signal.is_iq(), sig::fft::lobe_half_width(inputs.window));
	if (tone_) {
		tone_->fundamental.hz += signal.center_hz;
	}
}

// The highest point within a few bins of `hz`: clicking near a peak puts the marker on top of it.
double SpectrumWindow::snap_to_peak(double hz) const {
	if (trace_.db.empty()) {
		return hz;
	}
	const auto     centre = static_cast<std::ptrdiff_t>(std::lround((hz - trace_.first_hz) / trace_.bin_hz));
	std::ptrdiff_t best = std::clamp<std::ptrdiff_t>(centre, 0, static_cast<std::ptrdiff_t>(trace_.db.size()) - 1);
	for (std::ptrdiff_t k = centre - 8; k <= centre + 8; ++k) {
		if (k < 0 || k >= static_cast<std::ptrdiff_t>(trace_.db.size())) {
			continue;
		}
		if (trace_.db[static_cast<std::size_t>(k)] > trace_.db[static_cast<std::size_t>(best)]) {
			best = k;
		}
	}
	// Between bins: a parabola through the top three (as analysis::strongest does).
	const auto b = static_cast<std::size_t>(best);
	double     shift = 0.0;
	if (b > 0 && b + 1 < trace_.db.size()) {
		const double a = trace_.db[b - 1], m = trace_.db[b], c = trace_.db[b + 1];
		const double denominator = a - 2.0 * m + c;
		if (denominator < 0.0) {
			shift = std::clamp(0.5 * (a - c) / denominator, -0.5, 0.5);
		}
	}
	return trace_.first_hz + (static_cast<double>(b) + shift) * trace_.bin_hz;
}

void SpectrumWindow::draw_plot(Workspace& ws) {
	const sig::Signal& signal = ws.signal();
	const int rows = 2 + (tone_ ? 2 : 0) + std::max(static_cast<int>(markers_.size()), 2); // no markers: a 2-line hint
	const float       table_height = ImGui::GetTextLineHeightWithSpacing() * (static_cast<float>(rows) + 0.5f);
	const ImVec2      size{ -1.0f, std::max(ImGui::GetContentRegionAvail().y - table_height, ui::em(8.0f)) };
	const ImPlotFlags flags =
		ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | (frozen_ || max_hold_ ? 0 : ImPlotFlags_NoLegend);
	if (!ImPlot::BeginPlot("##spectrum", size, flags)) {
		return;
	}

	const bool       log_x = log_frequency_ && !signal.is_iq();
	const double     half_rate = signal.sample_rate / 2.0;
	const double     centre = signal.center_hz;
	const ImPlotCond cond = refit_ ? ImPlotCond_Always : ImPlotCond_Once;

	ImPlot::SetupAxes(centre != 0.0 ? "Frequency (dial)" : "Frequency (Hz)", "dBFS");
	ImPlot::SetupAxisFormat(ImAxis_X1, plots::hz_formatter);
	if (log_x) {
		ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
	}
	ImPlot::SetupAxisLimits(
		ImAxis_X1,
		centre + (signal.is_iq() ? -half_rate : (log_x ? trace_.bin_hz : 0.0)),
		centre + half_rate,
		cond
	);
	ImPlot::SetupAxisLimits(ImAxis_Y1, -140.0, 10.0, cond);
	ImPlot::SetupLegend(ImPlotLocation_NorthEast);
	refit_ = false;

	const std::size_t first = log_x ? 1 : 0; // log axis: skip 0 Hz
	const auto        plot = [&](const char* label, const Trace& trace) {
		if (trace.db.size() <= first) {
			return;
		}
		ImPlot::PlotLine(
			label,
			trace.db.data() + first,
			static_cast<int>(trace.db.size() - first),
			trace.bin_hz,
			trace.first_hz + static_cast<double>(first) * trace.bin_hz
		);
	};

	if (frozen_) {
		ImPlot::SetNextLineStyle({ 0.7f, 0.7f, 0.7f, 0.8f });
		plot(frozen_->label.c_str(), *frozen_);
	}
	if (max_hold_ && !max_trace_.db.empty()) {
		ImPlot::SetNextLineStyle({ 1.0f, 0.45f, 0.45f, 0.7f });
		plot("max hold", max_trace_);
	}
	plot("live", trace_);

	if (tone_) {
		const double x = tone_->fundamental.hz, y = tone_->fundamental.db;
		ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 5.0f, peak_color);
		ImPlot::PlotScatter("##peak", &x, &y, 1);
		ImPlot::Annotation(x, y, peak_color, { 10.0f, -10.0f }, true, "%s  %.1f dB", sig::format_hz(x).c_str(), y);
	}

	for (std::size_t i = 0; i < markers_.size(); ++i) {
		const double x = markers_[i], y = trace_.db_at(x);
		ImPlot::SetNextMarkerStyle(ImPlotMarker_Diamond, 6.0f, marker_color);
		ImPlot::PlotScatter(std::format("##marker{}", i).c_str(), &x, &y, 1);
		ImPlot::Annotation(x, y, marker_color, { 0.0f, -15.0f }, true, "M%zu", i + 1);
	}

	plots::hover_marks(ws, false, false);

	if (ImPlot::IsPlotHovered()) {
		const ImPlotPoint mouse = ImPlot::GetPlotMousePos();
		ws.next_hover.hz = mouse.x;
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
			if (markers_.size() == max_markers) {
				markers_.erase(markers_.begin());
			}
			markers_.push_back(snap_to_peak(mouse.x));
		}
	}
	ImPlot::EndPlot();
	export_.remember_last_item();
}

void SpectrumWindow::draw_measurements(const sig::Signal& signal) const {
	if (!ImGui::BeginTable("##measurements", 4, ImGuiTableFlags_SizingStretchProp)) {
		return;
	}

	// One measurement: its name (hover it for an explanation) and its value. Two per row.
	const auto cell = [](const char* name, const char* tip, const std::string& value) {
		ImGui::TableNextColumn();
		ImGui::TextDisabled("%s", name);
		ui::explain(tip);
		ImGui::TableNextColumn();
		ImGui::TextUnformatted(value.c_str());
	};
	const auto dbfs = [](double amplitude) {
		return std::format("{:.1f} dBFS", 20.0 * std::log10(std::max(amplitude, 1e-12)));
	};

	cell("RMS", tips::rms, dbfs(levels_.rms));
	cell("Peak", tips::peak, dbfs(levels_.peak));
	cell("DC", tips::dc, std::format("{:.4f}", levels_.dc));
	cell("Crest factor", tips::crest, std::format("{:.1f} dB", levels_.crest_db()));

	if (tone_) {
		std::string thd = "-";
		if (tone_->thd_db) {
			thd = std::format("{:.1f} dB ({:.2g} %)", *tone_->thd_db, 100.0 * std::pow(10.0, *tone_->thd_db / 20.0));
		}
		cell(
			"Strongest",
			tips::strongest,
			std::format("{} at {:.1f} dBFS", sig::format_hz(tone_->fundamental.hz), tone_->fundamental.db)
		);
		cell("SNR", tips::snr, std::format("{:.1f} dB", tone_->snr_db));
		cell("THD", tips::thd, thd);
		cell(
			"Analysed",
			"How much of the signal these numbers are based on.",
			std::format(
				"{} samples ({})",
				analysed_frames_,
				sig::format_seconds(static_cast<double>(analysed_frames_) / signal.sample_rate)
			)
		);
	}
	ImGui::EndTable();
}

void SpectrumWindow::draw_markers(Workspace& ws) {
	if (auto band = listen_job_.take()) {
		ws.audition = Audition{ ws.current, make_track(std::move(*band)), listen_label_ };
	}

	if (markers_.empty()) {
		ui::hint("Right-click a peak for a marker: its exact level, the distance to marker 1, and Listen.");
		return;
	}

	for (std::size_t i = 0; i < markers_.size(); ++i) {
		ImGui::PushID(static_cast<int>(i));
		const double hz = markers_[i];
		const float  db = trace_.db_at(hz);
		ImGui::TextColored(marker_color, "M%zu", i + 1);
		ImGui::SameLine();
		std::string text = std::format("{}  {:.1f} dBFS", sig::format_hz(hz), db);
		if (i > 0) {
			text += std::format("    vs M1: {:+.4g} Hz, {:+.1f} dB", hz - markers_[0], db - trace_.db_at(markers_[0]));
		}
		ImGui::TextUnformatted(text.c_str());

		ImGui::SameLine();
		ImGui::BeginDisabled(listen_job_.busy());
		if (ImGui::SmallButton("Listen")) {
			// A narrow band around the marker; at least a few bins wide so a slightly wobbly tone stays in.
			const double half_width = std::max(4.0 * trace_.bin_hz, 15.0);
			const double centre = ws.signal().center_hz;
			listen_label_ = std::format("only {} (+-{})", sig::format_hz(hz), sig::format_hz(half_width));
			listen_job_.start(
				[track = ws.current,
			     low = hz - half_width - centre,
			     high = hz + half_width - centre](std::stop_token) -> std::optional<sig::Signal> {
					auto band = sig::transform::band_pass(track->signal(), low, high);
					return band ? std::optional{ std::move(*band) } : std::nullopt;
				}
			);
			ws.playback.playing = true;
		}
		ImGui::EndDisabled();
		ui::explain("Plays the signal with everything except this frequency filtered away.");
		ImGui::SameLine();
		if (ImGui::SmallButton("x")) {
			markers_.erase(markers_.begin() + static_cast<std::ptrdiff_t>(i));
			ImGui::PopID();
			break;
		}
		ImGui::PopID();
	}
}
