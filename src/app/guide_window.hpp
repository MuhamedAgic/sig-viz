#pragma once

#include <array>
#include <string>
#include <vector>

#include "workspace.hpp"

// Explains the ideas behind the app, one topic at a time, each with an everyday comparison.
// "Show me" loads a fitting signal and says where to look.
// Puzzles: signals with something hidden in them, to find with the app's tools.
class GuideWindow {
public:
	static constexpr const char* title = "Guide";
	bool                         open = true;

	void draw(Workspace& ws);

	// For the command palette.
	[[nodiscard]] static std::vector<std::string> topics();
	[[nodiscard]] static std::vector<std::string> puzzles();
	void                                          show_topic(int index);
	void                                          show_puzzle(int index);

private:
	void draw_puzzles(Workspace& ws);

	int shown_lesson_ = -1; // the lesson whose "Show me" was pressed last: its hint is shown
	int open_topic_ = -1;   // asked to be opened (palette)
	int open_puzzle_ = -1;

	struct Attempt {
		std::string answer;
		int         tries = 0;
		bool        solved = false;
		bool        hint = false;
	};
	std::array<Attempt, 5> attempts_{};
};
