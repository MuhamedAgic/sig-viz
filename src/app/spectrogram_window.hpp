#pragma once

#include <cstddef>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

#include <implot.h>

#include "background.hpp"
#include "gpu_image.hpp"
#include "screenshot.hpp"
#include "workspace.hpp"
#include "core/fft.hpp"

// Frequency over time for the whole signal, as an image: every column is the spectrum of a short
// block, its colour the level. Drag the yellow line to move the playhead. Shift + drag marks a band
// (a box of time x frequency) that can be listened to on its own or extracted as a new signal.
//
// Two stages, each redone only when its inputs change:
//   analyse:  the FFTs, into a grid of dB values  (signal, FFT size, window)   on a worker thread
//   colorize: dB -> colours -> GPU image           (dB range, colour map)
class SpectrogramWindow {
public:
	static constexpr const char* title = "Spectrogram";
	bool                         open = true;

	void draw(Workspace& ws);

private:
	struct AnalysisSettings {
		TrackPtr         track;
		int              size_log2 = 10; // FFT size 2^10 = 1024
		sig::fft::Window window = sig::fft::Window::Hann;
		bool             operator==(const AnalysisSettings&) const = default;
	};

	struct ColorSettings {
		float          floor_db = -100.0f;
		float          ceiling_db = 0.0f;
		ImPlotColormap colormap = ImPlotColormap_Viridis;
		bool           operator==(const ColorSettings&) const = default;
	};

	// The analysis result: `columns` blocks along time, `bins` frequencies each; db[column * bins + bin].
	struct Grid {
		std::vector<float> db;
		std::size_t        columns = 0;
		std::size_t        bins = 0;
		double             seconds_per_column = 0.0;
		double             low_hz = 0.0;  // bottom edge of the image (signal frequency, without center_hz)
		double             high_hz = 0.0; // top edge
	};

	// A box of time x frequency (frequencies on the dial, i.e. with center_hz).
	struct Band {
		double begin = 0.0, end = 0.0;
		double low_hz = 0.0, high_hz = 0.0;
	};

	void                                     draw_settings();
	void                                     draw_plot(Workspace& ws);
	void                                     handle_band_picking(const Workspace& ws);
	void                                     draw_band_tools(Workspace& ws);
	[[nodiscard]] static std::optional<Grid> analyse(const AnalysisSettings& settings, std::stop_token stop);
	void                                     colorize();

	AnalysisSettings analysis_settings_, requested_; // requested_: what the grid is (being) computed from
	ColorSettings    color_settings_, colorized_;
	Grid             grid_;
	Background<Grid> analysis_;
	GpuImage         image_;
	bool             refit_ = true;
	bool             refit_when_ready_ = false;

	std::optional<Band>     band_;
	bool                    picking_ = false; // shift + drag in progress
	ImPlotPoint             pick_start_{};
	Background<sig::Signal> band_job_; // extracting or isolating the band
	bool                    band_job_is_listen_ = false;
	std::string             band_error_;

	PlotExport export_;
};
