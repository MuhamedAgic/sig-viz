#pragma once

// Feeds a raylib AudioStream from sig::audio::Renderer.
//
// raylib's stream is double-buffered: whenever one half has been played, IsAudioStreamProcessed()
// turns true and we refill it. That is polled once per frame from the UI thread, so there is no
// audio callback and no locking, but it needs a reasonably steady frame rate (>~25 FPS with the
// 2048-frame buffers used here). If the app stalls, the stream is stopped instead of looping
// stale data (raylib streams loop their buffers when they are not refilled).
//
// Requires InitAudioDevice() before construction and CloseAudioDevice() after destruction.

#include <cmath>
#include <vector>

#include <raylib.h>

#include "signal_audio.hpp"

class AudioOutput {
public:
	static constexpr unsigned sample_rate = 48'000;
	static constexpr int     chunk_frames = 2048; // 43 ms; two of them are queued at any time

	struct Settings {
		sig::audio::Listen listen = sig::audio::Listen::Signal;
		float speed = 1.0f;
		bool  loop = true;
		float volume = 0.25f;
	};

	AudioOutput() {
		if (!IsAudioDeviceReady()) return; // no sound card: stay silent, the rest of the app still works

		SetAudioStreamBufferSizeDefault(chunk_frames);
		stream_ = LoadAudioStream(sample_rate, 32, 1); // 32 bit = float samples, mono
		ready_ = stream_.buffer != nullptr;
	}

	~AudioOutput() {
		if (!ready_) return;
		stop();
		UnloadAudioStream(stream_);
	}

	AudioOutput(const AudioOutput&) = delete;
	AudioOutput& operator=(const AudioOutput&) = delete;

	[[nodiscard]] bool available() const { return ready_; }

	// Silence immediately (drops what is buffered). Cheap to call every frame.
	void stop() {
		if (!playing_) return;
		StopAudioStream(stream_);
		playing_ = false;
	}

	// Call once per frame, whether or not anything should be audible.
	//   playhead:    where the UI's playhead is, in seconds; the audio follows it
	//   wants_sound: false while paused, muted or scrubbing
	void update(const sig::Signal& signal, float playhead, const Settings& settings, bool wants_sound) {
		if (!ready_) return;
		if (!wants_sound) {
			stop();
			return;
		}

		// Scrubbed, looped around, or drifted: restart from the playhead.
		if (playing_ && out_of_sync(signal, playhead, settings)) stop();

		if (!playing_) {
			renderer_.seek(signal, playhead);
			fill(signal, settings); // prime both halves of the double buffer
			fill(signal, settings);
			PlayAudioStream(stream_);
			playing_ = true;
		}
		else {
			for (int i = 0; i < 2 && IsAudioStreamProcessed(stream_); ++i) fill(signal, settings);
		}

		SetAudioStreamVolume(stream_, settings.volume);
	}

private:
	// The render cursor legitimately runs ahead of the playhead by what is buffered (up to ~85 ms
	// of audio, which is more source time at higher speeds), so only react to bigger differences.
	[[nodiscard]] bool out_of_sync(const sig::Signal& signal, float playhead, const Settings& settings) const {
		double error = renderer_.cursor_seconds(signal) - static_cast<double>(playhead);
		if (settings.loop) error = std::remainder(error, static_cast<double>(signal.duration())); // shortest way round
		return std::abs(error) > 0.1 + 0.15 * static_cast<double>(settings.speed);
	}

	void fill(const sig::Signal& signal, const Settings& settings) {
		const sig::audio::Params params{
			.listen = settings.listen,
			.speed = static_cast<double>(settings.speed),
			.loop = settings.loop,
		};
		renderer_.render(signal, params, sample_rate, buffer_);
		UpdateAudioStream(stream_, buffer_.data(), chunk_frames);
	}

	AudioStream stream_{};
	bool ready_ = false;
	bool playing_ = false;
	sig::audio::Renderer renderer_;
	std::vector<float> buffer_ = std::vector<float>(chunk_frames);
};