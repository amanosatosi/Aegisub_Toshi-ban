// SPDX-License-Identifier: ISC
#include "mangetsu_perspective.h"

#include "ass_dialogue.h"

#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <utility>

namespace {
struct PerspectiveTag {
	size_t begin = std::string::npos;
	size_t end = 0;
	bool animated = false;
};

bool ValidArguments(std::string const& args) {
	std::istringstream input(args);
	input.imbue(std::locale::classic());
	input >> std::ws;
	if (input.eof()) return true; // Empty parentheses restore the rectangle.
	for (int i = 0; i < 8; ++i) {
		double value;
		if (!(input >> value) || !std::isfinite(value)) return false;
		if (i != 7) {
			char separator;
			if (!(input >> separator) || separator != ',') return false;
		}
	}
	input >> std::ws;
	return input.eof();
}

PerspectiveTag FindPerspective(std::string const& source) {
	PerspectiveTag tag;
	for (size_t begin = source.find('{'); begin != std::string::npos;) {
		size_t end = source.find('}', begin + 1);
		if (end == std::string::npos) break;
		int depth = 0;
		for (size_t i = begin + 1; i < end; ++i) {
			if (source.compare(i, 8, "\\mpersp(") == 0) {
				size_t close = source.find(')', i + 8);
				if (close < end && ValidArguments(source.substr(i + 8, close - i - 8))) {
					if (depth) tag.animated = true;
					else {
						tag.begin = i;
						tag.end = close + 1;
					}
					i = close;
					continue;
				}
			}
			if (source[i] == '(') ++depth;
			else if (source[i] == ')' && depth > 0) --depth;
		}
		begin = source.find('{', end + 1);
	}
	return tag;
}
}

bool HasAnimatedMangetsuPerspective(AssDialogue const& line) {
	return FindPerspective(line.Text.get()).animated;
}

bool SetMangetsuPerspective(AssDialogue& line, MangetsuPerspectivePins const& pins) {
	double winding = 0;
	for (size_t i = 0; i < pins.size(); ++i) {
		auto edge = pins[(i + 1) % pins.size()] - pins[i];
		auto next = pins[(i + 2) % pins.size()] - pins[(i + 1) % pins.size()];
		double turn = edge.Cross(next);
		if (!std::isfinite(turn) || std::abs(turn) <= 1e-8 || (i && ((turn > 0) != (winding > 0))))
			return false;
		winding = turn;
	}

	auto source = line.Text.get();
	auto tag = FindPerspective(source);
	if (tag.animated) return false;

	std::ostringstream text;
	text.imbue(std::locale::classic());
	text << "\\mpersp(" << std::fixed << std::setprecision(7);
	for (size_t i = 0; i < pins.size(); ++i) {
		if (i) text << ',';
		text << pins[i].X() << ',' << pins[i].Y();
	}
	text << ')';

	if (tag.begin == std::string::npos)
		source.insert(0, '{' + text.str() + '}');
	else
		source.replace(tag.begin, tag.end - tag.begin, text.str());
	line.Text = std::move(source);
	return true;
}
