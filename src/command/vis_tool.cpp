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
#include "../visual_tool_perspective.h"
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

	template<VisualToolPerspectiveSetting M>
	struct visual_tool_persp_setting : public Command {
		CMD_TYPE(COMMAND_VALIDATE | COMMAND_TOGGLE)

		bool Validate(const agi::Context *c) override {
			return c->videoDisplay->ToolIsType(typeid(VisualToolPerspective)) &&
				(c->videoDisplay->GetSubTool() & PERSP_MODE) == PERSP_MODE_ARCH1T3CHT;
		}

		virtual const bool CheckActive(int subtool) {
			return subtool & M;
		}

		virtual const int UpdateSubTool(int subtool) {
			return subtool ^ M;
		}

		bool IsActive(const agi::Context *c) override {
			return Validate(c) && CheckActive(c->videoDisplay->GetSubTool());
		}

		void operator()(agi::Context *c) override {
			if (!c->videoDisplay->ToolIsType(typeid(VisualToolPerspective)))
				c->videoDisplay->SetTool(agi::make_unique<VisualToolPerspective>(c->videoDisplay, c));
			c->videoDisplay->SetSubTool(UpdateSubTool(c->videoDisplay->GetSubTool()));
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

	template<VisualToolPerspectiveSetting M>
	struct visual_tool_persp_mode : public Command {
		CMD_TYPE(COMMAND_VALIDATE | COMMAND_RADIO)

		bool Validate(const agi::Context *c) override {
			return c->videoDisplay->ToolIsType(typeid(VisualToolPerspective));
		}

		bool IsActive(const agi::Context *c) override {
			return Validate(c) && (c->videoDisplay->GetSubTool() & PERSP_MODE) == M;
		}

		void operator()(agi::Context *c) override {
			if (!c->videoDisplay->ToolIsType(typeid(VisualToolPerspective)))
				c->videoDisplay->SetTool(agi::make_unique<VisualToolPerspective>(c->videoDisplay, c));
			c->videoDisplay->SetSubTool((c->videoDisplay->GetSubTool() & ~PERSP_MODE) | M);
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

	struct visual_mode_perspective final : public visual_tool_command<VisualToolPerspective> {
		CMD_NAME("video/tool/perspective")
		CMD_ICON(visual_perspective)
		STR_MENU("Perspective / Distort")
		STR_DISP("Perspective / Distort")
		STR_HELP("Edit true Mangetsu perspective, bilinear distortion, or the arch1t3ct ASS approximation")
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

	struct visual_mode_perspective_true final : public visual_tool_persp_mode<PERSP_MODE_PERSPECTIVE> {
		CMD_NAME("video/tool/perspective/true")
		CMD_ICON(visual_perspective)
		STR_MENU("Perspective")
		STR_DISP("Perspective")
		STR_HELP("True Mangetsu projective corner pin (\\perspective)")
	};

	struct visual_mode_perspective_distort final : public visual_tool_persp_mode<PERSP_MODE_DISTORT> {
		CMD_NAME("video/tool/perspective/distort")
		CMD_ICON(visual_distort)
		STR_MENU("Distort")
		STR_DISP("Distort")
		STR_HELP("Mangetsu bilinear deformation (\\distort)")
	};

	struct visual_mode_perspective_arch1t3cht final : public visual_tool_persp_mode<PERSP_MODE_ARCH1T3CHT> {
		CMD_NAME("video/tool/perspective/arch1t3cht")
		CMD_ICON(visual_perspective_plane)
		STR_MENU("arch1t3ct")
		STR_DISP("arch1t3ct")
		STR_HELP("ASS-compatible perspective approximation")
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
		STR_MENU("Cycle Curved Text Alignment")
		STR_DISP("Curved Text Alignment")
		STR_HELP("Cycle \\ctan start / center / end alignment")

		bool Validate(const agi::Context *c) override {
			return !!c->project->VideoProvider();
		}

		void operator()(agi::Context *c) override {
			if (!c->videoDisplay->ToolIsType(typeid(VisualToolCurvedText)))
				c->videoDisplay->SetTool(agi::make_unique<VisualToolCurvedText>(c->videoDisplay, c));
			c->videoDisplay->SetSubTool(CT_CYCLE_ALIGNMENT);
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

	// Perspective settings
	struct visual_mode_perspective_plane final : public visual_tool_persp_setting<PERSP_OUTER> {
		CMD_NAME("video/tool/perspective/plane")
		CMD_ICON(visual_perspective_plane)
		STR_MENU("Show Surrounding Plane")
		STR_DISP("Show Surrounding Plane")
		STR_HELP("Toggles showing a second quad for the ambient 3D plane.")
	};

	// Perspective settings
	struct visual_mode_perspective_lock_inner final : public visual_tool_persp_setting<PERSP_LOCK_OUTER> {
		CMD_NAME("video/tool/perspective/lock_outer")
		CMD_ICON(visual_perspective_lock_outer)
		STR_MENU("Lock Outer Quad")
		STR_DISP("Lock Outer Quad")
		STR_HELP("When the surrounding plane is also visible, switches which quad is locked. If inactive, the inner quad can only be resized without changing the perspective plane. If active, this holds for the outer quad instead.")

		bool Validate(const agi::Context *c) override {
			return c->videoDisplay->ToolIsType(typeid(VisualToolPerspective)) &&
				(c->videoDisplay->GetSubTool() & PERSP_MODE) == PERSP_MODE_ARCH1T3CHT &&
				(c->videoDisplay->GetSubTool() & PERSP_OUTER);
		}
	};

	struct visual_mode_perspective_grid final : public visual_tool_persp_setting<PERSP_GRID> {
		CMD_NAME("video/tool/perspective/grid")
		CMD_ICON(visual_perspective_grid)
		STR_MENU("Show Grid")
		STR_DISP("Show Grid")
		STR_HELP("Toggles showing a 3D grid in the visual perspective tool")
	};

	struct visual_mode_perspective_orgmode_center : public visual_tool_persp_setting<PERSP_ORGMODE_CENTER> {
		CMD_NAME("video/tool/perspective/orgmode/center")
		CMD_ICON(visual_perspective_orgmode_center)
		STR_MENU("\\org Mode: Center")
		STR_DISP("\\org Mode: Center")
		STR_HELP("Puts \\org at the center of the perspective quad")

		const bool CheckActive(int subtool) override {
			return (subtool & PERSP_ORGMODE) == PERSP_ORGMODE_CENTER;
		}

		const int UpdateSubTool(int subtool) override {
			return (subtool & ~PERSP_ORGMODE) | PERSP_ORGMODE_CENTER;
		}
	};

	struct visual_mode_perspective_orgmode_nofax : public visual_tool_persp_setting<PERSP_ORGMODE_NOFAX> {
		CMD_NAME("video/tool/perspective/orgmode/nofax")
		CMD_ICON(visual_perspective_orgmode_nofax)
		STR_MENU("\\org Mode: No \\fax")
		STR_DISP("\\org Mode: No \\fax")
		STR_HELP("Finds a value for \\org where \\fax can be zero, if possible. Use this mode if your event contains line breaks.")

		const bool CheckActive(int subtool) override {
			return (subtool & PERSP_ORGMODE) == PERSP_ORGMODE_NOFAX;
		}

		const int UpdateSubTool(int subtool) override {
			return (subtool & ~PERSP_ORGMODE) | PERSP_ORGMODE_NOFAX;
		}
	};

	struct visual_mode_perspective_orgmode_keep : public visual_tool_persp_setting<PERSP_ORGMODE_KEEP> {
		CMD_NAME("video/tool/perspective/orgmode/keep")
		CMD_ICON(visual_perspective_orgmode_keep)
		STR_MENU("\\org Mode: Keep")
		STR_DISP("\\org Mode: Keep")
		STR_HELP("Fixes the position of \\org")

		const bool CheckActive(int subtool) override {
			return (subtool & PERSP_ORGMODE) == PERSP_ORGMODE_KEEP;
		}

		const int UpdateSubTool(int subtool) override {
			return (subtool & ~PERSP_ORGMODE) | PERSP_ORGMODE_KEEP;
		}
	};

	struct visual_mode_perspective_orgmode_cycle : public visual_tool_persp_setting<PERSP_ORGMODE> {
		CMD_NAME("video/tool/perspective/orgmode/cycle")
		STR_MENU("Cycle \\org mode")
		STR_DISP("Cycle \\org mode")
		STR_HELP("Cycles through the three \\org modes")

		const bool CheckActive(int subtool) override {
			return false;
		}

		const int UpdateSubTool(int subtool) override {
			int newtool = 0;
			switch (subtool & PERSP_ORGMODE) {
				case PERSP_ORGMODE_CENTER:
					newtool = PERSP_ORGMODE_NOFAX;
					break;
				case PERSP_ORGMODE_NOFAX:
					newtool = PERSP_ORGMODE_KEEP;
					break;
				case PERSP_ORGMODE_KEEP:
					newtool = PERSP_ORGMODE_CENTER;
					break;
				default:
					break;
			}
			return (subtool & ~PERSP_ORGMODE) | newtool;
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
		reg(agi::make_unique<visual_mode_perspective>());
		reg(agi::make_unique<visual_mode_scale>());
		reg(agi::make_unique<visual_mode_clip>());
		reg(agi::make_unique<visual_mode_vector_clip>());
		reg(agi::make_unique<visual_motion_track>());

		reg(agi::make_unique<visual_mode_perspective_true>());
		reg(agi::make_unique<visual_mode_perspective_distort>());
		reg(agi::make_unique<visual_mode_perspective_arch1t3cht>());
		reg(agi::make_unique<visual_mode_perspective_plane>());
		reg(agi::make_unique<visual_mode_perspective_lock_inner>());
		reg(agi::make_unique<visual_mode_perspective_grid>());
		reg(agi::make_unique<visual_mode_perspective_orgmode_center>());
		reg(agi::make_unique<visual_mode_perspective_orgmode_nofax>());
		reg(agi::make_unique<visual_mode_perspective_orgmode_keep>());
		reg(agi::make_unique<visual_mode_perspective_orgmode_cycle>());

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
