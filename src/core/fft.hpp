#pragma once

// Fourier transforms and power spectra.
//
// The idea: most signals are a mix of simple waves, like a chord is a mix of notes. The Fourier
// transform un-mixes them, like a prism splitting white light into its colours, and says how strong
// each frequency is. The FFT (Fast Fourier Transform) is a fast way to compute it.
//
// pocketfft does the actual transform (any length; fastest when the length only has small prime
// factors, like powers of 2). This file hides its API and adds the signal-analysis parts on top:
// window functions, scaling to dBFS and averaging.

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <pocketfft_hdronly.h>

#include "signal.hpp"

namespace sig::fft {

	using Complex = std::complex<float>;

	// In-place discrete Fourier transform.
	inline void forward(std::span<Complex> data) {
		const pocketfft::shape_t  shape{ data.size() };
		const pocketfft::stride_t stride{ sizeof(Complex) };
		pocketfft::c2c(shape, stride, stride, { 0 }, pocketfft::FORWARD, data.data(), data.data(), 1.0f);
	}

	// In-place inverse transform, scaled so that inverse(forward(x)) == x.
	inline void inverse(std::span<Complex> data) {
		const pocketfft::shape_t  shape{ data.size() };
		const pocketfft::stride_t stride{ sizeof(Complex) };
		pocketfft::c2c(
			shape,
			stride,
			stride,
			{ 0 },
			pocketfft::BACKWARD,
			data.data(),
			data.data(),
			1.0f / static_cast<float>(data.size())
		);
	}

	// ---- windows ----------------------------------------------------------------------------------
	// An FFT assumes the block repeats forever. Where the block's end meets its start there is usually
	// a jump, and that jump smears energy over the whole spectrum ("leakage"). A window fades the block
	// in and out, like a DJ crossfading between two records, so there is no jump.
	// Trade-off: a window that fades more leaks less, but blurs each peak over more bins (a wider "lobe").

	enum class Window { Rectangular, Hann, Blackman, FlatTop };
	inline constexpr std::array all_windows{ Window::Rectangular, Window::Hann, Window::Blackman, Window::FlatTop };

	[[nodiscard]] constexpr std::string_view name(Window window) {
		switch (window) {
		case Window::Rectangular: return "Rectangular";
		case Window::Hann:        return "Hann";
		case Window::Blackman:    return "Blackman";
		case Window::FlatTop:     return "Flat top";
		}
		std::unreachable();
	}

	// How many bins either side of its centre a pure tone spreads over.
	[[nodiscard]] constexpr std::size_t lobe_half_width(Window window) {
		switch (window) {
		case Window::Rectangular: return 1;
		case Window::Hann:        return 2;
		case Window::Blackman:    return 3;
		case Window::FlatTop:     return 5;
		}
		std::unreachable();
	}

	[[nodiscard]] inline std::vector<float> make_window(Window window, std::size_t size) {
		std::vector<float> out(size);
		for (std::size_t n = 0; n < size; ++n) {
			const double x = tau * static_cast<double>(n) / static_cast<double>(size);
			double       w = 1.0;
			switch (window) {
			case Window::Rectangular: break;
			case Window::Hann:        w = 0.5 - 0.5 * std::cos(x); break;
			case Window::Blackman:    w = 0.42 - 0.5 * std::cos(x) + 0.08 * std::cos(2 * x); break;
			case Window::FlatTop:
				w = 0.21557895 - 0.41663158 * std::cos(x) + 0.277263158 * std::cos(2 * x) -
				    0.083578947 * std::cos(3 * x) + 0.006947368 * std::cos(4 * x);
				break;
			}
			out[n] = static_cast<float>(w);
		}
		return out;
	}

	// ---- spectra ----------------------------------------------------------------------------------

	// Power per frequency bin, scaled so that a full-scale tone (amplitude 1) peaks at 1, i.e. 0 dBFS.
	//   real signals: size/2 + 1 bins, from 0 Hz up to half the sample rate
	//   I/Q signals:  size bins, from -rate/2 up to just below +rate/2 (negative frequencies exist for I/Q)
	struct Spectrum {
		std::vector<float> power;
		double             first_hz = 0.0; // frequency of power[0]
		double             bin_hz = 1.0;   // spacing between bins

		[[nodiscard]] double hz(double bin) const {
			return first_hz + bin * bin_hz;
		}
	};

	// Computes spectra of `size`-sample blocks of a signal. Keeps its window and scratch buffer,
	// so one analyzer can process many blocks without allocating.
	class Analyzer {
	public:
		Analyzer(std::size_t size, Window window)
			: size_{ size }
			, window_kind_{ window }
			, window_{ make_window(window, size) }
			, buffer_(size) {
			double sum = 0.0;
			for (const float w : window_) {
				sum += w;
			}
			window_sum_ = static_cast<float>(sum);
		}

		[[nodiscard]] std::size_t size() const {
			return size_;
		}
		[[nodiscard]] Window window() const {
			return window_kind_;
		}
		[[nodiscard]] std::size_t bins(const Signal& signal) const {
			return signal.is_iq() ? size_ : size_ / 2 + 1;
		}

		// Adds the power spectrum of the block starting at frame `start` to `out` (bins(signal) long).
		// Samples outside the signal count as silence. Adding (instead of overwriting) makes averaging easy.
		void accumulate(const Signal& signal, std::ptrdiff_t start, std::span<float> out) {
			const auto frames = static_cast<std::ptrdiff_t>(signal.frames());
			for (std::size_t n = 0; n < size_; ++n) {
				const std::ptrdiff_t i = start + static_cast<std::ptrdiff_t>(n);
				const Complex v = (i >= 0 && i < frames) ? signal.complex_at(static_cast<std::size_t>(i)) : Complex{};
				buffer_[n] = v * window_[n];
			}
			forward(buffer_);

			// A real tone shows up twice (at +f and -f) and we only keep +f, hence the 2.
			const float scale = (signal.is_iq() ? 1.0f : 2.0f) / window_sum_;
			const float power_scale = scale * scale;
			for (std::size_t k = 0; k < out.size(); ++k) {
				const std::size_t bin = signal.is_iq() ? (k + size_ / 2) % size_ : k; // I/Q: put 0 Hz in the middle
				out[k] += std::norm(buffer_[bin]) * power_scale;
			}
		}

		// Average spectrum of the frames [begin, end) (Welch's method): the range is covered with
		// half-overlapping blocks, at most `max_blocks` of them (spread out evenly when the range is long).
		// A range shorter than one block gives a single block centred on it.
		//
		// Why average? Like stacking several photos of the same scene: what is really there (tones) stays,
		// the random grain (noise) evens out, so the noise floor becomes smooth and quiet tones show up.
		[[nodiscard]] Spectrum average(
			const Signal& signal,
			std::size_t   begin,
			std::size_t   end,
			std::size_t   max_blocks = 64
		) {
			Spectrum result{
				.power = std::vector<float>(bins(signal)),
				.first_hz = signal.is_iq() ? -static_cast<double>(size_ / 2) * signal.sample_rate / size_ : 0.0,
				.bin_hz = signal.sample_rate / static_cast<double>(size_),
			};

			const auto        length = static_cast<std::ptrdiff_t>(end > begin ? end - begin : 0);
			const auto        block = static_cast<std::ptrdiff_t>(size_);
			const std::size_t wanted =
				length > block ? 1 + static_cast<std::size_t>((length - block) / (block / 2)) : 1;
			const std::size_t blocks = std::clamp<std::size_t>(wanted, 1, max_blocks);

			for (std::size_t b = 0; b < blocks; ++b) {
				const std::ptrdiff_t start = blocks == 1 ? static_cast<std::ptrdiff_t>(begin) + (length - block) / 2
				                                         : static_cast<std::ptrdiff_t>(begin) +
				                                               static_cast<std::ptrdiff_t>(b) * (length - block) /
				                                                   static_cast<std::ptrdiff_t>(blocks - 1);
				accumulate(signal, start, result.power);
			}
			for (float& p : result.power) {
				p /= static_cast<float>(blocks);
			}
			return result;
		}

	private:
		std::size_t          size_;
		Window               window_kind_;
		std::vector<float>   window_;
		float                window_sum_ = 1.0f;
		std::vector<Complex> buffer_;
	};

} // namespace sig::fft
