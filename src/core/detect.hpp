#pragma once

// Finding things in a recording you know nothing about yet:
//   find_bursts: where is something transmitting? (a radio recording is mostly silence)
//   identify:    what is it? A guess of the modulation, symbol rate and frequency, with the reasons.

#include <algorithm>
#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <format>
#include <string>
#include <vector>

#include "digital.hpp"
#include "fft.hpp"
#include "signal.hpp"
#include "units.hpp"

namespace sig::analysis {

	struct Burst {
		double begin = 0.0;   // seconds
		double end = 0.0;
		double peak_db = 0.0; // loudest moment, dBFS
	};

	// Stretches where the signal is at least `threshold_db` above the noise floor.
	//
	// Like spotting cars on a quiet night road from the noise they make: first learn how loud "nothing"
	// is (the floor: the level most of the recording sits at), then everything clearly above it is a car.
	// Gaps shorter than `min_gap` seconds are bridged, so one packet with a short pause stays one burst.
	[[nodiscard]] inline std::vector<Burst> find_bursts(const Signal& signal, double threshold_db, double min_gap = 0.005) {
		std::vector<Burst> bursts;
		if (signal.frames() == 0) return bursts;

		// Power in blocks of about a millisecond.
		const auto block = static_cast<std::size_t>(std::max(1.0, signal.sample_rate * 0.001));
		std::vector<double> db;
		for (std::size_t start = 0; start < signal.frames(); start += block) {
			const std::size_t end = std::min(start + block, signal.frames());
			double power = 0.0;
			for (std::size_t i = start; i < end; ++i) power += std::norm(signal.complex_at(i));
			db.push_back(to_db(power / static_cast<double>(end - start)));
		}

		// The floor: 10 % of the blocks are quieter than this.
		std::vector<double> sorted = db;
		std::ranges::nth_element(sorted, sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() / 10));
		const double threshold = sorted[sorted.size() / 10] + threshold_db;

		const double seconds_per_block = static_cast<double>(block) / signal.sample_rate;
		const auto gap_blocks = static_cast<std::size_t>(min_gap / seconds_per_block);
		for (std::size_t b = 0; b < db.size(); ++b) {
			if (db[b] < threshold) continue;
			const double begin = static_cast<double>(b) * seconds_per_block;
			const double end = static_cast<double>(b + 1) * seconds_per_block;
			if (!bursts.empty() && begin - bursts.back().end <= static_cast<double>(gap_blocks) * seconds_per_block + 1e-12) {
				bursts.back().end = end;
				bursts.back().peak_db = std::max(bursts.back().peak_db, db[b]);
			}
			else {
				bursts.push_back({ begin, end, db[b] });
			}
		}
		std::erase_if(bursts, [&](const Burst& b) { return b.end - b.begin < 2.0 * seconds_per_block; }); // single blips
		return bursts;
	}

	struct Identification {
		bool            digital = false;   // false: no clear digital pattern (analog, noise, ...)
		digital::Scheme scheme = digital::Scheme::BPSK;
		double          symbol_rate = 0.0; // symbols per second
		double          center_hz = 0.0;   // middle of the occupied band (signal frequency)
		double          bandwidth_hz = 0.0;
		std::string     reasons;           // how the guess was made, for people
	};

	namespace detail {

		// Spectrum of x (power per bin, bins 0 .. size/2), x zero-padded to a power of 2.
		[[nodiscard]] inline std::vector<double> power_spectrum(std::span<const std::complex<float>> x, std::size_t& size) {
			size = std::bit_ceil(std::max<std::size_t>(x.size(), 2));
			std::vector<fft::Complex> buffer(size);
			std::ranges::copy(x, buffer.begin());
			fft::forward(buffer);
			std::vector<double> p(size);
			for (std::size_t k = 0; k < size; ++k) p[k] = std::norm(buffer[k]);
			return p;
		}

		// Which part of the energy is in the strongest tone (its bin and the two next to it):
		// nearly 1 for a pure tone, small for noise or a mix of many tones.
		[[nodiscard]] inline double tone_fraction(std::span<const std::complex<float>> x) {
			std::size_t size = 0;
			const auto p = power_spectrum(x, size);
			double sum = 0.0;
			std::size_t top = 0;
			for (std::size_t k = 0; k < size; ++k) {
				sum += p[k];
				if (p[k] > p[top]) top = k;
			}
			const double tone = p[(top + size - 1) % size] + p[top] + p[(top + 1) % size];
			return sum > 0.0 ? tone / sum : 0.0;
		}

		[[nodiscard]] inline double median(std::vector<double> v) {
			if (v.empty()) return 0.0;
			std::ranges::nth_element(v, v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2));
			return v[v.size() / 2];
		}

	} // namespace detail

	// A best guess of what [first, last) frames contain, the way a person would work it out:
	//   1. Where is the energy? (the band that holds 98 % of the power: centre and width)
	//   2. Does the strength change a lot?           -> on-off keying (OOK)
	//   3. Is the frequency mostly at two values?     -> FSK
	//   4. Does squaring leave a pure tone?           -> BPSK (both phases square to the same thing)
	//      Does the 4th power leave one?              -> QPSK
	//   5. How often does something change? The changes happen on a fixed beat, the symbol rate, which
	//      shows up as a tone in the spectrum of "how much changed from one sample to the next".
	[[nodiscard]] inline Identification identify(const Signal& signal, std::size_t first, std::size_t last) {
		Identification id;
		last = std::min({ last, signal.frames(), first + (std::size_t{ 1 } << 18) });
		if (last <= first + 64) {
			id.reasons = "Too short to say anything.";
			return id;
		}
		const double rate = signal.sample_rate;
		std::vector<std::complex<float>> z(last - first);
		for (std::size_t n = 0; n < z.size(); ++n) z[n] = signal.complex_at(first + n);

		// 1. Occupied band: the frequencies that rise well above the noise floor (halfway, in dB, between
		//    the floor and the strongest bin), so wideband noise doesn't count as signal.
		{
			fft::Analyzer analyzer(1024, fft::Window::Hann);
			const fft::Spectrum spectrum = analyzer.average(signal, first, last);
			std::vector<double> db(spectrum.power.size());
			std::ranges::transform(spectrum.power, db.begin(), [](float p) { return to_db(p); });
			const double floor = detail::median(db), top = *std::ranges::max_element(db);
			const double threshold = floor + (top - floor) / 2.0;
			std::size_t low = db.size(), high = 0;
			for (std::size_t k = 0; k < db.size(); ++k) {
				if (db[k] < threshold) continue;
				low = std::min(low, k);
				high = k;
			}
			if (low > high) low = high = 0;
			id.center_hz = signal.is_iq() ? spectrum.hz((static_cast<double>(low) + static_cast<double>(high)) / 2.0) : 0.0;
			id.bandwidth_hz = static_cast<double>(high - low + 1) * spectrum.bin_hz;
			if (signal.is_iq() && std::abs(id.center_hz) < 2.0 * spectrum.bin_hz) id.center_hz = 0.0;
		}

		// Look at it around 0 Hz.
		if (id.center_hz != 0.0)
			for (std::size_t n = 0; n < z.size(); ++n) z[n] *= std::polar(1.0f, static_cast<float>(-tau * id.center_hz * static_cast<double>(n) / rate));
		if (!signal.is_iq()) { // a real signal: look at its analytic (I/Q) version
			Signal part = signal;
			for (auto& channel : part.channels) channel.assign(z.size(), 0.0f);
			for (std::size_t n = 0; n < z.size(); ++n) part.channels[0][n] = z[n].real();
			const auto analytic = transform::detail::analytic(part);
			std::ranges::copy(analytic, z.begin());
		}

		// 2. Envelope variation.
		double mean = 0.0, spread = 0.0;
		for (const auto& v : z) mean += std::abs(v);
		mean /= static_cast<double>(z.size());
		for (const auto& v : z) spread += (std::abs(v) - mean) * (std::abs(v) - mean);
		const double variation = std::sqrt(spread / static_cast<double>(z.size())) / std::max(mean, 1e-20);

		// 3. Instantaneous frequency: mostly at two values (FSK), or mostly still (PSK, with spikes at the changes)?
		std::vector<double> frequency(z.size(), 0.0);
		for (std::size_t n = 1; n < z.size(); ++n) frequency[n] = std::arg(z[n] * std::conj(z[n - 1])) * rate / tau;
		frequency[0] = frequency[1];
		// Two values with a gap between them (FSK), or one value (PSK: still, except for spikes at the jumps)?
		// The middle half of all values lies between p25 and p75; for FSK hardly any are right in the middle.
		std::vector<double> sorted = frequency;
		std::ranges::sort(sorted);
		const double p25 = sorted[sorted.size() / 4], p75 = sorted[sorted.size() * 3 / 4];
		const double middle = (p25 + p75) / 2.0, typical = (p75 - p25) / 2.0; // FSK: the deviation
		const auto near_middle = std::ranges::count_if(frequency, [&](double f) { return std::abs(f - middle) < typical / 2.0; });
		const bool two_values = typical > rate * 1e-4 && static_cast<double>(near_middle) < 0.15 * static_cast<double>(frequency.size());

		// "What changed from one sample to the next", for the symbol rate.
		std::vector<std::complex<float>> change(z.size(), {});
		if (variation > 0.35) {
			id.scheme = digital::Scheme::OOK;
			id.reasons = std::format("The strength jumps between levels (variation {:.0f} %): on-off keying.", variation * 100.0);
			for (std::size_t n = 1; n < z.size(); ++n) change[n] = std::abs(std::abs(z[n]) - std::abs(z[n - 1]));
		}
		else if (two_values) {
			id.scheme = digital::Scheme::FSK;
			id.reasons = std::format("Steady strength, and the frequency sits about {} either side of the centre: FSK.", format_hz(typical));
			for (std::size_t n = 1; n < z.size(); ++n) change[n] = static_cast<float>(std::abs(frequency[n] - frequency[n - 1]));
		}
		else {
			std::vector<std::complex<float>> squared(z.size()), fourth(z.size());
			for (std::size_t n = 0; n < z.size(); ++n) {
				squared[n] = z[n] * z[n];
				fourth[n] = squared[n] * squared[n];
			}
			const double two = detail::tone_fraction(squared), four = detail::tone_fraction(fourth);
			if (two > 0.4) {
				id.scheme = digital::Scheme::BPSK;
				id.reasons = "Steady strength; squared it becomes a pure tone (the two phases fold onto one): BPSK.";
				// That tone sits at exactly twice the carrier: a precise centre frequency.
				if (signal.is_iq()) id.center_hz += digital::detail::strongest_tone(squared, rate) / 2.0;
			}
			else if (four > 0.4) {
				id.scheme = digital::Scheme::QPSK;
				id.reasons = "Steady strength; to the 4th power it becomes a pure tone (four phases fold onto one): QPSK.";
				if (signal.is_iq()) id.center_hz += digital::detail::strongest_tone(fourth, rate) / 4.0;
			}
			else {
				id.reasons = "No clear digital pattern: steady strength, but no tone appears when squared. Analog (AM / FM / voice) or noise?";
				return id;
			}
			for (std::size_t n = 1; n < z.size(); ++n) change[n] = std::norm(z[n] - z[n - 1]);
		}

		// 5. Symbol rate: the beat of the changes. Its harmonics (2x, 3x, ...) are about as strong, so take
		// the lowest peak that is nearly as strong as the strongest.
		float change_mean = 0.0f;
		for (const auto& c : change) change_mean += c.real();
		change_mean /= static_cast<float>(change.size());
		for (auto& c : change) c -= change_mean;
		std::size_t size = 0;
		const auto p = detail::power_spectrum(change, size);
		const double bin_hz = rate / static_cast<double>(size);
		// Ignore very slow beats (the length of the analysed part, or a repeating message). For FSK the
		// symbol rate is never far below the frequency deviation.
		double min_hz = 5.0 * rate / static_cast<double>(z.size());
		if (id.scheme == digital::Scheme::FSK) min_hz = std::max(min_hz, typical / 2.0);
		const std::size_t min_bin = std::max<std::size_t>(3, static_cast<std::size_t>(min_hz / bin_hz));
		double strongest = 0.0;
		for (std::size_t k = min_bin; k < size / 2; ++k) strongest = std::max(strongest, p[k]);
		std::size_t pick = 0;
		for (std::size_t k = min_bin; k + 1 < size / 2; ++k) {
			if (p[k] >= 0.5 * strongest && p[k] >= p[k - 1] && p[k] >= p[k + 1]) {
				pick = k;
				break;
			}
		}
		if (pick == 0) {
			id.reasons += " But no regular beat was found, so no symbol rate.";
			return id;
		}
		const double a = std::log(p[pick - 1] + 1e-30), b = std::log(p[pick] + 1e-30), c = std::log(p[pick + 1] + 1e-30);
		const double shift = (a - 2.0 * b + c) < 0.0 ? 0.5 * (a - c) / (a - 2.0 * b + c) : 0.0;
		id.symbol_rate = (static_cast<double>(pick) + shift) * bin_hz;
		id.digital = true;
		id.reasons += std::format(" Things change on a beat of {:.0f} per second: that is the symbol rate.", id.symbol_rate);
		return id;
	}

} // namespace sig::analysis
