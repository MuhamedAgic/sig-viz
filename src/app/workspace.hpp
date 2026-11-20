#pragma once

// What all windows share: the signals, which one is being looked at, where the playhead is and the
// user's marks on it (a selection and two cursors). Windows get it passed in every frame.

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <utility>

#include "library.hpp"
#include "core/examples.hpp"
#include "core/signal.hpp"

struct TimeRange {
	double begin = 0.0; // seconds
	double end = 0.0;

	[[nodiscard]] double length() const {
		return end - begin;
	}
};

struct Playback {
	double position = 0.0; // seconds
	float  speed = 1.0f;
	bool   playing = false;
	bool   loop = true;
	bool   scrubbing = false; // the user is dragging the playhead: hold still

	// Moves the playhead on by dt seconds of real time. When looping, it wraps within `loop_range`.
	void advance(double dt, double duration, const TimeRange& loop_range) {
		if (!playing || scrubbing) {
			return;
		}

		position += dt * speed;
		if (loop && position >= loop_range.end) {
			position = loop_range.begin + std::fmod(position - loop_range.begin, loop_range.length());
		}
		else if (!loop && position >= duration) {
			position = duration;
			playing = false;
		}
	}

	void toggle(double duration) {
		if (!playing && position >= duration) {
			position = 0.0; // replay from the start
		}
		playing = !playing;
	}
};

// Where the mouse points in some window, so the others can show the same spot (a faint line).
struct Hover {
	std::optional<double> seconds;
	std::optional<double> hz; // on the dial: includes the signal's center_hz
};

// Something to hear instead of the current signal: one band of it, a transform's result, ...
// The viewer plays it while `source` is still the current signal.
struct Audition {
	TrackPtr    source;
	TrackPtr    track;
	std::string label; // e.g. "only 400 - 480 Hz"
};

struct Workspace {
	Library                              library;
	TrackPtr                             current; // the signal in the viewer; the analysis windows follow it
	Playback                             playback;
	std::optional<TimeRange>             selection; // shift + drag in the viewer
	std::array<std::optional<double>, 2> cursors;   // right-click in the viewer (seconds)
	std::optional<Audition>              audition;

	Hover hover;      // last frame's: read this
	Hover next_hover; // this frame's: windows write it while the mouse is over them

	// Call at the end of every frame.
	void end_frame() {
		hover = std::exchange(next_hover, {});
		if (audition && audition->source != current) {
			audition.reset();
		}
	}

	Workspace() {
		for (sig::Signal& example : sig::make_example_signals()) {
			library.add(std::move(example), true);
		}
		select(library.tracks().front());
	}

	[[nodiscard]] const sig::Signal& signal() const {
		return current->signal();
	}

	void select(TrackPtr track) {
		current = std::move(track);
		playback.position = 0.0;
		selection.reset();
		cursors = {};
		audition.reset();
	}

	// Swaps a live input's previous update for a new one, without disturbing the playhead or marks.
	void update_live(const TrackPtr& previous, TrackPtr update) {
		if (current == previous) {
			current = update;
		}
		library.replace(previous, std::move(update));
	}

	// Adds a new signal to the library and shows it.
	void add_and_select(sig::Signal signal, Lineage lineage = {}) {
		select(library.add(std::move(signal), false, std::move(lineage)));
	}

	// The part playback loops over: the selection if there is one, else the whole signal.
	[[nodiscard]] TimeRange loop_range() const {
		return selection.value_or(TimeRange{ 0.0, signal().duration() });
	}

	// The part to analyse: the selection if there is one, else `seconds` around the playhead
	// (kept inside the signal, so nothing before its start or after its end is analysed).
	[[nodiscard]] TimeRange focus(double seconds) const {
		if (selection) {
			return *selection;
		}
		const double duration = signal().duration();
		const double begin = std::clamp(playback.position - seconds / 2.0, 0.0, std::max(duration - seconds, 0.0));
		return { begin, std::min(begin + seconds, duration) };
	}
};
