#include "spectrogram_window.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <string>

#include <imgui.h>

#include "plots.hpp"
#include "tips.hpp"
#include "ui.hpp"
#include "core/transforms.hpp"
#include "core/units.hpp"

namespace {

	// Long signals get fewer, more spread out blocks: there is no point in more columns than pixels.
	constexpr std::size_t max_columns = 2048;

	constexpr std::array colormaps{ ImPlotColormap_Viridis,
		                            ImPlotColormap_Plasma,
		                            ImPlotColormap_Hot,
		                            ImPlotColormap_Jet,
		                            ImPlotColormap_Greys };

	constexpr ImVec4 band_color{ 1.0f, 1.0f, 1.0f, 0.9f };

} // namespace

void SpectrogramWindow::draw(Workspace& ws) {
	if (!open) {
		return;
	}
	ImGui::SetNextWindowSize({ 700.0f, 500.0f }, ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(title, &open)) {
		ImGui::End();
		return;
	}

	draw_settings();

	// ---- (re)build what changed ----
	analysis_settings_.track = ws.current;
	if (!(analysis_settings_ == requested_)) {
		if (!same_source(analysis_settings_.track, requested_.track)) {
			refit_when_ready_ = true; // once the new grid is there (the old one shows until then)
			band_.reset();
			band_error_.clear();
		}
		requested_ = analysis_settings_;
		analysis_.start([settings = requested_](std::stop_token stop) { return analyse(settings, stop); });
	}
	if (auto grid = analysis_.take()) {
		grid_ = std::move(*grid);
		colorize();
		refit_ = refit_ || refit_when_ready_;
		refit_when_ready_ = false;
	}
	else if (!(color_settings_ == colorized_)) {
		colorize();
	}

	draw_band_tools(ws);
	draw_plot(ws);
	export_.draw();

	ImGui::End();
}

void SpectrogramWindow::draw_settings() {
	ImGui::SetNextItemWidth(ui::em(6.0f));
	if (ImGui::BeginCombo("Size", std::to_string(1 << analysis_settings_.size_log2).c_str())) {
		for (int k = 7; k <= 12; ++k) {
			if (ImGui::Selectable(std::to_string(1 << k).c_str(), k == analysis_settings_.size_log2)) {
				analysis_settings_.size_log2 = k;
			}
		}
		ImGui::EndCombo();
	}
	ui::help(tips::spectrogram_size);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ui::em(8.0f));
	ui::enum_combo("Window", analysis_settings_.window, sig::fft::all_windows);
	ui::help(tips::window);
	ImGui::SetNextItemWidth(ui::em(6.0f));
	if (ImGui::BeginCombo("Colours", ImPlot::GetColormapName(color_settings_.colormap))) {
		for (const ImPlotColormap map : colormaps) {
			if (ImGui::Selectable(ImPlot::GetColormapName(map), map == color_settings_.colormap)) {
				color_settings_.colormap = map;
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ui::em(10.0f));
	ImGui::DragFloatRange2(
		"dB",
		&color_settings_.floor_db,
		&color_settings_.ceiling_db,
		0.5f,
		-200.0f,
		20.0f,
		"%.0f",
		"%.0f"
	);
	ui::help(tips::db_range);
	ImGui::SameLine();
	export_.button("spectrogram.png");
	if (analysis_.busy()) {
		ImGui::SameLine();
		ui::busy("computing");
	}
}

// ---- band: shift + drag ---------------------------------------------------------------------------------

void SpectrogramWindow::draw_band_tools(Workspace& ws) {
	if (auto result = band_job_.take()) {
		if (band_job_is_listen_) {
			ws.audition =
				Audition{ ws.current,
				          make_track(std::move(*result)),
				          std::format("only {} - {}", sig::format_hz(band_->low_hz), sig::format_hz(band_->high_hz)) };
		}
		else {
			ws.add_and_select(std::move(*result));
		}
	}

	if (!band_) {
		ui::hint(
			"Time runs to the right, frequency goes up, brighter = stronger. Drag the yellow line to move the playhead; "
			"shift + drag to mark a band (one station, one note) to listen to or extract."
		);
		return;
	}

	ImGui::Text(
		"Band: %s - %s, %s - %s",
		sig::format_hz(band_->low_hz).c_str(),
		sig::format_hz(band_->high_hz).c_str(),
		sig::format_seconds(band_->begin).c_str(),
		sig::format_seconds(band_->end).c_str()
	);

	const sig::Signal& signal = ws.signal();
	const double       centre = signal.center_hz; // the band is on the dial; transforms want signal frequencies
	ImGui::BeginDisabled(band_job_.busy());
	ImGui::SameLine();
	if (ImGui::SmallButton("Listen")) {
		band_job_is_listen_ = true;
		band_job_.start([track = ws.current, band = *band_, centre](std::stop_token) -> std::optional<sig::Signal> {
			auto result = sig::transform::band_pass(track->signal(), band.low_hz - centre, band.high_hz - centre);
			return result ? std::optional{ std::move(*result) } : std::nullopt;
		});
	}
	ui::explain("Play the whole signal with only this band of frequencies, so you hear what is in it.");
	ImGui::SameLine();
	if (ImGui::SmallButton("Extract as new signal")) {
		band_job_is_listen_ = false;
		band_job_.start([track = ws.current, band = *band_, centre](std::stop_token) -> std::optional<sig::Signal> {
			auto result = sig::transform::extract_band(
				track->signal(),
				band.begin,
				band.end,
				band.low_hz - centre,
				band.high_hz - centre
			);
			if (!result) {
				return std::nullopt;
			}
			result->name = std::format(
				"{} | band {}",
				track->name(),
				sig::format_hz(band.low_hz + (band.high_hz - band.low_hz) / 2)
			);
			result->description = std::format(
				"{} - {} out of {}, moved to 0 Hz (I/Q).",
				sig::format_hz(band.low_hz),
				sig::format_hz(band.high_hz),
				track->name()
			);
			return std::optional{ std::move(*result) };
		});
	}
	ui::explain(
		"Cuts out this box as a new I/Q signal: the band moved to 0 Hz, everything else filtered away, at a "
		"sample rate that just fits. Like tuning a radio to one station."
	);
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::SmallButton("Clear")) {
		band_.reset();
		if (ws.audition && ws.audition->source == ws.current) {
			ws.audition.reset();
		}
	}
	if (band_job_.busy()) {
		ImGui::SameLine();
		ui::busy("working");
	}
}

void SpectrogramWindow::handle_band_picking(const Workspace& ws) {
	const ImPlotPoint mouse = ImPlot::GetPlotMousePos();
	const double      duration = ws.signal().duration();
	if (ImPlot::IsPlotHovered() && ImGui::GetIO().KeyShift && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		picking_ = true;
		const double t = std::clamp(mouse.x, 0.0, duration);
		band_ = Band{ t, t, mouse.y, mouse.y };
		pick_start_ = { t, mouse.y };
	}
	if (picking_) {
		const double t = std::clamp(mouse.x, 0.0, duration);
		band_ = Band{ std::min(t, pick_start_.x),
			          std::max(t, pick_start_.x),
			          std::min(mouse.y, pick_start_.y),
			          std::max(mouse.y, pick_start_.y) };
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			picking_ = false;
			if (band_->end - band_->begin <= 0.0 || band_->high_hz - band_->low_hz <= 0.0) {
				band_.reset(); // a click, not a box
			}
		}
	}
}

// ---- plot -----------------------------------------------------------------------------------------------

void SpectrogramWindow::draw_plot(Workspace& ws) {
	const sig::Signal& signal = ws.signal();
	const double       centre = signal.center_hz;
	const float        bar_width = ui::em(4.5f);
	const double       end_time = grid_.seconds_per_column * static_cast<double>(grid_.columns);

	// While shift is held (or a band is being drawn), the mouse draws instead of panning.
	const bool drawing =
		picking_ || (ImGui::GetIO().KeyShift && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows));
	const ImPlotAxisFlags lock = drawing ? ImPlotAxisFlags_Lock : ImPlotAxisFlags_None;

	if (ImPlot::BeginPlot(
			"##spectrogram",
			{ -bar_width - ImGui::GetStyle().ItemSpacing.x, -1.0f },
			ImPlotFlags_NoLegend
		)) {
		const ImPlotCond cond = refit_ ? ImPlotCond_Always : ImPlotCond_Once;
		ImPlot::SetupAxes("Time (s)", centre != 0.0 ? "Frequency (dial)" : "Frequency (Hz)", lock, lock);
		ImPlot::SetupAxisFormat(ImAxis_Y1, plots::hz_formatter);
		ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, end_time, cond);
		ImPlot::SetupAxisLimits(ImAxis_Y1, grid_.low_hz + centre, grid_.high_hz + centre, cond);
		refit_ = false;

		if (image_.valid()) {
			ImPlot::PlotImage(
				"##image",
				image_.id(),
				{ 0.0, grid_.low_hz + centre },
				{ end_time, grid_.high_hz + centre }
			);
		}
		plots::time_marks(ws);
		plots::hover_marks(ws, true, true);

		handle_band_picking(ws);
		if (band_ && !picking_) {
			ImPlot::DragRect(0, &band_->begin, &band_->low_hz, &band_->end, &band_->high_hz, band_color);
		}
		else if (band_) {
			plots::rectangle(
				band_->begin,
				band_->low_hz,
				band_->end,
				band_->high_hz,
				ImGui::ColorConvertFloat4ToU32(band_color)
			);
		}

		double position = ws.playback.position;
		if (ImPlot::DragLineX(1, &position, ImGui::ColorConvertU32ToFloat4(waveform::playhead_color), 2.0f)) {
			ws.playback.position = std::clamp(position, 0.0, static_cast<double>(signal.duration()));
		}

		if (ImPlot::IsPlotHovered()) {
			const ImPlotPoint mouse = ImPlot::GetPlotMousePos();
			ws.next_hover = { .seconds = mouse.x, .hz = mouse.y };
		}
		ImPlot::EndPlot();
		export_.remember_last_item();
	}
	ImGui::SameLine();
	ImPlot::ColormapScale(
		"dBFS",
		color_settings_.floor_db,
		color_settings_.ceiling_db,
		{ bar_width, -1.0f },
		"%g",
		0,
		color_settings_.colormap
	);
}

// ---- analysis ---------------------------------------------------------------------------------------------

std::optional<SpectrogramWindow::Grid> SpectrogramWindow::analyse(
	const AnalysisSettings& settings,
	std::stop_token         stop
) {
	const sig::Signal& signal = settings.track->signal();
	const std::size_t  size = std::size_t{ 1 } << settings.size_log2;
	sig::fft::Analyzer analyzer(size, settings.window);

	Grid grid;
	// Blocks overlap by 75 %, unless the signal is so long that they must be spread out.
	const std::size_t hop = std::max(size / 4, (signal.frames() + max_columns - 1) / max_columns);
	grid.columns = std::max<std::size_t>(1, (signal.frames() + hop - 1) / hop);
	grid.bins = analyzer.bins(signal);
	grid.seconds_per_column = static_cast<double>(hop) / signal.sample_rate;

	const double bin_hz = signal.sample_rate / static_cast<double>(size);
	const double first_hz = signal.is_iq() ? -static_cast<double>(size / 2) * bin_hz : 0.0;
	grid.low_hz = first_hz - bin_hz / 2.0;
	grid.high_hz = first_hz + (static_cast<double>(grid.bins) - 0.5) * bin_hz;

	grid.db.assign(grid.columns * grid.bins, 0.0f);
	std::vector<float> power(grid.bins);
	for (std::size_t c = 0; c < grid.columns; ++c) {
		if (stop.stop_requested()) {
			return std::nullopt;
		}
		std::ranges::fill(power, 0.0f);
		const auto centre = static_cast<std::ptrdiff_t>(c * hop + hop / 2);
		analyzer.accumulate(signal, centre - static_cast<std::ptrdiff_t>(size / 2), power);
		for (std::size_t b = 0; b < grid.bins; ++b) {
			grid.db[c * grid.bins + b] = static_cast<float>(sig::to_db(power[b]));
		}
	}
	return grid;
}

void SpectrogramWindow::colorize() {
	colorized_ = color_settings_;
	const std::size_t columns = grid_.columns, bins = grid_.bins;
	if (columns == 0 || bins == 0) {
		return;
	}

	// Sample the colour map once instead of for every pixel.
	std::array<ImU32, 256> palette{};
	for (std::size_t i = 0; i < palette.size(); ++i) {
		palette[i] =
			ImGui::ColorConvertFloat4ToU32(ImPlot::SampleColormap(static_cast<float>(i) / 255.0f, colorized_.colormap));
	}

	const float        range = std::max(colorized_.ceiling_db - colorized_.floor_db, 1.0f);
	std::vector<ImU32> pixels(columns * bins);
	for (std::size_t c = 0; c < columns; ++c) {
		for (std::size_t b = 0; b < bins; ++b) {
			const float       level = std::clamp((grid_.db[c * bins + b] - colorized_.floor_db) / range, 0.0f, 1.0f);
			const std::size_t row = bins - 1 - b; // top row = highest frequency
			pixels[row * columns + c] = palette[static_cast<std::size_t>(level * 255.0f)];
		}
	}
	image_.upload(pixels, static_cast<int>(columns), static_cast<int>(bins));
}
