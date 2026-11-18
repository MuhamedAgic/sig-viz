#pragma once

// Operations that make a new signal out of existing ones. Inputs are never modified.
// Every operation returns the new signal or a message saying why it could not be done.
// The result keeps the input's name; the caller decides what to call it.

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <expected>
#include <format>
#include <numbers>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fft.hpp"
#include "signal.hpp"

namespace sig::transform {

	using Result = std::expected<Signal, std::string>;
	using Complex = std::complex<float>;

	namespace detail {

		// A signal with the same name, description, rate and zoom as `like`, but new samples.
		[[nodiscard]] inline Signal with_channels(const Signal& like, Layout layout, std::vector<std::vector<float>> channels) {
			Signal out{ like.name, like.description, like.sample_rate, like.default_view_seconds, layout, std::move(channels) };
			return out;
		}

		[[nodiscard]] inline std::vector<Complex> to_complex(const Signal& signal) {
			std::vector<Complex> out(signal.frames());
			for (std::size_t i = 0; i < out.size(); ++i) out[i] = signal.complex_at(i);
			return out;
		}

		[[nodiscard]] inline Signal from_complex(const Signal& like, std::span<const Complex> samples) {
			std::vector<float> i(samples.size()), q(samples.size());
			for (std::size_t n = 0; n < samples.size(); ++n) {
				i[n] = samples[n].real();
				q[n] = samples[n].imag();
			}
			return with_channels(like, Layout::IQ, { std::move(i), std::move(q) });
		}

		[[nodiscard]] inline Signal from_real(const Signal& like, std::vector<float> samples) {
			return with_channels(like, Layout::Real, { std::move(samples) });
		}

		// Applies fn to every sample of every channel.
		template <typename Fn>
		[[nodiscard]] Signal map_samples(const Signal& signal, Fn fn) {
			Signal out = signal;
			for (auto& channel : out.channels)
				for (float& v : channel) v = fn(v);
			return out;
		}

		// The analytic signal x + j * hilbert(x): the same real signal, with the negative frequencies
		// removed. Its magnitude is the envelope and its angle the phase, which is what AM and FM
		// demodulation need. Done in the frequency domain: zero the negative half, double the positive.
		[[nodiscard]] inline std::vector<Complex> analytic(const Signal& real) {
			std::vector<Complex> z = to_complex(real);
			const std::size_t n = z.size();
			fft::forward(z);
			for (std::size_t k = 1; k < n; ++k) {
				if (2 * k < n) z[k] *= 2.0f;      // positive frequencies
				else if (2 * k > n) z[k] = 0.0f;  // negative frequencies (the Nyquist bin, 2k == n, stays)
			}
			fft::inverse(z);
			return z;
		}

		// I/Q samples of any signal; real signals are made analytic first.
		[[nodiscard]] inline std::vector<Complex> as_iq(const Signal& signal) {
			return signal.is_iq() ? to_complex(signal) : analytic(signal);
		}

		[[nodiscard]] inline float peak_magnitude(std::span<const float> samples) {
			float peak = 0.0f;
			for (const float v : samples) peak = std::max(peak, std::abs(v));
			return peak;
		}

		// Scales so that the largest magnitude becomes `target`.
		inline void normalize_in_place(std::vector<float>& samples, float target = 0.9f) {
			const float peak = peak_magnitude(samples);
			if (peak > 0.0f)
				for (float& v : samples) v *= target / peak;
		}

		inline void remove_mean(std::vector<float>& samples) {
			if (samples.empty()) return;
			double sum = 0.0;
			for (const float v : samples) sum += v;
			const auto mean = static_cast<float>(sum / static_cast<double>(samples.size()));
			for (float& v : samples) v -= mean;
		}

	} // namespace detail

	// ---- level ------------------------------------------------------------------------------------

	[[nodiscard]] inline Result gain(const Signal& signal, float db) {
		const float factor = std::pow(10.0f, db / 20.0f);
		return detail::map_samples(signal, [factor](float v) { return v * factor; });
	}

	// Scales the signal so its largest magnitude is 1 (I/Q: largest |I + jQ|).
	[[nodiscard]] inline Result normalize(const Signal& signal) {
		float peak = 0.0f;
		for (std::size_t i = 0; i < signal.frames(); ++i) peak = std::max(peak, std::abs(signal.complex_at(i)));
		if (peak == 0.0f) return std::unexpected("The signal is silent");
		return detail::map_samples(signal, [peak](float v) { return v / peak; });
	}

	[[nodiscard]] inline Result remove_dc(const Signal& signal) {
		Signal out = signal;
		for (auto& channel : out.channels) detail::remove_mean(channel);
		return out;
	}

	// Adds Gaussian noise so that signal power / noise power = snr_db.
	[[nodiscard]] inline Result add_noise(const Signal& signal, float snr_db, unsigned seed) {
		double power = 0.0;
		for (std::size_t i = 0; i < signal.frames(); ++i) power += std::norm(signal.complex_at(i));
		power /= static_cast<double>(std::max<std::size_t>(signal.frames(), 1));
		if (power == 0.0) return std::unexpected("The signal is silent, so a signal-to-noise ratio means nothing");

		const double noise_power = power / std::pow(10.0, snr_db / 10.0);
		std::mt19937 rng{ seed };
		std::normal_distribution<float> noise{ 0.0f, static_cast<float>(std::sqrt(noise_power / static_cast<double>(signal.channels.size()))) };
		return detail::map_samples(signal, [&](float v) { return v + noise(rng); });
	}

	// ---- filters ----------------------------------------------------------------------------------

	enum class FilterType { LowPass, HighPass, BandPass, BandStop };
	inline constexpr std::array all_filter_types{ FilterType::LowPass, FilterType::HighPass, FilterType::BandPass, FilterType::BandStop };

	[[nodiscard]] constexpr std::string_view name(FilterType type) {
		switch (type) {
		case FilterType::LowPass:  return "Low-pass";
		case FilterType::HighPass: return "High-pass";
		case FilterType::BandPass: return "Band-pass";
		case FilterType::BandStop: return "Band-stop";
		}
		std::unreachable();
	}

	// The band [low_hz, high_hz] is what passes (band-stop: what is removed).
	// Low-pass only uses high_hz, high-pass only low_hz.
	struct FilterSpec {
		FilterType type = FilterType::LowPass;
		float      low_hz = 300.0f;
		float      high_hz = 3000.0f;
		int        taps = 101; // filter length: more taps = sharper edges, slower
	};

	// FIR filter coefficients (windowed sinc). Always an odd count, symmetric around the middle tap,
	// so filtering delays nothing.
	[[nodiscard]] inline std::vector<float> design_filter(const FilterSpec& spec, float sample_rate) {
		const int taps = std::max(spec.taps, 3) | 1;
		const int middle = taps / 2;
		const double nyquist = sample_rate / 2.0;

		// Ideal low-pass up to `cutoff_hz`, shortened to `taps` with a Blackman window, gain 1 at 0 Hz.
		const auto low_pass = [&](double cutoff_hz) {
			const double f = std::clamp(cutoff_hz, 0.0, nyquist) / sample_rate; // cycles per sample, 0 .. 0.5
			std::vector<double> h(static_cast<std::size_t>(taps));
			double sum = 0.0;
			for (int n = 0; n < taps; ++n) {
				const double x = n - middle;
				const double sinc = x == 0.0 ? 2.0 * f : std::sin(tau * f * x) / (std::numbers::pi * x);
				const double w = 0.42 - 0.5 * std::cos(tau * n / (taps - 1)) + 0.08 * std::cos(2.0 * tau * n / (taps - 1));
				h[static_cast<std::size_t>(n)] = sinc * w;
				sum += sinc * w;
			}
			if (sum > 0.0)
				for (double& v : h) v /= sum;
			return h;
		};
		// "Everything minus the band": an impulse (passes all) minus a band-pass.
		const auto invert = [&](std::vector<double> h) {
			for (double& v : h) v = -v;
			h[static_cast<std::size_t>(middle)] += 1.0;
			return h;
		};
		const auto subtract = [](std::vector<double> a, const std::vector<double>& b) {
			for (std::size_t i = 0; i < a.size(); ++i) a[i] -= b[i];
			return a;
		};

		std::vector<double> h;
		switch (spec.type) {
		case FilterType::LowPass:  h = low_pass(spec.high_hz); break;
		case FilterType::HighPass: h = invert(low_pass(spec.low_hz)); break;
		case FilterType::BandPass: h = subtract(low_pass(spec.high_hz), low_pass(spec.low_hz)); break;
		case FilterType::BandStop: h = invert(subtract(low_pass(spec.high_hz), low_pass(spec.low_hz))); break;
		}

		// Designed in double precision for accuracy, applied in float for speed.
		std::vector<float> taps_out(h.size());
		std::ranges::transform(h, taps_out.begin(), [](double v) { return static_cast<float>(v); });
		return taps_out;
	}

	// Filters every channel. I/Q signals get the same response at -f as at +f.
	[[nodiscard]] inline Result filter(const Signal& signal, const FilterSpec& spec) {
		const std::vector<float> h = design_filter(spec, signal.sample_rate);
		const std::size_t middle = h.size() / 2;

		Signal out = signal;
		std::vector<float> padded;
		for (auto& channel : out.channels) {
			// Pad with silence so every output sample can use all taps without bounds checks.
			padded.assign(channel.size() + 2 * middle, 0.0f);
			std::ranges::copy(channel, padded.begin() + static_cast<std::ptrdiff_t>(middle));

			for (std::size_t n = 0; n < channel.size(); ++n) {
				float sum = 0.0f;
				for (std::size_t k = 0; k < h.size(); ++k) sum += h[k] * padded[n + k];
				channel[n] = sum;
			}
		}
		return out;
	}

	// ---- frequency and rate -----------------------------------------------------------------------

	// Moves the whole spectrum up by `hz` (down if negative) by multiplying with e^(j 2 pi hz t).
	// The result is always I/Q: a shifted real signal is no longer symmetric around 0 Hz.
	[[nodiscard]] inline Result shift_frequency(const Signal& signal, double hz) {
		std::vector<Complex> z = detail::to_complex(signal);
		for (std::size_t n = 0; n < z.size(); ++n) {
			const double phase = tau * hz * static_cast<double>(n) / signal.sample_rate;
			z[n] *= Complex{ static_cast<float>(std::cos(phase)), static_cast<float>(std::sin(phase)) };
		}
		return detail::from_complex(signal, z);
	}

	// Converts to another sample rate with windowed-sinc interpolation. When the rate goes down,
	// everything above the new half-rate is filtered out first (otherwise it would fold back as aliasing).
	[[nodiscard]] inline Result resample(const Signal& signal, float new_rate) {
		if (!(new_rate > 0.0f)) return std::unexpected("The new sample rate must be greater than 0");

		const double ratio = new_rate / static_cast<double>(signal.sample_rate); // output samples per input sample
		const auto out_frames = static_cast<std::size_t>(std::llround(static_cast<double>(signal.frames()) * ratio));
		if (out_frames == 0) return std::unexpected("The result would have no samples");

		// The interpolation kernel sinc(x) * window, for x in 0 .. zero_crossings. It is evaluated a lot,
		// so it is tabulated once (with `steps` entries per zero crossing) and looked up.
		constexpr int zero_crossings = 16;
		constexpr int steps = 256;
		std::vector<float> table(zero_crossings * steps + 2);
		for (std::size_t i = 0; i < table.size(); ++i) {
			const double x = static_cast<double>(i) / steps;
			const double sinc = x == 0.0 ? 1.0 : std::sin(std::numbers::pi * x) / (std::numbers::pi * x);
			const double window = x < zero_crossings ? 0.5 + 0.5 * std::cos(std::numbers::pi * x / zero_crossings) : 0.0;
			table[i] = static_cast<float>(sinc * window);
		}

		const double cutoff = std::min(1.0, ratio);         // fraction of the input band to keep
		const double reach = zero_crossings / cutoff;        // kernel half-width, in input samples
		const auto frames = static_cast<std::ptrdiff_t>(signal.frames());

		Signal out = detail::with_channels(signal, signal.layout, {});
		out.sample_rate = new_rate;
		for (const auto& in : signal.channels) {
			std::vector<float>& y = out.channels.emplace_back(out_frames);
			for (std::size_t n = 0; n < out_frames; ++n) {
				const double t = static_cast<double>(n) / ratio; // position in the input, in samples
				const auto first = std::max<std::ptrdiff_t>(0, static_cast<std::ptrdiff_t>(std::ceil(t - reach)));
				const auto last = std::min<std::ptrdiff_t>(frames - 1, static_cast<std::ptrdiff_t>(std::floor(t + reach)));
				double sum = 0.0;
				for (std::ptrdiff_t i = first; i <= last; ++i) {
					const double x = std::abs(t - static_cast<double>(i)) * cutoff * steps;
					const auto k = static_cast<std::size_t>(x);
					if (k + 1 >= table.size()) continue;
					const auto kernel = std::lerp(table[k], table[k + 1], static_cast<float>(x - static_cast<double>(k)));
					sum += in[static_cast<std::size_t>(i)] * kernel;
				}
				y[n] = static_cast<float>(sum * cutoff);
			}
		}
		return out;
	}

	// ---- real <-> I/Q, demodulation ----------------------------------------------------------------

	// Real -> I/Q via the Hilbert transform: I is the signal itself, Q the same signal shifted 90 degrees.
	[[nodiscard]] inline Result to_iq(const Signal& signal) {
		if (signal.is_iq()) return std::unexpected("The signal already is I/Q");
		return detail::from_complex(signal, detail::analytic(signal));
	}

	// I/Q -> real, keeping I.
	[[nodiscard]] inline Result to_real(const Signal& signal) {
		if (!signal.is_iq()) return std::unexpected("The signal already is real");
		return detail::from_real(signal, signal.channels[0]);
	}

	// Envelope |I + jQ| without its average. Real signals are made I/Q first. Normalised.
	[[nodiscard]] inline Result am_demodulate(const Signal& signal) {
		const std::vector<Complex> z = detail::as_iq(signal);
		std::vector<float> out(z.size());
		for (std::size_t n = 0; n < z.size(); ++n) out[n] = std::abs(z[n]);
		detail::remove_mean(out);
		detail::normalize_in_place(out);
		return detail::from_real(signal, std::move(out));
	}

	// Instantaneous frequency: the phase step from one sample to the next. Real signals are made I/Q
	// first. Normalised, so the output shows the shape of the frequency changes, not their size.
	[[nodiscard]] inline Result fm_demodulate(const Signal& signal) {
		const std::vector<Complex> z = detail::as_iq(signal);
		std::vector<float> out(z.size());
		for (std::size_t n = 1; n < z.size(); ++n) out[n] = std::arg(z[n] * std::conj(z[n - 1]));
		if (out.size() > 1) out[0] = out[1];
		detail::remove_mean(out);
		detail::normalize_in_place(out);
		return detail::from_real(signal, std::move(out));
	}

	// ---- editing ------------------------------------------------------------------------------------

	// Keeps [begin, end) seconds.
	[[nodiscard]] inline Result trim(const Signal& signal, double begin, double end) {
		const auto to_frame = [&](double t) {
			return static_cast<std::size_t>(std::clamp(std::round(t * signal.sample_rate), 0.0, static_cast<double>(signal.frames())));
		};
		const std::size_t first = to_frame(begin), last = to_frame(end);
		if (first >= last) return std::unexpected("Nothing to keep: the range is empty");

		Signal out = detail::with_channels(signal, signal.layout, {});
		for (const auto& channel : signal.channels)
			out.channels.emplace_back(channel.begin() + static_cast<std::ptrdiff_t>(first), channel.begin() + static_cast<std::ptrdiff_t>(last));
		return out;
	}

	enum class Combine { Add, Subtract, Multiply };
	inline constexpr std::array all_combines{ Combine::Add, Combine::Subtract, Combine::Multiply };

	[[nodiscard]] constexpr std::string_view name(Combine op) {
		switch (op) {
		case Combine::Add:      return "A + B";
		case Combine::Subtract: return "A - B";
		case Combine::Multiply: return "A x B";
		}
		std::unreachable();
	}

	// Sample by sample A (op) B, with B delayed by `lag` samples. The result has A's length; where B
	// has no samples it counts as 0. Real and I/Q can be mixed: the result is I/Q if either is.
	// Multiplying a message by a carrier is exactly what modulation is.
	[[nodiscard]] inline Result combine(const Signal& a, const Signal& b, Combine op, std::ptrdiff_t lag = 0) {
		if (a.sample_rate != b.sample_rate)
			return std::unexpected(std::format("A is {} Hz and B is {} Hz; resample one of them first", a.sample_rate, b.sample_rate));

		std::vector<Complex> out(a.frames());
		const auto nb = static_cast<std::ptrdiff_t>(b.frames());
		for (std::size_t n = 0; n < out.size(); ++n) {
			const std::ptrdiff_t j = static_cast<std::ptrdiff_t>(n) - lag;
			const Complex x = a.complex_at(n);
			const Complex y = (j >= 0 && j < nb) ? b.complex_at(static_cast<std::size_t>(j)) : Complex{};
			switch (op) {
			case Combine::Add:      out[n] = x + y; break;
			case Combine::Subtract: out[n] = x - y; break;
			case Combine::Multiply: out[n] = x * y; break;
			}
		}

		if (a.is_iq() || b.is_iq()) return detail::from_complex(a, out);
		std::vector<float> real(out.size());
		for (std::size_t n = 0; n < out.size(); ++n) real[n] = out[n].real();
		return detail::from_real(a, std::move(real));
	}

} // namespace sig::transform
