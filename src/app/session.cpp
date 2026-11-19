#include "session.hpp"

#include <array>
#include <format>
#include <fstream>
#include <optional>
#include <sstream>
#include <vector>

#include "core/signal_io.hpp"

namespace session {

	namespace {

		constexpr const char* header = "sig-viz session 1";

		// Names and descriptions go into one line each, tab-separated: keep tabs and newlines out.
		std::string one_line(std::string text) {
			for (char& c : text)
				if (c == '\t' || c == '\n' || c == '\r') c = ' ';
			return text;
		}

		std::vector<std::string> split_tabs(const std::string& line) {
			std::vector<std::string> parts;
			std::stringstream in(line);
			for (std::string part; std::getline(in, part, '\t');) parts.push_back(part);
			return parts;
		}

	} // namespace

	std::expected<void, std::string> save(const Workspace& ws, const std::filesystem::path& file) {
		const std::filesystem::path folder = file.parent_path() / (file.stem().string() + "_signals");
		std::error_code error;
		std::filesystem::create_directories(folder, error);
		if (error) return std::unexpected(std::format("Can't make the folder {}", folder.string()));

		std::ofstream out(file);
		if (!out) return std::unexpected(std::format("Can't write {}", file.string()));
		out << header << '\n';

		int number = 0;
		for (const TrackPtr& track : ws.library.tracks()) {
			if (track->built_in()) continue; // the examples are always there
			const std::string wav = std::format("{:03}.wav", ++number);
			if (auto saved = sig::io::save(track->signal(), folder / wav, sig::io::Format::Wav); !saved) return saved;
			const sig::Signal& s = track->signal();
			out << "signal\t" << wav << '\t' << std::format("{:.17g}", s.center_hz) << '\t' << s.default_view_seconds << '\t'
				<< one_line(s.name) << '\t' << one_line(s.description) << '\n';
		}
		out << "current\t" << one_line(ws.current->name()) << '\n';
		out << "playhead\t" << std::format("{:.17g}", ws.playback.position) << '\n';
		if (ws.selection) out << "selection\t" << std::format("{:.17g}\t{:.17g}", ws.selection->begin, ws.selection->end) << '\n';
		for (std::size_t i = 0; i < ws.cursors.size(); ++i)
			if (ws.cursors[i]) out << "cursor\t" << i << '\t' << std::format("{:.17g}", *ws.cursors[i]) << '\n';

		if (!out) return std::unexpected(std::format("Error while writing {}", file.string()));
		return {};
	}

	std::expected<void, std::string> load(Workspace& ws, const std::filesystem::path& file) {
		std::ifstream in(file);
		std::string line;
		if (!in || !std::getline(in, line) || line != header) return std::unexpected(std::format("{} is not a sig-viz session", file.string()));
		const std::filesystem::path folder = file.parent_path() / (file.stem().string() + "_signals");

		std::string current;
		double playhead = 0.0;
		std::optional<TimeRange> selection;
		std::array<std::optional<double>, 2> cursors;
		while (std::getline(in, line)) {
			const auto parts = split_tabs(line);
			if (parts.empty()) continue;
			if (parts[0] == "signal" && parts.size() >= 6) {
				auto signal = sig::io::load(folder / parts[1], { .format = sig::io::Format::Wav });
				if (!signal) return std::unexpected(signal.error());
				signal->center_hz = std::stod(parts[2]);
				signal->default_view_seconds = std::stof(parts[3]);
				signal->name = parts[4];
				signal->description = parts[5];
				ws.library.add(std::move(*signal));
			}
			else if (parts[0] == "current" && parts.size() >= 2) current = parts[1];
			else if (parts[0] == "playhead" && parts.size() >= 2) playhead = std::stod(parts[1]);
			else if (parts[0] == "selection" && parts.size() >= 3) selection = TimeRange{ std::stod(parts[1]), std::stod(parts[2]) };
			else if (parts[0] == "cursor" && parts.size() >= 3) cursors[std::stoul(parts[1]) % 2] = std::stod(parts[2]);
		}

		// The last signal with that name: the one just loaded rather than an older one with the same name.
		TrackPtr shown;
		for (const TrackPtr& track : ws.library.tracks())
			if (track->name() == current) shown = track;
		if (shown) ws.select(shown);
		ws.playback.position = playhead;
		ws.selection = selection;
		ws.cursors = cursors;
		return {};
	}

} // namespace session
