// Copyright (c) 2011, Thomas Goyne <plorkyeran@aegisub.org>
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
// Aegisub Project http://www.aegisub.org/

#include "command.h"

#include "../dialog_manager.h"
#include "../dialog_motion_track.h"
#include "../include/aegisub/context.h"
#include "../libresrc/libresrc.h"
#include "../motion_tracking/motion_track_engine.h"
#include "../project.h"
#include "../video_controller.h"
#include "../video_display.h"
#include "../visual_tool_clip.h"
#include "../visual_tool_cross.h"
#include "../visual_tool_curved_text.h"
#include "../visual_tool_drag.h"
#include "../visual_tool_distort.h"
#include "../visual_tool_rotatexy.h"
#include "../visual_tool_rotatez.h"
#include "../visual_tool_scale.h"
#include "../visual_tool_vector_clip.h"

#include <libaegisub/make_unique.h>

#include <algorithm>
#include <wx/msgdlg.h>

namespace {
	using cmd::Command;

	template<class T>
	struct visual_tool_command : public Command {
		CMD_TYPE(COMMAND_VALIDATE | COMMAND_RADIO)

		bool Validate(const agi::Context *c) override {
			return !!c->project->VideoProvider();
		}

		bool IsActive(const agi::Context *c) override {
			return c->videoDisplay->ToolIsType(typeid(T));
		}

		void operator()(agi::Context *c) override {
			c->videoDisplay->SetTool(agi::make_unique<T>(c->videoDisplay, c));
		}
	};

	template<VisualToolVectorClipMode M>
	struct visual_tool_vclip_command : public Command {
		CMD_TYPE(COMMAND_VALIDATE | COMMAND_RADIO)

		bool Validate(const agi::Context *c) override {
			return !!c->project->VideoProvider();
		}

		bool IsActive(const agi::Context *c) override {
			return c->videoDisplay->ToolIsType(typeid(VisualToolVectorClip)) && c->videoDisplay->GetSubTool() == M;
		}

		void operator()(agi::Context *c) override {
			c->videoDisplay->SetTool(agi::make_unique<VisualToolVectorClip>(c->videoDisplay, c));
			c->videoDisplay->SetSubTool(M);
		}
	};

	template<VisualToolCurvedTextMode M>
	struct visual_tool_curved_text_command : public Command {
		CMD_TYPE(COMMAND_VALIDATE | COMMAND_RADIO)

		bool Validate(const agi::Context *c) override {
			return !!c->project->VideoProvider();
		}

		bool IsActive(const agi::Context *c) override {
			return c->videoDisplay->ToolIsType(typeid(VisualToolCurvedText)) && c->videoDisplay->GetSubTool() == M;
		}

		void operator()(agi::Context *c) override {
			if (!c->videoDisplay->ToolIsType(typeid(VisualToolCurvedText)))
				c->videoDisplay->SetTool(agi::make_unique<VisualToolCurvedText>(c->videoDisplay, c));
			c->videoDisplay->SetSubTool(M);
		}
	};

	struct visual_mode_cross final : public visual_tool_command<VisualToolCross> {
		CMD_NAME("video/tool/cross")
		CMD_ICON(visual_standard)
		STR_MENU("Standard")
		STR_DISP("Standard")
		STR_HELP("Standard mode, double click sets position")
	};

	struct visual_mode_drag final : public visual_tool_command<VisualToolDrag> {
		CMD_NAME("video/tool/drag")
		CMD_ICON(visual_move)
		STR_MENU("Drag")
		STR_DISP("Drag")
		STR_HELP("Drag subtitles")
	};

	struct visual_mode_rotate_z final : public visual_tool_command<VisualToolRotateZ> {
		CMD_NAME("video/tool/rotate/z")
		CMD_ICON(visual_rotatez)
		STR_MENU("Rotate Z")
		STR_DISP("Rotate Z")
		STR_HELP("Rotate subtitles on their Z axis")
	};

	struct visual_mode_rotate_xy final : public visual_tool_command<VisualToolRotateXY> {
		CMD_NAME("video/tool/rotate/xy")
		CMD_ICON(visual_rotatexy)
		STR_MENU("Rotate XY")
		STR_DISP("Rotate XY")
		STR_HELP("Rotate subtitles on their X and Y axes")
	};

	struct visual_mode_distort : public visual_tool_command<VisualToolDistort> {
		CMD_NAME("video/tool/distort")
		CMD_ICON(visual_distort)
		STR_MENU("Mangetsu Distort")
		STR_DISP("Mangetsu Distort")
		STR_HELP("Edit eight-value Mangetsu distortion with four corner handles")
	};

	// Compatibility for existing user hotkeys; activates only the Distort tool.
	struct visual_mode_perspective final : public visual_mode_distort {
		CMD_NAME("video/tool/perspective")
	};

	struct visual_mode_curved_text final : public visual_tool_command<VisualToolCurvedText> {
		CMD_NAME("video/tool/curved_text")
		CMD_ICON(visual_curved_text)
		STR_MENU("Curved Text")
		STR_DISP("Curved Text")
		STR_HELP("Create and edit Mangetsu \\ct text-on-path visually")
	};

	struct visual_mode_scale final : public visual_tool_command<VisualToolScale> {
		CMD_NAME("video/tool/scale")
		CMD_ICON(visual_scale)
		STR_MENU("Scale")
		STR_DISP("Scale")
		STR_HELP("Scale subtitles on X and Y axes")
	};

	struct visual_mode_clip final : public visual_tool_command<VisualToolClip> {
		CMD_NAME("video/tool/clip")
		CMD_ICON(visual_clip)
		STR_MENU("Clip")
		STR_DISP("Clip")
		STR_HELP("Clip subtitles to a rectangle")
	};

	struct visual_mode_vector_clip final : public visual_tool_command<VisualToolVectorClip> {
		CMD_NAME("video/tool/vector_clip")
		CMD_ICON(visual_vector_clip)
		STR_MENU("Vector Clip")
		STR_DISP("Vector Clip")
		STR_HELP("Clip subtitles to a vectorial area")
	};

	struct visual_motion_track final : public Command {
		CMD_NAME("video/tool/motion_track")
		CMD_ICON(button_motion_track)
		STR_MENU("Motion Track")
		STR_DISP("Motion Track")
		STR_HELP("Track an object and export After Effects keyframe data")
		CMD_TYPE(COMMAND_VALIDATE)

		bool Validate(const agi::Context *c) override {
			return !!c->project->VideoProvider();
		}

		void operator()(agi::Context *c) override {
			c->videoController->Stop();
			if (!motion_tracking::MotionTrackEngine::IsAvailable()) {
				wxMessageBox(_("Motion Track requires OpenCV, but this build was configured without OpenCV."),
					_("Motion Track"), wxOK | wxICON_INFORMATION, c->parent);
				return;
			}
			c->dialog->Show<DialogMotionTrack>(c);
		}
	};

	struct visual_mode_curved_text_arc final : public visual_tool_curved_text_command<CT_ARC> {
		CMD_NAME("video/tool/curved_text/arc")
		CMD_ICON(visual_curved_text)
		STR_MENU("Arc")
		STR_DISP("Arc")
		STR_HELP("Edit the curved baseline with start, bend and end handles")
	};

	struct visual_mode_curved_text_edit final : public visual_tool_curved_text_command<CT_EDIT_PATH> {
		CMD_NAME("video/tool/curved_text/edit")
		CMD_ICON(visual_curved_text_edit)
		STR_MENU("Path")
		STR_DISP("Path")
		STR_HELP("Edit Mangetsu \\ct path nodes and Bezier controls")
	};

	struct visual_mode_curved_text_insert final : public visual_tool_curved_text_command<CT_INSERT_PATH_POINT> {
		CMD_NAME("video/tool/curved_text/insert")
		CMD_ICON(visual_vector_clip_insert)
		STR_MENU("Insert Path Point")
		STR_DISP("Insert Path Point")
		STR_HELP("Split the nearest curved-text line or Bezier segment")
	};

	struct visual_mode_curved_text_remove_point final : public visual_tool_curved_text_command<CT_REMOVE_PATH_POINT> {
		CMD_NAME("video/tool/curved_text/remove_point")
		CMD_ICON(visual_vector_clip_remove)
		STR_MENU("Remove Path Point")
		STR_DISP("Remove Path Point")
		STR_HELP("Remove a curved-text node or convert a Bezier by removing a control")
	};

	struct visual_mode_curved_text_move final : public visual_tool_curved_text_command<CT_MOVE_PATH> {
		CMD_NAME("video/tool/curved_text/move")
		CMD_ICON(visual_curved_text_move)
		STR_MENU("Move Whole Path")
		STR_DISP("Move Whole Path")
		STR_HELP("Move the local \\ct path without changing subtitle position")
	};

	struct visual_mode_curved_text_ctx final : public visual_tool_curved_text_command<CT_ALONG_OFFSET> {
		CMD_NAME("video/tool/curved_text/ctx")
		CMD_ICON(visual_curved_text_ctx)
		STR_MENU("Along-Path Offset")
		STR_DISP("Along-Path Offset")
		STR_HELP("Drag text along the path by editing \\ctx")
	};

	struct visual_mode_curved_text_cty final : public visual_tool_curved_text_command<CT_NORMAL_OFFSET> {
		CMD_NAME("video/tool/curved_text/cty")
		CMD_ICON(visual_curved_text_cty)
		STR_MENU("Normal Offset")
		STR_DISP("Normal Offset")
		STR_HELP("Drag text perpendicular to the path by editing \\cty")
	};

	struct visual_mode_curved_text_ctan final : public Command {
		CMD_NAME("video/tool/curved_text/ctan")
		CMD_ICON(visual_curved_text_ctan)
		CMD_TYPE(COMMAND_VALIDATE)
		STR_MENU("Choose Curved Text Anchor")
		STR_DISP("Curved Text Alignment")
		STR_HELP("Choose a nine-way \\ctan curve anchor or the legacy baseline")

		bool Validate(const agi::Context *c) override {
			return !!c->project->VideoProvider();
		}

		void operator()(agi::Context *c) override {
			if (!c->videoDisplay->ToolIsType(typeid(VisualToolCurvedText)))
				c->videoDisplay->SetTool(agi::make_unique<VisualToolCurvedText>(c->videoDisplay, c));
			c->videoDisplay->SetSubTool(CT_CHOOSE_ALIGNMENT);
		}
	};

	struct visual_mode_curved_text_reset final : public Command {
		CMD_NAME("video/tool/curved_text/reset")
		CMD_ICON(visual_vector_clip_line)
		CMD_TYPE(COMMAND_VALIDATE)
		STR_MENU("Reset Straight")
		STR_DISP("Reset Straight")
		STR_HELP("Keep the curved-text endpoints and remove all curvature")

		bool Validate(const agi::Context *c) override { return !!c->project->VideoProvider(); }
		void operator()(agi::Context *c) override {
			if (!c->videoDisplay->ToolIsType(typeid(VisualToolCurvedText)))
				c->videoDisplay->SetTool(agi::make_unique<VisualToolCurvedText>(c->videoDisplay, c));
			c->videoDisplay->SetSubTool(CT_RESET_STRAIGHT);
		}
	};

	struct visual_mode_curved_text_reverse final : public Command {
		CMD_NAME("video/tool/curved_text/reverse")
		CMD_ICON(arrow_sort)
		CMD_TYPE(COMMAND_VALIDATE)
		STR_MENU("Reverse Path")
		STR_DISP("Reverse Path")
		STR_HELP("Reverse the curved-text baseline direction without changing its shape")

		bool Validate(const agi::Context *c) override { return !!c->project->VideoProvider(); }
		void operator()(agi::Context *c) override {
			if (!c->videoDisplay->ToolIsType(typeid(VisualToolCurvedText)))
				c->videoDisplay->SetTool(agi::make_unique<VisualToolCurvedText>(c->videoDisplay, c));
			c->videoDisplay->SetSubTool(CT_REVERSE_PATH);
		}
	};

	struct visual_mode_curved_text_remove final : public Command {
		CMD_NAME("video/tool/curved_text/remove")
		CMD_ICON(visual_vector_clip_remove)
		CMD_TYPE(COMMAND_VALIDATE)
		STR_MENU("Remove Curve")
		STR_DISP("Remove Curve")
		STR_HELP("Remove static \\ct paths without changing any unrelated tags or text")

		bool Validate(const agi::Context *c) override { return !!c->project->VideoProvider(); }
		void operator()(agi::Context *c) override {
			if (!c->videoDisplay->ToolIsType(typeid(VisualToolCurvedText)))
				c->videoDisplay->SetTool(agi::make_unique<VisualToolCurvedText>(c->videoDisplay, c));
			c->videoDisplay->SetSubTool(CT_REMOVE_CURVE);
		}
	};

	// Vector clip tools

	struct visual_mode_vclip_drag final : public visual_tool_vclip_command<VCLIP_DRAG> {
		CMD_NAME("video/tool/vclip/drag")
		CMD_ICON(visual_vector_clip_drag)
		STR_MENU("Drag")
		STR_DISP("Drag")
		STR_HELP("Drag control points")
	};

	struct visual_mode_vclip_line final : public visual_tool_vclip_command<VCLIP_LINE> {
		CMD_NAME("video/tool/vclip/line")
		CMD_ICON(visual_vector_clip_line)
		STR_MENU("Line")
		STR_DISP("Line")
		STR_HELP("Appends a line")
	};
	struct visual_mode_vclip_bicubic final : public visual_tool_vclip_command<VCLIP_BICUBIC> {
		CMD_NAME("video/tool/vclip/bicubic")
		CMD_ICON(visual_vector_clip_bicubic)
		STR_MENU("Bicubic")
		STR_DISP("Bicubic")
		STR_HELP("Appends a bezier bicubic curve")
	};
	struct visual_mode_vclip_convert final : public visual_tool_vclip_command<VCLIP_CONVERT> {
		CMD_NAME("video/tool/vclip/convert")
		CMD_ICON(visual_vector_clip_convert)
		STR_MENU("Convert")
		STR_DISP("Convert")
		STR_HELP("Converts a segment between line and bicubic")
	};
	struct visual_mode_vclip_insert final : public visual_tool_vclip_command<VCLIP_INSERT> {
		CMD_NAME("video/tool/vclip/insert")
		CMD_ICON(visual_vector_clip_insert)
		STR_MENU("Insert")
		STR_DISP("Insert")
		STR_HELP("Inserts a control point")
	};
	struct visual_mode_vclip_remove final : public visual_tool_vclip_command<VCLIP_REMOVE> {
		CMD_NAME("video/tool/vclip/remove")
		CMD_ICON(visual_vector_clip_remove)
		STR_MENU("Remove")
		STR_DISP("Remove")
		STR_HELP("Removes a control point")
	};
	struct visual_mode_vclip_freehand final : public visual_tool_vclip_command<VCLIP_FREEHAND> {
		CMD_NAME("video/tool/vclip/freehand")
		CMD_ICON(visual_vector_clip_freehand)
		STR_MENU("Freehand")
		STR_DISP("Freehand")
		STR_HELP("Draws a freehand shape")
	};
	struct visual_mode_vclip_freehand_smooth final : public visual_tool_vclip_command<VCLIP_FREEHAND_SMOOTH> {
		CMD_NAME("video/tool/vclip/freehand_smooth")
		CMD_ICON(visual_vector_clip_freehand_smooth)
		STR_MENU("Freehand smooth")
		STR_DISP("Freehand smooth")
		STR_HELP("Draws a smoothed freehand shape")
	};
}

namespace cmd {
	void init_visual_tools() {
		reg(agi::make_unique<visual_mode_cross>());
		reg(agi::make_unique<visual_mode_drag>());
		reg(agi::make_unique<visual_mode_rotate_z>());
		reg(agi::make_unique<visual_mode_rotate_xy>());
		reg(agi::make_unique<visual_mode_curved_text>());
		reg(agi::make_unique<visual_mode_distort>());
		reg(agi::make_unique<visual_mode_perspective>());
		reg(agi::make_unique<visual_mode_scale>());
		reg(agi::make_unique<visual_mode_clip>());
		reg(agi::make_unique<visual_mode_vector_clip>());
		reg(agi::make_unique<visual_motion_track>());

		reg(agi::make_unique<visual_mode_curved_text_arc>());
		reg(agi::make_unique<visual_mode_curved_text_edit>());
		reg(agi::make_unique<visual_mode_curved_text_insert>());
		reg(agi::make_unique<visual_mode_curved_text_remove_point>());
		reg(agi::make_unique<visual_mode_curved_text_move>());
		reg(agi::make_unique<visual_mode_curved_text_ctx>());
		reg(agi::make_unique<visual_mode_curved_text_cty>());
		reg(agi::make_unique<visual_mode_curved_text_ctan>());
		reg(agi::make_unique<visual_mode_curved_text_reset>());
		reg(agi::make_unique<visual_mode_curved_text_reverse>());
		reg(agi::make_unique<visual_mode_curved_text_remove>());

		reg(agi::make_unique<visual_mode_vclip_drag>());
		reg(agi::make_unique<visual_mode_vclip_line>());
		reg(agi::make_unique<visual_mode_vclip_bicubic>());
		reg(agi::make_unique<visual_mode_vclip_convert>());
		reg(agi::make_unique<visual_mode_vclip_insert>());
		reg(agi::make_unique<visual_mode_vclip_remove>());
		reg(agi::make_unique<visual_mode_vclip_freehand>());
		reg(agi::make_unique<visual_mode_vclip_freehand_smooth>());
	}
}
