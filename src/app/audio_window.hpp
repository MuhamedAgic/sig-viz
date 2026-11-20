#pragma once

#include <cstddef>
#include <vector>

#include "background.hpp"
#include "screenshot.hpp"
#include "workspace.hpp"
#include "core/audio_analysis.hpp"

// Measurements for sound:
//   Tuner:    the note at the playhead (like a guitar tuner) and the pitch over the whole signal
//   Loudness: LUFS meters, as used by broadcasters and streaming services
//   Levels:   how the sample values are spread (a histogram), and whether the recording clipped
class AudioWindow {
public:
	static constexpr const char* title = "Audio";
	bool                         open = true;

	void draw(Workspace& ws);

private:
	// Computed once per signal, in the background.
	struct Overview {
		TrackPtr                track;
		std::vector<double>     pitch_times;
		std::vector<double>     pitch_hz; // NaN where there is no clear note
		sig::analysis::Loudness loudness;
		std::size_t             clipped = 0;
		double                  peak = 0.0;
	};

	void                          draw_tuner(Workspace& ws);
	void                          draw_loudness(const Workspace& ws);
	void                          draw_levels(const Workspace& ws);
	[[nodiscard]] static Overview analyse(const TrackPtr& track, std::stop_token stop);

	TrackPtr             requested_;
	Overview             overview_;
	Background<Overview> job_;
	PlotExport           export_;
};
