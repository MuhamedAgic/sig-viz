#pragma once

// All signals the app knows about.

#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/peak_index.hpp"
#include "core/recipe.hpp"
#include "core/signal.hpp"

class Track;
using TrackPtr = std::shared_ptr<const Track>;

// Where a signal came from, when the Transform window made it: the signal it was made from (parent)
// and all steps from the original (not just the last one), so the whole chain can be re-run.
struct Lineage {
	TrackPtr                       parent;
	std::vector<sig::recipe::Step> steps;
};

// A signal plus what the UI precomputes from it once (peak indices, for fast drawing).
//
// Tracks never change after they are made and are shared with std::shared_ptr, so any window can
// keep one, even after it has been removed from the library. This also keeps the peak indices
// valid: they point into the track's own samples, which never move.
class Track {
public:
	Track(sig::Signal signal, bool built_in, Lineage lineage = {}, std::uint64_t live = 0)
		: signal_{ std::move(signal) }
		, built_in_{ built_in }
		, lineage_{ std::move(lineage) }
		, live_{ live } {
		sig::MinMax extremes;
		for (const auto& channel : signal_.channels) {
			extremes.add(peaks_.emplace_back(channel).all());
		}
		peak_ = extremes.peak();
	}

	Track(const Track&) = delete;
	Track& operator=(const Track&) = delete;

	[[nodiscard]] const sig::Signal& signal() const {
		return signal_;
	}
	[[nodiscard]] const std::string& name() const {
		return signal_.name;
	}
	[[nodiscard]] std::span<const sig::PeakIndex> peaks() const {
		return peaks_;
	}
	[[nodiscard]] float peak() const {
		return peak_;
	} // largest |sample|
	[[nodiscard]] bool built_in() const {
		return built_in_;
	} // an example, not loaded or made
	[[nodiscard]] const Lineage& lineage() const {
		return lineage_;
	}
	// Live input makes a new track for every update; they all share this number (0 = not live).
	[[nodiscard]] std::uint64_t live() const {
		return live_;
	}

	// The original this was made from (itself if it was not made by a recipe).
	[[nodiscard]] const Track& origin() const {
		return lineage_.parent ? lineage_.parent->origin() : *this;
	}

	// The signal as a std::shared_ptr<const Signal> that keeps this track alive (for recipe steps).
	[[nodiscard]] static std::shared_ptr<const sig::Signal> shared_signal(const TrackPtr& track) {
		return { track, &track->signal_ };
	}

private:
	sig::Signal                 signal_;
	std::vector<sig::PeakIndex> peaks_;
	float                       peak_ = 0.0f;
	bool                        built_in_;
	Lineage                     lineage_;
	std::uint64_t               live_ = 0;
};

[[nodiscard]] inline TrackPtr make_track(
	sig::Signal   signal,
	bool          built_in = false,
	Lineage       lineage = {},
	std::uint64_t live = 0
) {
	return std::make_shared<const Track>(std::move(signal), built_in, std::move(lineage), live);
}

// The same signal, or two updates of the same live input: windows keep their zoom, markers, ...
[[nodiscard]] inline bool same_source(const TrackPtr& a, const TrackPtr& b) {
	return a == b || (a && b && a->live() != 0 && a->live() == b->live());
}

class Library {
public:
	TrackPtr add(sig::Signal signal, bool built_in = false, Lineage lineage = {}) {
		return tracks_.emplace_back(make_track(std::move(signal), built_in, std::move(lineage)));
	}
	TrackPtr add(TrackPtr track) {
		return tracks_.emplace_back(std::move(track));
	}

	// Swaps a track for a newer version of it, in the same place (live input).
	void replace(const TrackPtr& old, TrackPtr track) {
		const auto it = std::ranges::find(tracks_, old);
		if (it != tracks_.end()) {
			*it = std::move(track);
		}
		else {
			tracks_.push_back(std::move(track));
		}
	}

	[[nodiscard]] TrackPtr find(std::string_view name) const {
		const auto it = std::ranges::find_if(tracks_, [&](const TrackPtr& t) { return t->name() == name; });
		return it != tracks_.end() ? *it : nullptr;
	}
	void remove(const TrackPtr& track) {
		std::erase(tracks_, track);
	}

	[[nodiscard]] std::span<const TrackPtr> tracks() const {
		return tracks_;
	}

private:
	std::vector<TrackPtr> tracks_;
};
