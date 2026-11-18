#pragma once

// Saving / loading signals. No ImGui dependency, so this is reusable from the SDR / aff3ct side.
//
//   WAV  : self-describing (has the sample rate). Mono = real signal, stereo = I/Q (L = I, R = Q).
//          Writes float32; reads 16-bit PCM and float32.
//   cf32 : raw interleaved float32 I/Q   (GNU Radio file sink, ".cfile")
//   cs16 : raw interleaved int16   I/Q
//   cs8  : raw interleaved int8    I/Q   (HackRF)
//   cu8  : raw interleaved uint8   I/Q   (RTL-SDR)
//   f32  : raw float32, a single real channel
//
// Raw files have no header, so the sample rate has to be supplied when loading them.
// Everything is little-endian. Files are read and written in one go (fine up to a few
// hundred MB; stream in chunks if you need more).

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "signal.hpp"

namespace sig::io {

	static_assert(std::endian::native == std::endian::little, "signal_io assumes a little-endian host");

	enum class Format { Wav, CF32, CS16, CS8, CU8, F32 };

	struct FormatInfo {
		Format           format;
		std::string_view name;
		std::string_view extension; // lower case, with the leading dot
		std::string_view description;
		bool             self_describing; // true if the file stores its own sample rate
	};

	inline constexpr std::array formats{
		FormatInfo{Format::Wav,  "WAV",  ".wav",  "16-bit PCM / float32. Mono = real, stereo = I/Q (L = I, R = Q).", true},
		FormatInfo{Format::CF32, "cf32", ".cf32", "Raw interleaved float32 I/Q (GNU Radio .cfile).", false},
		FormatInfo{Format::CS16, "cs16", ".cs16", "Raw interleaved int16 I/Q.", false},
		FormatInfo{Format::CS8,  "cs8",  ".cs8",  "Raw interleaved int8 I/Q (HackRF).", false},
		FormatInfo{Format::CU8,  "cu8",  ".cu8",  "Raw interleaved uint8 I/Q (RTL-SDR).", false},
		FormatInfo{Format::F32,  "f32",  ".f32",  "Raw float32, one real channel.", false},
	};

	[[nodiscard]] inline const FormatInfo& info(Format format) {
		return *std::ranges::find(formats, format, &FormatInfo::format);
	}

	[[nodiscard]] inline std::optional<Format> format_from_extension(const std::filesystem::path& path) {
		auto ext = path.extension().string();
		std::ranges::transform(ext, ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

		if (ext == ".cfile") return Format::CF32; // GNU Radio's traditional name

		const auto it = std::ranges::find(formats, ext, &FormatInfo::extension);
		if (it == formats.end()) return std::nullopt;
		return it->format;
	}

	[[nodiscard]] inline std::vector<std::string> all_extensions() {
		std::vector<std::string> out;
		for (const auto& f : formats) out.emplace_back(f.extension);
		out.emplace_back(".cfile");
		return out;
	}

	struct LoadOptions {
		Format format = Format::Wav;
		float  sample_rate = 48'000.0f; // ignored for WAV, which stores its own
	};

	using Result = std::expected<void, std::string>;

	namespace detail {

		// ---- sample encodings ---------------------------------------------------------------------

		enum class Encoding { F32, S16, S8, U8 };

		[[nodiscard]] inline Encoding encoding_of(Format format) {
			switch (format) {
			case Format::CS16: return Encoding::S16;
			case Format::CS8:  return Encoding::S8;
			case Format::CU8:  return Encoding::U8;
			default:           return Encoding::F32; // cf32, f32 (WAV picks its own)
			}
		}

		template <typename T, typename Convert>
		[[nodiscard]] std::vector<float> decode_as(std::span<const std::byte> bytes, Convert convert) {
			const std::size_t count = bytes.size() / sizeof(T); // a trailing partial sample is dropped
			std::vector<float> out(count);
			for (std::size_t i = 0; i < count; ++i) {
				T raw;
				std::memcpy(&raw, bytes.data() + i * sizeof(T), sizeof(T));
				out[i] = convert(raw);
			}
			return out;
		}

		[[nodiscard]] inline std::vector<float> decode(std::span<const std::byte> bytes, Encoding encoding) {
			switch (encoding) {
			case Encoding::F32:
				return decode_as<float>(bytes, [](float v) { return v; });
			case Encoding::S16:
				return decode_as<std::int16_t>(bytes, [](std::int16_t v) { return static_cast<float>(v) / 32768.0f; });
			case Encoding::S8:
				return decode_as<std::int8_t>(bytes, [](std::int8_t v) { return static_cast<float>(v) / 128.0f; });
			case Encoding::U8:
				return decode_as<std::uint8_t>(bytes, [](std::uint8_t v) { return (static_cast<float>(v) - 127.5f) / 127.5f; });
			}
			std::unreachable();
		}

		template <typename T, typename Convert>
		void encode_as(std::span<const float> samples, std::vector<std::byte>& out, Convert convert) {
			const std::size_t start = out.size();
			out.resize(start + samples.size() * sizeof(T));
			for (std::size_t i = 0; i < samples.size(); ++i) {
				const T raw = convert(std::clamp(samples[i], -1.0f, 1.0f));
				std::memcpy(out.data() + start + i * sizeof(T), &raw, sizeof(T));
			}
		}

		inline void encode(std::span<const float> samples, Encoding encoding, std::vector<std::byte>& out) {
			switch (encoding) {
			case Encoding::F32:
				return encode_as<float>(samples, out, [](float v) { return v; });
			case Encoding::S16:
				return encode_as<std::int16_t>(samples, out, [](float v) { return static_cast<std::int16_t>(std::lround(v * 32767.0f)); });
			case Encoding::S8:
				return encode_as<std::int8_t>(samples, out, [](float v) { return static_cast<std::int8_t>(std::lround(v * 127.0f)); });
			case Encoding::U8:
				return encode_as<std::uint8_t>(samples, out, [](float v) { return static_cast<std::uint8_t>(std::lround(v * 127.5f + 127.5f)); });
			}
			std::unreachable();
		}

		// ---- channel (de)interleaving ----------------------------------------------------------

		[[nodiscard]] inline std::vector<std::vector<float>> deinterleave(std::span<const float> samples, std::size_t channels) {
			const std::size_t frames = samples.size() / channels;
			std::vector<std::vector<float>> out(channels, std::vector<float>(frames));
			for (std::size_t i = 0; i < frames; ++i)
				for (std::size_t c = 0; c < channels; ++c) out[c][i] = samples[i * channels + c];
			return out;
		}

		// A real signal written as 2 channels gets a silent Q.
		[[nodiscard]] inline std::vector<float> interleave(const Signal& signal, std::size_t channels) {
			const std::size_t frames = signal.frames();
			std::vector<float> out(frames * channels, 0.0f);
			for (std::size_t c = 0; c < std::min(channels, signal.channels.size()); ++c)
				for (std::size_t i = 0; i < frames; ++i) out[i * channels + c] = signal.channels[c][i];
			return out;
		}

		// ---- byte helpers -----------------------------------------------------------------------

		template <typename T>
		[[nodiscard]] std::optional<T> read_le(std::span<const std::byte> bytes, std::size_t offset) {
			if (offset > bytes.size() || bytes.size() - offset < sizeof(T)) return std::nullopt;
			T value;
			std::memcpy(&value, bytes.data() + offset, sizeof(T));
			return value;
		}

		template <typename T>
		void append_le(std::vector<std::byte>& out, T value) {
			const std::size_t start = out.size();
			out.resize(start + sizeof(T));
			std::memcpy(out.data() + start, &value, sizeof(T));
		}

		inline void append_tag(std::vector<std::byte>& out, std::string_view tag) {
			for (const char c : tag) out.push_back(static_cast<std::byte>(c));
		}

		[[nodiscard]] inline std::expected<std::vector<std::byte>, std::string> read_file(const std::filesystem::path& path) {
			std::ifstream in(path, std::ios::binary | std::ios::ate);
			if (!in) return std::unexpected(std::format("Cannot open '{}'", path.string()));

			const auto size = static_cast<std::size_t>(in.tellg());
			in.seekg(0);

			std::vector<std::byte> bytes(size);
			if (!in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)))
				return std::unexpected(std::format("Error while reading '{}'", path.string()));
			return bytes;
		}

		[[nodiscard]] inline Result write_file(const std::filesystem::path& path, std::span<const std::byte> bytes) {
			std::ofstream out(path, std::ios::binary | std::ios::trunc);
			if (!out) return std::unexpected(std::format("Cannot create '{}'", path.string()));

			out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			if (!out) return std::unexpected(std::format("Error while writing '{}'", path.string()));
			return {};
		}

		// ---- WAV ----------------------------------------------------------------------------------

		struct Decoded {
			float sample_rate;
			std::vector<std::vector<float>> channels;
		};

		[[nodiscard]] inline std::expected<Decoded, std::string> decode_wav(std::span<const std::byte> bytes) {
			const auto tag_at = [&](std::size_t offset) -> std::string_view {
				if (offset > bytes.size() || bytes.size() - offset < 4) return {};
				return { reinterpret_cast<const char*>(bytes.data()) + offset, 4 };
				};

			if (tag_at(0) != "RIFF" || tag_at(8) != "WAVE") return std::unexpected("Not a RIFF/WAVE file");

			std::uint16_t format_tag = 0, channels = 0, bits = 0;
			std::uint32_t rate = 0;
			bool have_fmt = false, have_data = false;
			std::span<const std::byte> data;

			// Walk the chunk list (fmt, fact, LIST, data, ... in any order).
			std::size_t pos = 12;
			while (pos + 8 <= bytes.size()) {
				const auto id = tag_at(pos);
				const auto size = *read_le<std::uint32_t>(bytes, pos + 4); // can't fail: loop condition
				const std::size_t body = pos + 8;
				const std::size_t len = std::min<std::size_t>(size, bytes.size() - body); // tolerate truncated files

				if (id == "fmt " && len >= 16) {
					format_tag = *read_le<std::uint16_t>(bytes, body);
					channels = *read_le<std::uint16_t>(bytes, body + 2);
					rate = *read_le<std::uint32_t>(bytes, body + 4);
					bits = *read_le<std::uint16_t>(bytes, body + 14);
					if (format_tag == 0xFFFE && len >= 26) // WAVE_FORMAT_EXTENSIBLE: the real tag is in the sub-format
						format_tag = *read_le<std::uint16_t>(bytes, body + 24);
					have_fmt = true;
				}
				else if (id == "data") {
					data = bytes.subspan(body, len);
					have_data = true;
				}
				pos = body + len + (len & 1); // chunks are padded to even sizes
			}

			if (!have_fmt || !have_data) return std::unexpected("WAV file is missing its fmt or data chunk");
			if (rate == 0) return std::unexpected("WAV file has a sample rate of 0");
			if (channels < 1 || channels > 2)
				return std::unexpected(std::format("{} channels not supported (use mono for a real signal, stereo for I/Q)", channels));

			std::vector<float> interleaved;
			if (format_tag == 1 && bits == 16)      interleaved = decode(data, Encoding::S16);
			else if (format_tag == 3 && bits == 32) interleaved = decode(data, Encoding::F32);
			else
				return std::unexpected(std::format("Unsupported WAV encoding (format {}, {} bit). Use 16-bit PCM or 32-bit float.",
					format_tag, bits));

			return Decoded{ static_cast<float>(rate), deinterleave(interleaved, channels) };
		}

		inline void encode_wav(const Signal& signal, std::vector<std::byte>& out) {
			const auto channels = static_cast<std::uint16_t>(signal.channels.size());
			const auto rate = static_cast<std::uint32_t>(std::lround(signal.sample_rate));
			const auto frames = static_cast<std::uint32_t>(signal.frames());
			const std::uint32_t data_bytes = frames * channels * sizeof(float);

			out.reserve(56 + data_bytes);

			append_tag(out, "RIFF");
			append_le<std::uint32_t>(out, 48 + data_bytes); // file size - 8
			append_tag(out, "WAVE");

			append_tag(out, "fmt ");
			append_le<std::uint32_t>(out, 16);
			append_le<std::uint16_t>(out, 3); // IEEE float
			append_le<std::uint16_t>(out, channels);
			append_le<std::uint32_t>(out, rate);
			append_le<std::uint32_t>(out, rate * channels * sizeof(float)); // byte rate
			append_le<std::uint16_t>(out, static_cast<std::uint16_t>(channels * sizeof(float))); // block align
			append_le<std::uint16_t>(out, 32);

			append_tag(out, "fact"); // required by the spec for non-PCM formats
			append_le<std::uint32_t>(out, 4);
			append_le<std::uint32_t>(out, frames);

			append_tag(out, "data");
			append_le<std::uint32_t>(out, data_bytes);
			encode(interleave(signal, channels), Encoding::F32, out);
		}

		// ---- raw ----------------------------------------------------------------------------------

		[[nodiscard]] inline std::expected<Decoded, std::string> decode_raw(std::span<const std::byte> bytes,
			const LoadOptions& options) {
			if (!(options.sample_rate > 0.0f)) return std::unexpected("Sample rate must be greater than 0");

			const std::size_t channels = options.format == Format::F32 ? 1 : 2;
			const auto interleaved = decode(bytes, encoding_of(options.format));
			return Decoded{ options.sample_rate, deinterleave(interleaved, channels) };
		}

		[[nodiscard]] inline Signal to_signal(const std::filesystem::path& path, Format format, Decoded decoded) {
			Signal signal{
				.name = path.filename().string(),
				.description = std::format("Loaded from {} ({})", path.string(), info(format).name),
				.sample_rate = decoded.sample_rate,
				.default_view_seconds = 0.0f,
				.layout = decoded.channels.size() == 2 ? Layout::IQ : Layout::Real,
				.channels = std::move(decoded.channels),
			};
			// Start zoomed in on roughly 1000 samples, whatever the sample rate.
			signal.default_view_seconds = std::clamp(1000.0f / signal.sample_rate, 1e-5f, 10.0f);
			return signal;
		}

	} // namespace detail

	// ---- public API -----------------------------------------------------------------------------

	[[nodiscard]] inline std::expected<Signal, std::string> load(const std::filesystem::path& path,
		const LoadOptions& options) {
		const auto bytes = detail::read_file(path);
		if (!bytes) return std::unexpected(bytes.error());

		auto decoded = options.format == Format::Wav ? detail::decode_wav(*bytes)
			: detail::decode_raw(*bytes, options);
		if (!decoded) return std::unexpected(std::move(decoded.error()));
		if (decoded->channels.front().empty()) return std::unexpected("The file contains no samples");

		return detail::to_signal(path, options.format, std::move(*decoded));
	}

	[[nodiscard]] inline Result save(const Signal& signal, const std::filesystem::path& path, Format format) {
		if (signal.frames() == 0) return std::unexpected("There is nothing to save");

		std::vector<std::byte> bytes;

		switch (format) {
		case Format::Wav: {
			const std::size_t data_bytes = signal.frames() * signal.channels.size() * sizeof(float);
			if (data_bytes > 0xFFFF'FFFFu - 48u) return std::unexpected("Too large for a WAV file (4 GiB limit); use cf32 instead");
			detail::encode_wav(signal, bytes);
			break;
		}
		case Format::F32:
			if (signal.layout == Layout::IQ)
				return std::unexpected("f32 holds one real channel. Save I/Q signals as WAV, cf32, cs16, cs8 or cu8.");
			detail::encode(detail::interleave(signal, 1), detail::Encoding::F32, bytes);
			break;
		default: // cf32 / cs16 / cs8 / cu8: a real signal is written with Q = 0
			detail::encode(detail::interleave(signal, 2), detail::encoding_of(format), bytes);
			break;
		}

		return detail::write_file(path, bytes);
	}

} // namespace sig::io