#pragma once

// All signals the app knows about.

#include <algorithm>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "core/peak_index.hpp"
#include "core/signal.hpp"

// A signal plus what the UI precomputes from it once (peak indices, for fast drawing).
//
// Tracks never change after they are made and are shared with std::shared_ptr, so any window can
// keep one, even after it has been removed from the library. This also keeps the peak indices
// valid: they point into the track's own samples, which never move.
class Track {
public:
	Track(sig::Signal signal, bool built_in) : signal_{ std::move(signal) }, built_in_{ built_in } {
		sig::MinMax extremes;
		for (const auto& channel : signal_.channels) extremes.add(peaks_.emplace_back(channel).all());
		peak_ = extremes.peak();
	}

	Track(const Track&) = delete;
	Track& operator=(const Track&) = delete;

	[[nodiscard]] const sig::Signal&             signal() const { return signal_; }
	[[nodiscard]] const std::string&             name() const { return signal_.name; }
	[[nodiscard]] std::span<const sig::PeakIndex> peaks() const { return peaks_; }
	[[nodiscard]] float                          peak() const { return peak_; }        // largest |sample|
	[[nodiscard]] bool                           built_in() const { return built_in_; } // an example, not loaded or made

private:
	sig::Signal                 signal_;
	std::vector<sig::PeakIndex> peaks_;
	float                       peak_ = 0.0f;
	bool                        built_in_;
};

using TrackPtr = std::shared_ptr<const Track>;

[[nodiscard]] inline TrackPtr make_track(sig::Signal signal, bool built_in = false) {
	return std::make_shared<const Track>(std::move(signal), built_in);
}

class Library {
public:
	TrackPtr add(sig::Signal signal, bool built_in = false) { return tracks_.emplace_back(make_track(std::move(signal), built_in)); }
	void     remove(const TrackPtr& track) { std::erase(tracks_, track); }

	[[nodiscard]] std::span<const TrackPtr> tracks() const { return tracks_; }

private:
	std::vector<TrackPtr> tracks_;
};
