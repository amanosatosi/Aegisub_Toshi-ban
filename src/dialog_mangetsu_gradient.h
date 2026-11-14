#pragma once

#include <functional>

class AssStyle;
class wxWindow;
namespace agi { struct Context; }
namespace mangetsu {
class StyleMetadataDraft;
bool EditStyleGradients(wxWindow *parent, StyleMetadataDraft& draft, AssStyle const& style,
	int wrap, std::function<void()> refresh);
}
