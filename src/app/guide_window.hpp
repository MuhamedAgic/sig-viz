#pragma once

#include "workspace.hpp"

// Explains the ideas behind the app, one topic at a time, each with an everyday comparison.
// "Show me" loads a fitting signal and says where to look.
class GuideWindow {
public:
	static constexpr const char* title = "Guide";
	bool open = true;

	void draw(Workspace& ws);

private:
	int shown_lesson_ = -1; // the lesson whose "Show me" was pressed last: its hint is shown
};
