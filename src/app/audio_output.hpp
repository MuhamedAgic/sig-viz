#pragma once

// Plays a Signal through a raylib AudioStream, following the UI's playhead.
//
// raylib's stream is double-buffered: whenever one half has been played, IsAudioStreamProcessed()
// turns true and we refill it. That is polled once per frame from the UI thread, so there is no
// audio callback and no locking, but it needs a reasonably steady frame rate (>~25 FPS with the
// 2048-frame buffers used here). If the app stalls, the stream is stopped instead of looping
// stale data (raylib streams loop their buffers when they are not refilled).
//
// Requires InitAudioDevice() before construction and CloseAudioDevice() after destruction.

#include <algorithm>
#include <cmath>
#include <vector>

#include <raylib.h>

#include "core/signal_audio.hpp"

class AudioOutput {
public:
	static constexpr unsigned sample_rate = 48'000;
	static constexpr int      chunk_frames = 2048; // 43 ms; two of them are queued at any time

	AudioOutput() {
		if (!IsAudioDeviceReady()) {
			return; // no sound card: stay silent, the rest of the app still works
		}

		SetAudioStreamBufferSizeDefault(chunk_frames);
		stream_ = LoadAudioStream(sample_rate, 32, 1); // 32 bit = float samples, mono
		ready_ = stream_.buffer != nullptr;
	}

	~AudioOutput() {
		if (!ready_) {
			return;
		}
		stop();
		UnloadAudioStream(stream_);
	}

	AudioOutput(const AudioOutput&) = delete;
	AudioOutput& operator=(const AudioOutput&) = delete;

	[[nodiscard]] bool available() const {
		return ready_;
	}

	// Call every frame while the signal should be audible. The sound follows `playhead` (seconds):
	// when the UI jumps (scrub, loop) the audio jumps with it.
	void play(const sig::Signal& signal, double playhead, const sig::audio::Params& params, float volume) {
		if (!ready_) {
			return;
		}

		if (playing_ && out_of_sync(signal, playhead, params)) {
			stop();
		}

		if (!playing_) {
			renderer_.seek(signal, playhead);
			fill(signal, params); // prime both halves of the double buffer
			fill(signal, params);
			PlayAudioStream(stream_);
			playing_ = true;
		}
		else {
			for (int i = 0; i < 2 && IsAudioStreamProcessed(stream_); ++i) {
				fill(signal, params);
			}
		}

		SetAudioStreamVolume(stream_, volume);
	}

	// Silence immediately (drops what is buffered). Call every frame it should not be audible; it is cheap.
	void stop() {
		if (!playing_) {
			return;
		}
		StopAudioStream(stream_);
		playing_ = false;
	}

private:
	// The render cursor legitimately runs ahead of the playhead by what is buffered (up to ~85 ms
	// of audio, which is more source time at higher speeds), so only react to bigger differences.
	[[nodiscard]] bool out_of_sync(const sig::Signal& signal, double playhead, const sig::audio::Params& params) const {
		double       error = renderer_.cursor_seconds(signal) - playhead;
		const double loop_length =
			std::min(params.loop_end, static_cast<double>(signal.duration())) - std::max(params.loop_begin, 0.0);
		if (params.loop && loop_length > 0.0) {
			error = std::remainder(error, loop_length); // shortest way round
		}
		return std::abs(error) > 0.1 + 0.15 * params.speed;
	}

	void fill(const sig::Signal& signal, const sig::audio::Params& params) {
		renderer_.render(signal, params, sample_rate, buffer_);
		UpdateAudioStream(stream_, buffer_.data(), chunk_frames);
	}

	AudioStream          stream_{};
	bool                 ready_ = false;
	bool                 playing_ = false;
	sig::audio::Renderer renderer_;
	std::vector<float>   buffer_ = std::vector<float>(chunk_frames);
};
