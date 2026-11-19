#pragma once

#include <vector>

#include "background.hpp"
#include "workspace.hpp"
#include "core/detect.hpp"

// Lists where something happens in the viewer's signal: every stretch clearly above the noise floor.
// Click a burst to select it (the other windows then analyse just that part).
class BurstsWindow {
public:
	static constexpr const char* title = "Bursts";
	bool open = true;

	void draw(Workspace& ws);

private:
	float                                 threshold_db_ = 10.0f;
	TrackPtr                              searched_;          // the signal the list belongs to
	float                                 searched_db_ = 0.0f;
	std::vector<sig::analysis::Burst>     bursts_;
	Background<std::vector<sig::analysis::Burst>> job_;
};
