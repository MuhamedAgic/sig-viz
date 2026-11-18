#pragma once

#include <cstddef>
#include <vector>

#include <implot.h>

#include "gpu_image.hpp"
#include "workspace.hpp"
#include "core/fft.hpp"

// Frequency over time for the whole signal, as an image: every column is the spectrum of a short
// block, its colour the level. Drag the yellow line to move the playhead.
//
// Two stages, each redone only when its inputs change:
//   analyse:  the FFTs, into a grid of dB values  (signal, FFT size, window)
//   colorize: dB -> colours -> GPU image           (dB range, colour map)
class SpectrogramWindow {
public:
	static constexpr const char* title = "Spectrogram";
	bool open = true;

	void draw(Workspace& ws);

private:
	struct AnalysisSettings {
		TrackPtr         track;
		int              size_log2 = 10; // FFT size 2^10 = 1024
		sig::fft::Window window = sig::fft::Window::Hann;
		bool operator==(const AnalysisSettings&) const = default;
	};

	struct ColorSettings {
		float          floor_db = -100.0f;
		float          ceiling_db = 0.0f;
		ImPlotColormap colormap = ImPlotColormap_Viridis;
		bool operator==(const ColorSettings&) const = default;
	};

	void analyse();
	void colorize();

	AnalysisSettings analysis_settings_, analysed_;
	ColorSettings    color_settings_, colorized_;

	// The grid: columns_ blocks along time, bins_ frequencies each; db_[column * bins_ + bin].
	std::vector<float> db_;
	std::size_t        columns_ = 0;
	std::size_t        bins_ = 0;
	double             seconds_per_column_ = 0.0;
	double             low_hz_ = 0.0;  // bottom edge of the image
	double             high_hz_ = 0.0; // top edge

	GpuImage image_;
	bool     refit_ = true;
};
