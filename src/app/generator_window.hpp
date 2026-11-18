#pragma once

#include <string>

#include "workspace.hpp"
#include "core/generator.hpp"

// Makes new signals from settings: tones (plain, AM, FM, swept), noise and digital data.
class GeneratorWindow {
public:
	static constexpr const char* title = "Generator";
	bool open = true;

	void draw(Workspace& ws);

private:
	sig::generator::Settings settings_;
	std::string              error_;
};
