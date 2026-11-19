#pragma once

// Measurements on signals: levels, tone quality (SNR / THD) and how well two signals line up.
//
// Most results are in decibels (dB), a logarithmic scale, because levels span an enormous range (a
// whisper vs. a jet engine). For amplitudes: dB = 20 * log10(ratio), so -6 dB is about half and -20 dB a
// tenth. For powers (amplitude squared): dB = 10 * log10(ratio). dBFS = relative to full scale (1.0).

#include <algorithm>
#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <optional>
#include <vector>

#include "fft.hpp"
#include "signal.hpp"
#include "units.hpp"

namespace sig::analysis {

	// ---- levels ---------------------------------------------------------------------------------------

	struct Levels {
		double dc = 0.0;   // average value (I/Q: magnitude of the average)
		double rms = 0.0;  // root mean square: the "energy" level
		double peak = 0.0; // largest magnitude
		std::size_t clipped = 0; // samples at full scale: the recording hit its ceiling and the tops were cut off
		[[nodiscard]] double crest_db() const { return rms > 0.0 ? 20.0 * std::log10(peak / rms) : 0.0; }
	};

	// Levels of frames [begin, end). For I/Q signals every sample counts as the complex number I + jQ.
	[[nodiscard]] inline Levels levels(const Signal& signal, std::size_t begin, std::size_t end) {
		end = std::min(end, signal.frames());
		if (begin >= end) return {};

		std::complex<double> sum{};
		double sum_squares = 0.0;
		double peak = 0.0;
		std::size_t clipped = 0;
		for (std::size_t i = begin; i < end; ++i) {
			const std::complex<float> z = signal.complex_at(i);
			sum += std::complex<double>(z);
			sum_squares += std::norm(std::complex<double>(z));
			peak = std::max(peak, static_cast<double>(std::abs(z)));
			if (std::abs(z.real()) >= 0.999f || std::abs(z.imag()) >= 0.999f) ++clipped;
		}
		const auto count = static_cast<double>(end - begin);
		return {
			.dc = signal.is_iq() ? std::abs(sum) / count : sum.real() / count,
			.rms = std::sqrt(sum_squares / count),
			.peak = peak,
			.clipped = clipped,
		};
	}

	// ---- spectrum measurements ---------------------------------------------------------------------

	struct Peak {
		std::size_t bin; // strongest bin
		double      hz;  // refined frequency, between bins
		double      db;  // dBFS
	};

	// Strongest component, ignoring the first `skip` bins (e.g. DC). The frequency is refined by fitting
	// a parabola through the top three bins, so it is far more precise than the bin spacing.
	[[nodiscard]] inline std::optional<Peak> strongest(const fft::Spectrum& spectrum, std::size_t skip = 0) {
		const auto& p = spectrum.power;
		if (p.size() <= skip) return std::nullopt;

		const auto top = static_cast<std::size_t>(std::max_element(p.begin() + static_cast<std::ptrdiff_t>(skip), p.end()) - p.begin());
		if (top == 0 || top + 1 >= p.size()) return Peak{ top, spectrum.hz(static_cast<double>(top)), to_db(p[top]) };

		const double a = to_db(p[top - 1]), b = to_db(p[top]), c = to_db(p[top + 1]);
		const double denominator = a - 2.0 * b + c;
		const double shift = denominator != 0.0 ? 0.5 * (a - c) / denominator : 0.0; // in bins, -0.5 .. 0.5
		return Peak{ top, spectrum.hz(static_cast<double>(top) + shift), b - 0.25 * (a - c) * shift };
	}

	struct ToneQuality {
		Peak                  fundamental;
		double                snr_db;  // fundamental power vs. everything that is not the tone or its harmonics
		std::optional<double> thd_db;  // harmonics (2f, 3f, ...) vs. fundamental; real signals only
	};

	// Treats the strongest component as "the" tone and measures how clean it is.
	//   SNR: how much the tone stands out from the noise, like one voice at a noisy party.
	//   THD: how strong its overtones (2x, 3x, ... the frequency) are. A pure sine has none; they are
	//        what makes a distorted guitar sound "dirty".
	//   lobe: fft::lobe_half_width() of the window used, i.e. how many bins one tone occupies each side
	[[nodiscard]] inline std::optional<ToneQuality> tone_quality(const fft::Spectrum& spectrum, bool is_iq, std::size_t main_lobe) {
		const auto& p = spectrum.power;
		// A tone also leaks a little into the bins next to its main lobe (the window's side lobes).
		// Count those as tone too, otherwise a perfectly clean tone would show a poor SNR.
		const std::size_t lobe = 2 * main_lobe;
		const std::size_t dc_bins = is_iq ? 0 : lobe + 1; // a real signal's DC offset is not part of the tone
		const auto peak = strongest(spectrum, dc_bins);
		if (!peak) return std::nullopt;

		std::vector<bool> used(p.size(), false);
		std::fill_n(used.begin(), dc_bins, true);

		// Total power of the bins around `centre` that are not counted yet.
		const auto take = [&](std::size_t centre) {
			double sum = 0.0;
			const std::size_t first = centre > lobe ? centre - lobe : 0;
			for (std::size_t k = first; k <= centre + lobe && k < p.size(); ++k) {
				if (used[k]) continue;
				used[k] = true;
				sum += p[k];
			}
			return sum;
		};

		const double fundamental = take(peak->bin);

		std::optional<double> thd_db;
		if (!is_iq) {
			double harmonics = 0.0;
			for (int h = 2; h <= 9; ++h) {
				const double bin = (h * peak->hz - spectrum.first_hz) / spectrum.bin_hz;
				if (bin + static_cast<double>(lobe) >= static_cast<double>(p.size())) break; // above half the sample rate
				harmonics += take(static_cast<std::size_t>(std::lround(bin)));
				thd_db = to_db(harmonics / fundamental);
			}
		}

		double noise = 0.0;
		for (std::size_t k = 0; k < p.size(); ++k)
			if (!used[k]) noise += p[k];

		return ToneQuality{ *peak, to_db(fundamental / noise), thd_db };
	}

	// ---- comparing two signals ---------------------------------------------------------------------

	// How many samples B has to be delayed to line up best with A (negative: B is moved earlier),
	// i.e. a[n] ~ b[n - lag]. Uses the cross-correlation, computed with FFTs, over the first
	// `max_samples` of each signal.
	//
	// Cross-correlation is like sliding a transparency (B) over a drawing (A): for every shift, multiply
	// what lies on top of each other and add it all up. Where the lines match, the sum is largest.
	// Trying every shift one by one would be slow; the FFT does all of them at once.
	[[nodiscard]] inline std::ptrdiff_t best_lag(const Signal& a, const Signal& b, std::size_t max_samples = std::size_t{ 1 } << 20) {
		const std::size_t na = std::min(a.frames(), max_samples);
		const std::size_t nb = std::min(b.frames(), max_samples);
		if (na == 0 || nb == 0) return 0;

		const std::size_t n = std::bit_ceil(na + nb); // zero padding: lags never wrap onto each other
		std::vector<fft::Complex> x(n), y(n);
		for (std::size_t i = 0; i < na; ++i) x[i] = a.complex_at(i);
		for (std::size_t i = 0; i < nb; ++i) y[i] = b.complex_at(i);

		fft::forward(x);
		fft::forward(y);
		for (std::size_t k = 0; k < n; ++k) x[k] *= std::conj(y[k]);
		fft::inverse(x); // x[m] = sum over i of a[i] * conj(b[i - m])

		std::size_t best = 0;
		for (std::size_t m = 1; m < n; ++m)
			if (std::norm(x[m]) > std::norm(x[best])) best = m;

		// Indices past A's length are the negative lags, wrapped around.
		return best < na ? static_cast<std::ptrdiff_t>(best) : static_cast<std::ptrdiff_t>(best) - static_cast<std::ptrdiff_t>(n);
	}

	struct Similarity {
		double      correlation = 0.0;    // 1 = identical shape, 0 = unrelated, -1 = inverted (level does not matter)
		double      rms_difference = 0.0; // RMS of A - B
		std::size_t overlap = 0;          // samples where both exist
	};

	// Compares A with B delayed by `lag` samples, over the part where both exist.
	[[nodiscard]] inline Similarity similarity(const Signal& a, const Signal& b, std::ptrdiff_t lag) {
		const auto na = static_cast<std::ptrdiff_t>(a.frames());
		const auto nb = static_cast<std::ptrdiff_t>(b.frames());
		const std::ptrdiff_t first = std::max<std::ptrdiff_t>(0, lag);
		const std::ptrdiff_t last = std::min(na, nb + lag);
		if (first >= last) return {};

		std::complex<double> cross{};
		double energy_a = 0.0, energy_b = 0.0, difference = 0.0;
		for (std::ptrdiff_t i = first; i < last; ++i) {
			const std::complex<double> za = a.complex_at(static_cast<std::size_t>(i));
			const std::complex<double> zb = b.complex_at(static_cast<std::size_t>(i - lag));
			cross += za * std::conj(zb);
			energy_a += std::norm(za);
			energy_b += std::norm(zb);
			difference += std::norm(za - zb);
		}
		const double norm = std::sqrt(energy_a * energy_b);
		const auto overlap = static_cast<std::size_t>(last - first);
		return {
			.correlation = norm > 0.0 ? cross.real() / norm : 0.0,
			.rms_difference = std::sqrt(difference / static_cast<double>(overlap)),
			.overlap = overlap,
		};
	}

} // namespace sig::analysis
