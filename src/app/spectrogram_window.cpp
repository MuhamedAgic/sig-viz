#include "spectrogram_window.hpp"

#include <algorithm>
#include <array>
#include <string>

#include <imgui.h>

#include "plots.hpp"
#include "ui.hpp"
#include "core/units.hpp"

namespace {

	// Long signals get fewer, more spread out blocks: there is no point in more columns than pixels.
	constexpr std::size_t max_columns = 2048;

	constexpr std::array colormaps{ ImPlotColormap_Viridis, ImPlotColormap_Plasma, ImPlotColormap_Hot, ImPlotColormap_Jet, ImPlotColormap_Greys };

} // namespace

void SpectrogramWindow::draw(Workspace& ws) {
	if (!open) return;
	ImGui::SetNextWindowSize({ 700.0f, 500.0f }, ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(title, &open)) {
		ImGui::End();
		return;
	}

	// ---- settings ----
	ImGui::SetNextItemWidth(ui::em(6.0f));
	if (ImGui::BeginCombo("Size", std::to_string(1 << analysis_settings_.size_log2).c_str())) {
		for (int k = 7; k <= 12; ++k)
			if (ImGui::Selectable(std::to_string(1 << k).c_str(), k == analysis_settings_.size_log2)) analysis_settings_.size_log2 = k;
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ui::em(8.0f));
	ui::enum_combo("Window", analysis_settings_.window, sig::fft::all_windows);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ui::em(6.0f));
	if (ImGui::BeginCombo("Colours", ImPlot::GetColormapName(color_settings_.colormap))) {
		for (const ImPlotColormap map : colormaps)
			if (ImGui::Selectable(ImPlot::GetColormapName(map), map == color_settings_.colormap)) color_settings_.colormap = map;
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ui::em(10.0f));
	ImGui::DragFloatRange2("dB", &color_settings_.floor_db, &color_settings_.ceiling_db, 0.5f, -200.0f, 20.0f, "%.0f", "%.0f");
	ui::hint("Small sizes show fast changes sharply, large sizes separate close frequencies.");

	// ---- (re)build what changed ----
	analysis_settings_.track = ws.current;
	if (!(analysis_settings_ == analysed_)) {
		if (analysis_settings_.track != analysed_.track) refit_ = true;
		analyse();
		colorize();
	}
	else if (!(color_settings_ == colorized_)) {
		colorize();
	}

	// ---- plot ----
	const float bar_width = ui::em(4.5f);
	const double end_time = seconds_per_column_ * static_cast<double>(columns_);
	if (ImPlot::BeginPlot("##spectrogram", { -bar_width - ImGui::GetStyle().ItemSpacing.x, -1.0f }, ImPlotFlags_NoLegend)) {
		const ImPlotCond cond = refit_ ? ImPlotCond_Always : ImPlotCond_Once;
		ImPlot::SetupAxes("Time (s)", "Frequency (Hz)");
		ImPlot::SetupAxisFormat(ImAxis_Y1, plots::hz_formatter);
		ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, end_time, cond);
		ImPlot::SetupAxisLimits(ImAxis_Y1, low_hz_, high_hz_, cond);
		refit_ = false;

		if (image_.valid()) ImPlot::PlotImage("##image", image_.id(), { 0.0, low_hz_ }, { end_time, high_hz_ });
		plots::time_marks(ws);

		double position = ws.playback.position;
		if (ImPlot::DragLineX(0, &position, ImGui::ColorConvertU32ToFloat4(waveform::playhead_color), 2.0f))
			ws.playback.position = std::clamp(position, 0.0, static_cast<double>(ws.signal().duration()));
		ImPlot::EndPlot();
	}
	ImGui::SameLine();
	ImPlot::ColormapScale("dBFS", color_settings_.floor_db, color_settings_.ceiling_db, { bar_width, -1.0f }, "%g", 0, color_settings_.colormap);

	ImGui::End();
}

void SpectrogramWindow::analyse() {
	analysed_ = analysis_settings_;
	const sig::Signal& signal = analysed_.track->signal();

	const std::size_t size = std::size_t{ 1 } << analysed_.size_log2;
	sig::fft::Analyzer analyzer(size, analysed_.window);

	// Blocks overlap by 75 %, unless the signal is so long that they must be spread out.
	const std::size_t hop = std::max(size / 4, (signal.frames() + max_columns - 1) / max_columns);
	columns_ = std::max<std::size_t>(1, (signal.frames() + hop - 1) / hop);
	bins_ = analyzer.bins(signal);
	seconds_per_column_ = static_cast<double>(hop) / signal.sample_rate;

	const double bin_hz = signal.sample_rate / static_cast<double>(size);
	const double first_hz = signal.is_iq() ? -static_cast<double>(size / 2) * bin_hz : 0.0;
	low_hz_ = first_hz - bin_hz / 2.0;
	high_hz_ = first_hz + (static_cast<double>(bins_) - 0.5) * bin_hz;

	db_.assign(columns_ * bins_, 0.0f);
	std::vector<float> power(bins_);
	for (std::size_t c = 0; c < columns_; ++c) {
		std::ranges::fill(power, 0.0f);
		const auto centre = static_cast<std::ptrdiff_t>(c * hop + hop / 2);
		analyzer.accumulate(signal, centre - static_cast<std::ptrdiff_t>(size / 2), power);
		for (std::size_t b = 0; b < bins_; ++b) db_[c * bins_ + b] = static_cast<float>(sig::to_db(power[b]));
	}
}

void SpectrogramWindow::colorize() {
	colorized_ = color_settings_;
	if (columns_ == 0 || bins_ == 0) return;

	// Sample the colour map once instead of for every pixel.
	std::array<ImU32, 256> palette{};
	for (std::size_t i = 0; i < palette.size(); ++i)
		palette[i] = ImGui::ColorConvertFloat4ToU32(ImPlot::SampleColormap(static_cast<float>(i) / 255.0f, colorized_.colormap));

	const float range = std::max(colorized_.ceiling_db - colorized_.floor_db, 1.0f);
	std::vector<ImU32> pixels(columns_ * bins_);
	for (std::size_t c = 0; c < columns_; ++c) {
		for (std::size_t b = 0; b < bins_; ++b) {
			const float level = std::clamp((db_[c * bins_ + b] - colorized_.floor_db) / range, 0.0f, 1.0f);
			const std::size_t row = bins_ - 1 - b; // top row = highest frequency
			pixels[row * columns_ + c] = palette[static_cast<std::size_t>(level * 255.0f)];
		}
	}
	image_.upload(pixels, static_cast<int>(columns_), static_cast<int>(bins_));
}
