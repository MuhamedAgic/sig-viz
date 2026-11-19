#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "background.hpp"
#include "screenshot.hpp"
#include "workspace.hpp"
#include "core/analysis.hpp"
#include "core/fft.hpp"

// Frequency content of the viewer's signal (live around the playhead, of the selection or of the
// whole signal), plus level and tone measurements. Like a lab spectrum analyser it has markers
// (right-click), a max-hold trace and a frozen reference trace to compare against.
class SpectrumWindow {
public:
	static constexpr const char* title = "Spectrum";
	bool open = true;

	void draw(Workspace& ws);

private:
	enum class Source { Playhead, Selection, Whole }; // which part of the signal is analysed

	friend constexpr std::string_view name(Source source) {
		switch (source) {
		case Source::Playhead:  return "Around the playhead";
		case Source::Selection: return "Selection";
		case Source::Whole:     return "Whole signal";
		}
		std::unreachable();
	}

	// What the cached results were computed from; when it changes, they are recomputed.
	struct Inputs {
		TrackPtr         track;
		std::size_t      begin = 0;
		std::size_t      end = 0;
		std::size_t      size = 0;
		sig::fft::Window window = sig::fft::Window::Hann;
		bool operator==(const Inputs&) const = default;
	};

	// A trace to plot: dB per bin, from `first_hz` (on the dial) every `bin_hz`.
	struct Trace {
		std::vector<float> db;
		double             first_hz = 0.0;
		double             bin_hz = 1.0;
		std::string        label;

		[[nodiscard]] float db_at(double hz) const; // interpolated between bins
	};

	void draw_settings(const Workspace& ws);
	void analyse(const Workspace& ws, const Inputs& inputs);
	void draw_plot(Workspace& ws);
	void draw_measurements(const sig::Signal& signal) const;
	void draw_markers(Workspace& ws);
	[[nodiscard]] double snap_to_peak(double hz) const;

	// Settings
	int              size_log2_ = 12; // FFT size 2^12 = 4096
	sig::fft::Window window_ = sig::fft::Window::Blackman;
	Source           source_ = Source::Playhead;
	bool             log_frequency_ = false;
	bool             max_hold_ = false;

	// Results
	Inputs                                    inputs_;
	std::optional<sig::fft::Analyzer>         analyzer_;
	Trace                                     trace_;     // the live spectrum
	Trace                                     max_trace_; // highest level seen per bin (max hold)
	std::optional<Trace>                      frozen_;    // a copy to compare against
	sig::analysis::Levels                     levels_;
	std::optional<sig::analysis::ToneQuality> tone_;
	std::size_t                               analysed_frames_ = 0;
	bool                                      refit_ = true; // reset the axes on the next frame

	// Markers: frequencies on the dial. Right-click places one on the nearest peak.
	std::vector<double>     markers_;
	Background<sig::Signal> listen_job_;
	std::string             listen_label_;

	PlotExport export_;
};
