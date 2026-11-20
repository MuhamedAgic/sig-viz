#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "live_input.hpp"
#include "workspace.hpp"

// Live input: the microphone, or a radio through rtl_tcp. The newest few seconds become a signal like
// any other ("Live: ..."), updated several times a second, so every window works on it live: the
// spectrum, the spectrogram (a scrolling waterfall), the tuner, the digital decoder.
// "Keep a copy" freezes what is there now as a normal signal; "Record" keeps everything from now until
// "Stop recording" (not only the newest seconds) and then shows it in the viewer, to play back.
class LiveWindow {
public:
	static constexpr const char* title = "Live";
	bool                         open = true;

	void draw(Workspace& ws);

private:
	enum class Source { Microphone, Radio };

	friend constexpr std::string_view name(Source source) {
		switch (source) {
		case Source::Microphone: return "Microphone";
		case Source::Radio:      return "Radio (rtl_tcp)";
		}
		std::unreachable();
	}

	void start(Workspace& ws);
	void stop();
	void update(Workspace& ws);
	void start_recording();
	void finish_recording(Workspace& ws); // the recording becomes a signal in the library, shown in the viewer

	Source         source_ = Source::Microphone;
	RtlTcpSettings radio_;
	float          keep_seconds_ = 5.0f; // how much of the newest signal the live track holds
	bool           follow_ = true;       // keep the playhead at the newest samples

	std::unique_ptr<LiveInput>            input_;
	std::string                           error_;
	std::vector<float>                    i_, q_; // the newest samples (at most keep_seconds_)
	TrackPtr                              track_; // the live track in the library
	std::uint64_t                         live_id_ = 0;
	std::chrono::steady_clock::time_point last_update_{};
	double                                received_ = 0.0; // samples, for the rate shown
	std::chrono::steady_clock::time_point started_{};

	bool               recording_ = false;
	std::vector<float> recorded_i_, recorded_q_;
};
