#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "workspace.hpp"
#include "core/analysis.hpp"
#include "core/fft.hpp"

// Frequency content of the viewer's signal (live around the playhead, of the selection or of the
// whole signal), plus level and tone measurements.
class SpectrumWindow {
public:
	static constexpr const char* title = "Spectrum";
	bool open = true;

	void draw(const Workspace& ws);

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

	void analyse(const Workspace& ws, const Inputs& inputs);
	void draw_plot(const sig::Signal& signal);
	void draw_measurements(const sig::Signal& signal) const;

	// Settings
	int              size_log2_ = 12; // FFT size 2^12 = 4096
	sig::fft::Window window_ = sig::fft::Window::Blackman;
	Source           source_ = Source::Playhead;
	bool             log_frequency_ = false;

	// Results
	Inputs                                      inputs_;
	std::optional<sig::fft::Analyzer>           analyzer_;
	sig::fft::Spectrum                          spectrum_;
	std::vector<float>                          db_; // spectrum_.power in dBFS, for plotting
	sig::analysis::Levels                       levels_;
	std::optional<sig::analysis::ToneQuality>   tone_;
	std::size_t                                 analysed_frames_ = 0;
	bool                                        refit_ = true; // reset the axes on the next frame
};
