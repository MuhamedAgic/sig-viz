#pragma once

// Turns a Signal into audible samples. No raylib / ImGui dependency, so it is easy to test
// and reusable (e.g. to listen to the output of an aff3ct demodulator).
//
// Real signals are played as they are. I/Q signals can be listened to as:
//   I / Q : one of the two channels
//   AM    : magnitude sqrt(I^2 + Q^2) with the DC (carrier) removed
//   FM    : instantaneous frequency, i.e. the phase step between consecutive samples
//
// The signal's sample rate is converted to the output rate on the fly. When the signal's rate is
// higher than the output rate, every output sample is the average of the source samples it
// covers (a crude low-pass, so a 2.4 MS/s capture doesn't alias into a mess).
// The speed factor works like a tape machine: 2x plays twice as fast and one octave higher.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <string_view>
#include <utility>

#include "signal.hpp"

namespace sig::audio {

	enum class Listen { Signal, I, Q, Am, Fm };

	[[nodiscard]] constexpr std::string_view name(Listen listen) {
		switch (listen) {
		case Listen::Signal: return "Signal";
		case Listen::I:      return "I";
		case Listen::Q:      return "Q";
		case Listen::Am:     return "AM (magnitude)";
		case Listen::Fm:     return "FM (frequency)";
		}
		std::unreachable();
	}

	// The modes that make sense for a layout.
	[[nodiscard]] inline std::span<const Listen> listen_options(Layout layout) {
		static constexpr std::array real{ Listen::Signal };
		static constexpr std::array iq{ Listen::I, Listen::Q, Listen::Am, Listen::Fm };
		return layout == Layout::IQ ? std::span<const Listen>{iq} : std::span<const Listen>{ real };
	}

	struct Params {
		Listen listen = Listen::Signal;
		double speed = 1.0;
		bool   loop = true;
	};

	class Renderer {
	public:
		// Start (or restart) rendering at the given time.
		void seek(const Signal& signal, double seconds) {
			cursor_ = std::clamp(seconds * signal.sample_rate, 0.0, static_cast<double>(signal.frames()));
			dc_primed_ = false;
		}

		// Time of the next sample that will be rendered. It runs ahead of what is audible by whatever is buffered.
		[[nodiscard]] double cursor_seconds(const Signal& signal) const { return cursor_ / signal.sample_rate; }

		// Fills `out` with mono samples at `out_rate` Hz and advances the cursor.
		void render(const Signal& signal, const Params& params, double out_rate, std::span<float> out) {
			const auto frames = static_cast<std::int64_t>(signal.frames());
			if (frames == 0) {
				std::ranges::fill(out, 0.0f);
				return;
			}

			const Listen listen = effective_listen(signal, params.listen);
			const double step = signal.sample_rate * params.speed / out_rate; // source samples per output sample

			// Value of the (possibly demodulated) signal at source index i; wraps when looping.
			const auto value_at = [&](std::int64_t i) -> float {
				if (params.loop) i = ((i % frames) + frames) % frames;
				else if (i < 0 || i >= frames) return 0.0f;
				const std::int64_t prev = i > 0 ? i - 1 : (params.loop ? frames - 1 : 0);
				return sample(signal, listen, static_cast<std::size_t>(i), static_cast<std::size_t>(prev));
				};

			for (float& o : out) {
				if (!params.loop && cursor_ >= static_cast<double>(frames)) {
					o = 0.0f; // past the end: silence
					continue;
				}

				const auto base = static_cast<std::int64_t>(std::floor(cursor_));
				float v = 0.0f;
				if (step <= 1.0) {
					// Source rate <= output rate: interpolate between the two neighbouring samples.
					const auto frac = static_cast<float>(cursor_ - static_cast<double>(base));
					v = std::lerp(value_at(base), value_at(base + 1), frac);
				}
				else {
					// Source rate > output rate: average everything this output sample covers.
					const auto end = static_cast<std::int64_t>(std::floor(cursor_ + step));
					const auto count = std::max<std::int64_t>(end - base, 1);
					float sum = 0.0f;
					for (std::int64_t k = 0; k < count; ++k) sum += value_at(base + k);
					v = sum / static_cast<float>(count);
				}

				if (listen == Listen::Am) v = block_dc(v);
				o = std::clamp(v, -1.0f, 1.0f);

				cursor_ += step;
				if (params.loop && cursor_ >= static_cast<double>(frames)) cursor_ = std::fmod(cursor_, static_cast<double>(frames));
			}
		}

	private:
		[[nodiscard]] static Listen effective_listen(const Signal& signal, Listen wanted) {
			if (signal.layout != Layout::IQ || signal.channels.size() < 2) return Listen::Signal;
			return wanted == Listen::Signal ? Listen::I : wanted;
		}

		[[nodiscard]] static float sample(const Signal& signal, Listen listen, std::size_t i, std::size_t prev) {
			const auto& ch0 = signal.channels[0];
			switch (listen) {
			case Listen::Signal:
			case Listen::I:  return ch0[i];
			case Listen::Q:  return signal.channels[1][i];
			case Listen::Am: return std::hypot(ch0[i], signal.channels[1][i]);
			case Listen::Fm: {
				// z[n] * conj(z[n-1]) has the phase step between the two samples, in (-pi, pi].
				const auto& ch1 = signal.channels[1];
				const float re = ch0[i] * ch0[prev] + ch1[i] * ch1[prev];
				const float im = ch1[i] * ch0[prev] - ch0[i] * ch1[prev];
				return std::atan2(im, re) / std::numbers::pi_v<float>; // +-1 = +-half the sample rate
			}
			}
			std::unreachable();
		}

		// One-pole DC blocker (about 40 Hz at 48 kHz): an AM carrier would otherwise be a huge DC offset.
		float block_dc(float x) {
			if (!dc_primed_) { // start from silence instead of a click
				dc_in_ = x;
				dc_out_ = 0.0f;
				dc_primed_ = true;
			}
			const float y = x - dc_in_ + 0.995f * dc_out_;
			dc_in_ = x;
			dc_out_ = y;
			return y;
		}

		double cursor_ = 0.0; // in source samples
		float  dc_in_ = 0.0f, dc_out_ = 0.0f;
		bool   dc_primed_ = false;
	};

} // namespace sig::audio