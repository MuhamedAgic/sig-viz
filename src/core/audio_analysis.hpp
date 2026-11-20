#pragma once

// Measurements for sound: which note is playing, and how loud it sounds.

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#include "fft.hpp"
#include "signal.hpp"

namespace sig::analysis {

	// ---- pitch ----------------------------------------------------------------------------------------

	struct Pitch {
		double hz = 0.0;
		double clarity = 0.0; // 0 .. 1: how clearly the sound repeats (a clean note ~1, noise ~0)
	};

	// The note's frequency in frames [begin, end) (YIN method), or nothing if no clear note is there.
	//
	// A note is a shape that repeats. Slide a copy of the sound along itself: at a shift of exactly one
	// period, the copy lies right on top of the original. The smallest such shift is the period, and
	// 1 / period the pitch. (The strongest frequency in a spectrum is not always the note: in many
	// instruments an overtone is louder than the note itself.)
	[[nodiscard]] inline std::optional<Pitch> pitch(
		const Signal& signal,
		std::size_t   begin,
		std::size_t   end,
		double        min_hz = 40.0,
		double        max_hz = 4000.0
	) {
		end = std::min(end, signal.frames());
		if (end <= begin + 64) {
			return std::nullopt;
		}
		const std::size_t   n = end - begin, window = n / 2;
		std::vector<double> x(n);
		for (std::size_t i = 0; i < n; ++i) {
			x[i] = signal.channels[0][begin + i];
		}

		// d(lag) = sum over the window of (x[j] - x[j + lag])^2: how different the sound is from itself
		// `lag` samples later. Expanded into energies and a correlation, which the FFT computes for all lags at once.
		const std::size_t         size = std::bit_ceil(2 * n);
		std::vector<fft::Complex> all(size), head(size);
		for (std::size_t i = 0; i < n; ++i) {
			all[i] = static_cast<float>(x[i]);
		}
		for (std::size_t i = 0; i < window; ++i) {
			head[i] = static_cast<float>(x[i]);
		}
		fft::forward(all);
		fft::forward(head);
		for (std::size_t k = 0; k < size; ++k) {
			all[k] *= std::conj(head[k]);
		}
		fft::inverse(all); // all[lag] = sum_j x[j] * x[j + lag], j < window

		std::vector<double> energy(n + 1, 0.0); // prefix sums of x^2
		for (std::size_t i = 0; i < n; ++i) {
			energy[i + 1] = energy[i] + x[i] * x[i];
		}
		const auto window_energy = [&](std::size_t from) { return energy[from + window] - energy[from]; };

		const auto min_lag = static_cast<std::size_t>(std::max(2.0, signal.sample_rate / max_hz));
		const auto max_lag = std::min(window - 1, static_cast<std::size_t>(signal.sample_rate / min_hz));
		if (min_lag + 2 >= max_lag) {
			return std::nullopt;
		}

		// Normalised: d'(lag) = d(lag) / (average of d over smaller lags). Dips well below 1 at the period.
		std::vector<double> d(max_lag + 2, 1.0);
		double              running = 0.0;
		for (std::size_t lag = 1; lag <= max_lag + 1; ++lag) {
			const double diff = window_energy(0) + window_energy(lag) - 2.0 * all[lag].real();
			running += diff;
			d[lag] = running > 0.0 ? diff * static_cast<double>(lag) / running : 1.0;
		}

		// The first dip below the threshold (the period, not a multiple of it), then down to its bottom.
		constexpr double threshold = 0.15;
		std::size_t      best = 0;
		for (std::size_t lag = min_lag; lag <= max_lag; ++lag) {
			if (d[lag] < threshold) {
				while (lag + 1 <= max_lag && d[lag + 1] < d[lag]) {
					++lag;
				}
				best = lag;
				break;
			}
		}
		if (best == 0) { // no clear dip: the deepest one, if it is deep enough
			best = min_lag;
			for (std::size_t lag = min_lag; lag <= max_lag; ++lag) {
				if (d[lag] < d[best]) {
					best = lag;
				}
			}
			if (d[best] > 0.4) {
				return std::nullopt;
			}
		}

		// Between samples: a parabola through the dip.
		const double a = d[best - 1], b = d[best], c = d[best + 1];
		const double shift = (a - 2.0 * b + c) > 0.0 ? 0.5 * (a - c) / (a - 2.0 * b + c) : 0.0;
		return Pitch{ signal.sample_rate / (static_cast<double>(best) + shift), std::clamp(1.0 - b, 0.0, 1.0) };
	}

	struct Note {
		std::string name;  // "A4"
		double      cents; // how far off the note: -50 .. +50 (100 cents = one semitone, the step between piano keys)
		int         midi;  // MIDI note number (A4 = 69)
	};

	// The piano key nearest to `hz` (A4 = 440 Hz; every semitone is 2^(1/12) higher).
	[[nodiscard]] inline Note nearest_note(double hz) {
		static constexpr std::array names{ "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
		const double                midi = 69.0 + 12.0 * std::log2(hz / 440.0);
		const auto                  key = static_cast<int>(std::lround(midi));
		const int                   octave = key / 12 - 1;
		return { std::string(names[static_cast<std::size_t>(((key % 12) + 12) % 12)]) + std::to_string(octave),
			     (midi - key) * 100.0,
			     key };
	}

	// ---- loudness (EBU R 128 / ITU-R BS.1770) -----------------------------------------------------------

	struct Loudness {
		std::optional<double> integrated_lufs; // the whole part, ignoring silence: what streaming services normalise
		std::optional<double> momentary_max;   // loudest 0.4 s
		std::optional<double> short_term_max;  // loudest 3 s
		double                range_lu = 0.0;  // how much the loudness varies (loud vs. quiet parts)
	};

	namespace detail {

		// A two-pole, two-zero filter (a "biquad"), run sample by sample.
		struct Biquad {
			double b0, b1, b2, a1, a2;
			double z1 = 0.0, z2 = 0.0;
			double operator()(double x) {
				const double y = b0 * x + z1;
				z1 = b1 * x - a1 * y + z2;
				z2 = b2 * x - a2 * y;
				return y;
			}
		};

		// The "K" weighting: roughly how sensitive our ears are. A shelf that lifts the highs a little
		// (the head's effect) and a high-pass that drops the deep bass we barely hear. Coefficients for any rate.
		[[nodiscard]] inline std::array<Biquad, 2> k_weighting(double rate) {
			double       f0 = 1681.974450955533, gain_db = 3.999843853973347, q = 0.7071752369554196;
			double       k = std::tan(std::numbers::pi * f0 / rate);
			const double vh = std::pow(10.0, gain_db / 20.0), vb = std::pow(vh, 0.4996667741545416);
			double       a0 = 1.0 + k / q + k * k;
			const Biquad shelf{ (vh + vb * k / q + k * k) / a0,
				                2.0 * (k * k - vh) / a0,
				                (vh - vb * k / q + k * k) / a0,
				                2.0 * (k * k - 1.0) / a0,
				                (1.0 - k / q + k * k) / a0 };
			f0 = 38.13547087602444;
			q = 0.5003270373238773;
			k = std::tan(std::numbers::pi * f0 / rate);
			a0 = 1.0 + k / q + k * k;
			const Biquad high_pass{ 1.0, -2.0, 1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0 };
			return { shelf, high_pass };
		}

		[[nodiscard]] inline double to_lufs(double mean_square) {
			return -0.691 + 10.0 * std::log10(std::max(mean_square, 1e-30));
		}

	} // namespace detail

	// LUFS: loudness units relative to full scale, the way broadcasters and streaming services measure
	// loudness. Unlike RMS, it weighs frequencies like our ears do, and it ignores the silent parts.
	// -14 LUFS is typical for music on streaming services, -23 LUFS for TV. Real signals (I/Q: I only).
	[[nodiscard]] inline Loudness loudness(const Signal& signal, std::size_t begin, std::size_t end) {
		end = std::min(end, signal.frames());
		Loudness          out;
		const auto        block = static_cast<std::size_t>(0.4 * signal.sample_rate); // 400 ms
		const std::size_t hop = block / 4;                                            // 75 % overlap
		if (end <= begin || block == 0 || end - begin < block) {
			return out;
		}

		// Weighted and squared.
		auto                filters = detail::k_weighting(signal.sample_rate);
		std::vector<double> squared_sum(end - begin + 1, 0.0); // prefix sums
		for (std::size_t i = begin; i < end; ++i) {
			const double y = filters[1](filters[0](signal.channels[0][i]));
			squared_sum[i - begin + 1] = squared_sum[i - begin] + y * y;
		}
		const auto mean_square = [&](std::size_t from, std::size_t length) {
			return (squared_sum[from + length] - squared_sum[from]) / static_cast<double>(length);
		};

		std::vector<double> blocks; // mean square of every 400 ms block
		for (std::size_t from = 0; from + block <= end - begin; from += hop) {
			blocks.push_back(mean_square(from, block));
		}
		double momentary = -1e9;
		for (const double ms : blocks) {
			momentary = std::max(momentary, detail::to_lufs(ms));
		}
		out.momentary_max = momentary;

		const auto short_block = static_cast<std::size_t>(3.0 * signal.sample_rate);
		if (end - begin >= short_block) {
			std::vector<double> short_term;
			for (std::size_t from = 0; from + short_block <= end - begin; from += hop) {
				short_term.push_back(detail::to_lufs(mean_square(from, short_block)));
			}
			out.short_term_max = *std::ranges::max_element(short_term);
			// Loudness range: spread between the quieter and louder short-term values (10th to 95th percentile).
			std::ranges::sort(short_term);
			out.range_lu = short_term[short_term.size() * 95 / 100] - short_term[short_term.size() / 10];
		}

		// Integrated: average the blocks, skipping silence (below -70 LUFS), then skip what is more than
		// 10 LU quieter than that average (pauses, fades), and average again.
		const auto gated_mean = [&](double gate) -> std::optional<double> {
			double      sum = 0.0;
			std::size_t count = 0;
			for (const double ms : blocks) {
				if (detail::to_lufs(ms) > gate) {
					sum += ms;
					++count;
				}
			}
			if (count == 0) {
				return std::nullopt;
			}
			return sum / static_cast<double>(count);
		};
		const auto absolute = gated_mean(-70.0);
		if (!absolute) {
			return out;
		}
		const auto relative = gated_mean(detail::to_lufs(*absolute) - 10.0);
		if (relative) {
			out.integrated_lufs = detail::to_lufs(*relative);
		}
		return out;
	}

} // namespace sig::analysis
