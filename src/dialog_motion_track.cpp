#include "dialog_motion_track.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "ass_style.h"
#include "async_video_provider.h"
#include "compat.h"
#include "format.h"
#include "include/aegisub/context.h"
#include "include/aegisub/subtitles_provider.h"
#include "libresrc/libresrc.h"
#include "motion_tracking/motion_track_engine.h"
#include "motion_tracking/motion_track_export_ae.h"
#include "motion_tracking/motion_track_commit.h"
#include "options.h"
#include "persist_location.h"
#include "project.h"
#include "selection_controller.h"
#include "utils.h"
#include "video_controller.h"
#include "video_frame.h"

#include <libaegisub/ass/time.h>
#include <libaegisub/exception.h>
#include <libaegisub/make_unique.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <functional>
#include <fstream>
#include <list>
#include <limits>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dcbuffer.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/timer.h>
#include <wx/utils.h>

namespace {
constexpr double pi = 3.14159265358979323846;

double DegToRad(double deg) {
	return deg * pi / 180.0;
}

wxPoint RotatePoint(double x, double y, double angle) {
	double c = std::cos(angle);
	double s = std::sin(angle);
	return wxPoint(static_cast<int>(std::lround(x * c - y * s)), static_cast<int>(std::lround(x * s + y * c)));
}

bool ModeHasSize(motion_tracking::MotionTrackMode mode) {
	return mode == motion_tracking::MotionTrackMode::PositionSize || mode == motion_tracking::MotionTrackMode::PositionSizeRotation;
}

bool ModeHasRotation(motion_tracking::MotionTrackMode mode) {
	return mode == motion_tracking::MotionTrackMode::PositionRotation || mode == motion_tracking::MotionTrackMode::PositionSizeRotation;
}

size_t FrameMemoryEstimate(int width, int height, int frame_count) {
	if (width <= 0 || height <= 0 || frame_count <= 0)
		return 0;

	auto frame_bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
	if (frame_bytes == 0 || static_cast<size_t>(frame_count) > std::numeric_limits<size_t>::max() / frame_bytes)
		return std::numeric_limits<size_t>::max();
	return frame_bytes * static_cast<size_t>(frame_count);
}
}

class MotionTrackFrameCache final {
	using Loader = std::function<std::shared_ptr<VideoFrame>(int)>;

	Loader loader;
	mutable std::mutex mutex;
	std::condition_variable condition;
	std::map<int, std::shared_ptr<VideoFrame>> frames;
	std::map<int, size_t> frame_bytes;
	std::list<int> lru;
	std::map<int, std::list<int>::iterator> lru_positions;
	std::set<int> loading;
	std::vector<int> cache_order;
	std::thread worker;
	std::atomic<bool> stop_requested{false};
	std::atomic<bool> background_enabled{false};
	std::atomic<bool> complete{false};
	size_t max_bytes = 512ULL * 1024ULL * 1024ULL;
	size_t current_bytes = 0;
	size_t estimated_bytes = 0;
	int target_count = 0;

	size_t BytesForFrame(VideoFrame const& frame) const {
		return frame.data.size();
	}

	void TouchLocked(int frame) {
		auto pos = lru_positions.find(frame);
		if (pos != lru_positions.end()) {
			lru.erase(pos->second);
			lru_positions.erase(pos);
		}
		lru.push_front(frame);
		lru_positions[frame] = lru.begin();
	}

	void EraseLocked(int frame) {
		auto frame_it = frames.find(frame);
		if (frame_it == frames.end())
			return;

		auto bytes_it = frame_bytes.find(frame);
		if (bytes_it != frame_bytes.end()) {
			current_bytes -= std::min(current_bytes, bytes_it->second);
			frame_bytes.erase(bytes_it);
		}
		auto lru_it = lru_positions.find(frame);
		if (lru_it != lru_positions.end()) {
			lru.erase(lru_it->second);
			lru_positions.erase(lru_it);
		}
		frames.erase(frame_it);
	}

	void EvictLocked(int protected_frame) {
		while (current_bytes > max_bytes && !lru.empty()) {
			int victim = lru.back();
			if (victim == protected_frame && lru.size() == 1)
				break;

			if (victim == protected_frame) {
				lru.pop_back();
				lru.push_front(victim);
				lru_positions[victim] = lru.begin();
				continue;
			}

			EraseLocked(victim);
		}
	}

	void StoreLocked(int frame, std::shared_ptr<VideoFrame> data) {
		if (!data)
			return;

		EraseLocked(frame);
		frames[frame] = data;
		size_t bytes = BytesForFrame(*data);
		frame_bytes[frame] = bytes;
		current_bytes += bytes;
		TouchLocked(frame);
		EvictLocked(frame);
	}

	void WorkerMain() {
		for (int frame : cache_order) {
			if (stop_requested)
				break;
			try {
				GetFrameBlockingOrLoad(frame);
			}
			catch (...) {
				std::lock_guard<std::mutex> lock(mutex);
				loading.erase(frame);
				condition.notify_all();
			}
		}
		complete = true;
		condition.notify_all();
	}

public:
	explicit MotionTrackFrameCache(Loader loader)
	: loader(std::move(loader)) {
	}

	~MotionTrackFrameCache() {
		Stop();
	}

	void Start(int start, int end, int width, int height, int priority_frame) {
		Stop();
		Clear();

		target_count = std::max(0, end - start + 1);
		estimated_bytes = FrameMemoryEstimate(width, height, target_count);
		stop_requested = false;
		complete = false;
		background_enabled = estimated_bytes <= max_bytes;

		if (target_count <= 0) {
			complete = true;
			return;
		}

		if (!background_enabled) {
			complete = true;
			return;
		}

		priority_frame = mid(start, priority_frame, end);
		cache_order.reserve(static_cast<size_t>(target_count));
		cache_order.push_back(priority_frame);
		for (int distance = 1; static_cast<int>(cache_order.size()) < target_count; ++distance) {
			int forward = priority_frame + distance;
			int backward = priority_frame - distance;
			if (forward <= end)
				cache_order.push_back(forward);
			if (backward >= start)
				cache_order.push_back(backward);
		}

		worker = std::thread([this] { WorkerMain(); });
	}

	void Stop() {
		stop_requested = true;
		condition.notify_all();
		if (worker.joinable())
			worker.join();
	}

	void Clear() {
		std::lock_guard<std::mutex> lock(mutex);
		frames.clear();
		frame_bytes.clear();
		lru.clear();
		lru_positions.clear();
		loading.clear();
		cache_order.clear();
		current_bytes = 0;
	}

	std::shared_ptr<VideoFrame> GetFrameBlockingOrLoad(int frame) {
		{
			std::unique_lock<std::mutex> lock(mutex);
			auto it = frames.find(frame);
			if (it != frames.end()) {
				TouchLocked(frame);
				return it->second;
			}

			while (!stop_requested && loading.count(frame)) {
				condition.wait(lock);
				it = frames.find(frame);
				if (it != frames.end()) {
					TouchLocked(frame);
					return it->second;
				}
			}

			if (stop_requested)
				return {};
			loading.insert(frame);
		}

		std::shared_ptr<VideoFrame> loaded;
		try {
			loaded = loader(frame);
		}
		catch (...) {
			std::lock_guard<std::mutex> lock(mutex);
			loading.erase(frame);
			condition.notify_all();
			throw;
		}

		{
			std::lock_guard<std::mutex> lock(mutex);
			loading.erase(frame);
			StoreLocked(frame, loaded);
		}
		condition.notify_all();
		return loaded;
	}

	int CachedCount() const {
		std::lock_guard<std::mutex> lock(mutex);
		return static_cast<int>(frames.size());
	}

	int TargetCount() const {
		return target_count;
	}

	bool BackgroundEnabled() const {
		return background_enabled;
	}

	bool Complete() const {
		return complete;
	}

};

class MotionTrackFrameBar final : public wxPanel {
	DialogMotionTrack *dialog = nullptr;
	bool dragging = false;

	int FrameFromX(int x) const {
		int start = dialog->GetStartFrame();
		int end = dialog->GetEndFrame();
		if (end <= start)
			return start;

		int width = GetClientSize().GetWidth();
		int pad = 16;
		double t = (x - pad) / static_cast<double>(std::max(1, width - pad * 2));
		t = std::clamp(t, 0.0, 1.0);
		return static_cast<int>(std::lround(start + t * (end - start)));
	}

	void OnPaint(wxPaintEvent &) {
		wxAutoBufferedPaintDC dc(this);
		dc.SetBackground(wxBrush(GetBackgroundColour()));
		dc.Clear();

		wxSize size = GetClientSize();
		int pad = 16;
		int y = size.y / 2;
		int left = pad;
		int right = std::max(left + 1, size.x - pad);

		dc.SetPen(wxPen(wxColour(120, 120, 120), 2));
		dc.DrawLine(left, y, right, y);

		int start = dialog->GetStartFrame();
		int end = dialog->GetEndFrame();
		for (int mark : dialog->GetHandoffMarks()) {
			if (mark <= start || mark >= end)
				continue;
			double mt = end > start ? (mark - start) / static_cast<double>(end - start) : 0.0;
			int mark_x = static_cast<int>(std::lround(left + std::clamp(mt, 0.0, 1.0) * (right - left)));
			dc.SetPen(wxPen(wxColour(230, 150, 40), 2));
			dc.DrawLine(mark_x, y - 8, mark_x, y + 8);
		}

		int current = dialog->GetCurrentFrame();
		double t = end > start ? (current - start) / static_cast<double>(end - start) : 0.0;
		t = std::clamp(t, 0.0, 1.0);
		int x = static_cast<int>(std::lround(left + t * (right - left)));

		wxPoint arrow[] = {
			wxPoint(x, y - 9),
			wxPoint(x - 6, y - 1),
			wxPoint(x + 6, y - 1)
		};
		dc.SetBrush(wxBrush(wxColour(65, 116, 180)));
		dc.SetPen(wxPen(wxColour(45, 80, 130), 1));
		dc.DrawPolygon(3, arrow);
		dc.DrawLine(x, y, x, y + 9);
	}

	void OnMouse(wxMouseEvent &evt) {
		if (evt.LeftDown()) {
			dragging = true;
			CaptureMouse();
			dialog->JumpToFrame(FrameFromX(evt.GetX()));
		}
		else if (evt.LeftUp()) {
			dragging = false;
			if (HasCapture())
				ReleaseMouse();
		}
		else if (dragging && evt.Dragging()) {
			dialog->JumpToFrame(FrameFromX(evt.GetX()));
		}
		else {
			evt.Skip();
		}
	}

public:
	MotionTrackFrameBar(wxWindow *parent, DialogMotionTrack *dialog)
	: wxPanel(parent, -1, wxDefaultPosition, wxSize(-1, 28), wxBORDER_SIMPLE)
	, dialog(dialog) {
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		Bind(wxEVT_PAINT, &MotionTrackFrameBar::OnPaint, this);
		Bind(wxEVT_LEFT_DOWN, &MotionTrackFrameBar::OnMouse, this);
		Bind(wxEVT_LEFT_UP, &MotionTrackFrameBar::OnMouse, this);
		Bind(wxEVT_MOTION, &MotionTrackFrameBar::OnMouse, this);
	}
};

class MotionTrackPreviewPanel final : public wxPanel {
	enum class DragMode {
		NoDrag,
		Pan,
		Move,
		Handle
	};

	DialogMotionTrack *dialog = nullptr;
	bool fit_mode = true;
	bool fast_render = false;
	double scale = 1.0;
	wxPoint2DDouble offset{0.0, 0.0};
	wxPoint2DDouble last_video_pos{0.0, 0.0};
	wxPoint last_mouse_pos;
	DragMode drag_mode = DragMode::NoDrag;
	wxBitmap cached_view;
	wxRect cached_source_rect;
	wxPoint cached_draw_pos;
	wxSize cached_draw_size;
	int cached_frame = -1;
	bool cached_fast_render = false;

	wxPoint2DDouble ImageToScreen(double x, double y) const {
		return {offset.m_x + x * scale, offset.m_y + y * scale};
	}

	wxPoint2DDouble ScreenToImage(wxPoint const& point) const {
		return {(point.x - offset.m_x) / scale, (point.y - offset.m_y) / scale};
	}

	void UpdateFitTransform() {
		auto const& image = dialog->GetPreviewImage();
		if (!image.IsOk())
			return;

		wxSize client = GetClientSize();
		scale = std::min(
			client.x / static_cast<double>(std::max(1, image.GetWidth())),
			client.y / static_cast<double>(std::max(1, image.GetHeight())));
		if (!std::isfinite(scale) || scale <= 0.0)
			scale = 1.0;
		offset.m_x = (client.x - image.GetWidth() * scale) / 2.0;
		offset.m_y = (client.y - image.GetHeight() * scale) / 2.0;
	}

	void DrawTrailMarkers(wxDC &dc) {
		if (!dialog->GetShowTrackTrail())
			return;

		auto trail = dialog->GetTrackTrailMarkers();
		if (trail.empty())
			return;

		int current = dialog->GetCurrentFrame();
		int past = std::max(1, dialog->GetTrackTrailPast());
		int future = std::max(1, dialog->GetTrackTrailFuture());

		for (auto const& item : trail) {
			auto marker = item.marker;
			double angle = DegToRad(marker.rotation_deg);
			double half = marker.size / 2.0;
			auto center = ImageToScreen(marker.cx, marker.cy);
			wxPoint c(static_cast<int>(std::lround(center.m_x)), static_cast<int>(std::lround(center.m_y)));

			int distance = std::max(1, std::abs(item.frame - current));
			int max_distance = item.frame < current ? past : future;
			double proximity = 1.0 - (distance - 1) / static_cast<double>(std::max(1, max_distance));
			proximity = std::clamp(proximity, 0.0, 1.0);
			int alpha = static_cast<int>(std::lround(70 + proximity * 45));
			if (item.state == motion_tracking::MotionTrackState::Predicted)
				alpha = static_cast<int>(alpha * 0.65);
			else if (item.state == motion_tracking::MotionTrackState::WeakTracked)
				alpha = static_cast<int>(alpha * 0.8);

			wxPoint corners[4];
			wxPoint rel[] = {
				RotatePoint(-half * scale, -half * scale, angle),
				RotatePoint( half * scale, -half * scale, angle),
				RotatePoint( half * scale,  half * scale, angle),
				RotatePoint(-half * scale,  half * scale, angle)
			};
			for (int i = 0; i < 4; ++i)
				corners[i] = wxPoint(c.x + rel[i].x, c.y + rel[i].y);

			wxPen pen(wxColour(255, 45, 45, alpha), 1,
				item.state == motion_tracking::MotionTrackState::Predicted ? wxPENSTYLE_SHORT_DASH : wxPENSTYLE_SOLID);
			dc.SetPen(pen);
			dc.SetBrush(*wxTRANSPARENT_BRUSH);
			dc.DrawPolygon(4, corners);

			wxPoint handle_rel = RotatePoint(half * scale, 0.0, angle);
			wxPoint handle(c.x + handle_rel.x, c.y + handle_rel.y);
			dc.DrawLine(c, handle);
			dc.SetBrush(wxBrush(wxColour(255, 45, 45, alpha)));
			dc.SetPen(wxPen(wxColour(255, 45, 45, alpha), 1));
			dc.DrawCircle(c, 2);
		}
	}

	void DrawMarker(wxDC &dc) {
		if (!dialog->HasCurrentMarker())
			return;

		auto marker = dialog->GetCurrentMarker();
		int segment_state = dialog->GetCurrentSegmentVisualState();
		wxColour marker_colour = segment_state == 1 ? wxColour(65, 145, 220) :
			segment_state == 2 ? wxColour(230, 120, 20) : wxColour(150, 150, 150);
		wxColour search_colour = segment_state == 1 ? wxColour(255, 170, 60) :
			segment_state == 2 ? wxColour(220, 90, 50) : wxColour(120, 120, 120);
		double angle = DegToRad(marker.rotation_deg);
		double half = marker.size / 2.0;
		double search_half = std::max(marker.search_size, marker.size) / 2.0;
		auto center = ImageToScreen(marker.cx, marker.cy);

		auto draw_square = [&](double h, wxPen pen) {
			wxPoint corners[4];
			wxPoint rel[] = {
				RotatePoint(-h * scale, -h * scale, angle),
				RotatePoint( h * scale, -h * scale, angle),
				RotatePoint( h * scale,  h * scale, angle),
				RotatePoint(-h * scale,  h * scale, angle)
			};
			for (int i = 0; i < 4; ++i)
				corners[i] = wxPoint(static_cast<int>(std::lround(center.m_x)) + rel[i].x, static_cast<int>(std::lround(center.m_y)) + rel[i].y);
			dc.SetPen(pen);
			dc.SetBrush(*wxTRANSPARENT_BRUSH);
			dc.DrawPolygon(4, corners);
		};

		draw_square(search_half, wxPen(search_colour, 1, wxPENSTYLE_SHORT_DASH));
		draw_square(half, wxPen(marker_colour, 2));

		wxPoint handle_rel = RotatePoint(half * scale, 0.0, angle);
		wxPoint c(static_cast<int>(std::lround(center.m_x)), static_cast<int>(std::lround(center.m_y)));
		wxPoint handle(c.x + handle_rel.x, c.y + handle_rel.y);

		dc.SetPen(wxPen(wxColour(230, 120, 20), 2));
		dc.DrawLine(c, handle);
		dc.SetBrush(wxBrush(marker_colour));
		dc.SetPen(wxPen(*wxWHITE, 1));
		dc.DrawCircle(c, 4);
		dc.SetBrush(wxBrush(wxColour(230, 120, 20)));
		dc.SetPen(wxPen(wxColour(120, 60, 0), 1));
		dc.DrawCircle(handle, 6);
	}

	bool HitHandle(wxPoint const& point, motion_tracking::MotionTrackMarker const& marker) const {
		double half = marker.size / 2.0;
		auto center = ImageToScreen(marker.cx, marker.cy);
		wxPoint rel = RotatePoint(half * scale, 0.0, DegToRad(marker.rotation_deg));
		double hx = center.m_x + rel.x;
		double hy = center.m_y + rel.y;
		double dx = point.x - hx;
		double dy = point.y - hy;
		return dx * dx + dy * dy <= 100.0;
	}

	bool HitMarker(wxPoint const& point, motion_tracking::MotionTrackMarker const& marker) const {
		auto image = ScreenToImage(point);
		double dx = image.m_x - marker.cx;
		double dy = image.m_y - marker.cy;
		double angle = -DegToRad(marker.rotation_deg);
		double c = std::cos(angle);
		double s = std::sin(angle);
		double lx = dx * c - dy * s;
		double ly = dx * s + dy * c;
		double half = marker.size / 2.0;
		return std::abs(lx) <= half && std::abs(ly) <= half;
	}

	void ZoomAt(wxPoint const& mouse, double factor) {
		if (!dialog->GetPreviewImage().IsOk())
			return;

		if (fit_mode)
			UpdateFitTransform();

		auto image_before = ScreenToImage(mouse);
		scale = std::clamp(scale * factor, 0.05, 16.0);
		offset.m_x = mouse.x - image_before.m_x * scale;
		offset.m_y = mouse.y - image_before.m_y * scale;
		fit_mode = false;
		fast_render = true;
		Refresh(false);
	}

	void DrawImage(wxDC &dc, wxImage const& image) {
		wxSize client = GetClientSize();
		if (client.x <= 0 || client.y <= 0 || scale <= 0.0)
			return;

		double src_left_f = std::max(0.0, (-offset.m_x) / scale);
		double src_top_f = std::max(0.0, (-offset.m_y) / scale);
		double src_right_f = std::min(static_cast<double>(image.GetWidth()), (client.x - offset.m_x) / scale);
		double src_bottom_f = std::min(static_cast<double>(image.GetHeight()), (client.y - offset.m_y) / scale);

		int src_left = std::clamp(static_cast<int>(std::floor(src_left_f)), 0, image.GetWidth());
		int src_top = std::clamp(static_cast<int>(std::floor(src_top_f)), 0, image.GetHeight());
		int src_right = std::clamp(static_cast<int>(std::ceil(src_right_f)), src_left, image.GetWidth());
		int src_bottom = std::clamp(static_cast<int>(std::ceil(src_bottom_f)), src_top, image.GetHeight());
		int src_w = src_right - src_left;
		int src_h = src_bottom - src_top;
		if (src_w <= 0 || src_h <= 0)
			return;

		int dst_x = static_cast<int>(std::floor(offset.m_x + src_left * scale));
		int dst_y = static_cast<int>(std::floor(offset.m_y + src_top * scale));
		int dst_right = static_cast<int>(std::ceil(offset.m_x + src_right * scale));
		int dst_bottom = static_cast<int>(std::ceil(offset.m_y + src_bottom * scale));
		int dst_w = std::max(1, dst_right - dst_x);
		int dst_h = std::max(1, dst_bottom - dst_y);

		wxRect source_rect(src_left, src_top, src_w, src_h);
		wxPoint draw_pos(dst_x, dst_y);
		wxSize draw_size(dst_w, dst_h);
		bool cache_valid =
			cached_view.IsOk() &&
			cached_frame == dialog->GetPreviewFrame() &&
			cached_source_rect.x == source_rect.x &&
			cached_source_rect.y == source_rect.y &&
			cached_source_rect.width == source_rect.width &&
			cached_source_rect.height == source_rect.height &&
			cached_draw_pos.x == draw_pos.x &&
			cached_draw_pos.y == draw_pos.y &&
			cached_draw_size.x == draw_size.x &&
			cached_draw_size.y == draw_size.y &&
			cached_fast_render == fast_render;

		if (!cache_valid) {
			wxImage crop = image.GetSubImage(source_rect);
			wxImage scaled = crop.Scale(dst_w, dst_h, fast_render ? wxIMAGE_QUALITY_NORMAL : wxIMAGE_QUALITY_HIGH);
			cached_view = wxBitmap(scaled);
			cached_source_rect = source_rect;
			cached_draw_pos = draw_pos;
			cached_draw_size = draw_size;
			cached_frame = dialog->GetPreviewFrame();
			cached_fast_render = fast_render;
		}

		dc.DrawBitmap(cached_view, cached_draw_pos.x, cached_draw_pos.y);
	}

	void OnPaint(wxPaintEvent &) {
		wxAutoBufferedPaintDC dc(this);
		dc.SetBackground(wxBrush(wxColour(28, 28, 28)));
		dc.Clear();

		auto const& image = dialog->GetPreviewImage();
		if (!image.IsOk()) {
			dc.SetTextForeground(wxColour(220, 220, 220));
			dc.DrawText(_("No video frame"), 12, 12);
			return;
		}

		if (fit_mode)
			UpdateFitTransform();

		DrawImage(dc, image);
		DrawTrailMarkers(dc);
		DrawMarker(dc);
	}

	void OnMouse(wxMouseEvent &evt) {
		if (!dialog->GetPreviewImage().IsOk()) {
			evt.Skip();
			return;
		}

		if (fit_mode)
			UpdateFitTransform();

		if (evt.MiddleDown()) {
			fit_mode = false;
			drag_mode = DragMode::Pan;
			last_mouse_pos = evt.GetPosition();
			CaptureMouse();
			return;
		}

		if (evt.LeftDown()) {
			SetFocus();
			auto image_pos = ScreenToImage(evt.GetPosition());
			if (dialog->HasCurrentMarker()) {
				auto marker = dialog->GetCurrentMarker();
				if (HitHandle(evt.GetPosition(), marker))
					drag_mode = DragMode::Handle;
				else if (HitMarker(evt.GetPosition(), marker))
					drag_mode = DragMode::Move;
				else {
					dialog->PlaceCurrentMarker(image_pos.m_x, image_pos.m_y);
					drag_mode = DragMode::Move;
				}
			}
			else {
				dialog->PlaceCurrentMarker(image_pos.m_x, image_pos.m_y);
				drag_mode = DragMode::Move;
			}
			last_video_pos = image_pos;
			CaptureMouse();
			return;
		}

		if (evt.LeftUp() || evt.MiddleUp()) {
			drag_mode = DragMode::NoDrag;
			fast_render = false;
			if (HasCapture())
				ReleaseMouse();
			Refresh(false);
			return;
		}

		if (evt.Dragging() && drag_mode != DragMode::NoDrag) {
			if (drag_mode == DragMode::Pan) {
				wxPoint pos = evt.GetPosition();
				offset.m_x += pos.x - last_mouse_pos.x;
				offset.m_y += pos.y - last_mouse_pos.y;
				last_mouse_pos = pos;
				fast_render = true;
				Refresh(false);
				return;
			}

			if (!dialog->HasCurrentMarker())
				return;

			auto marker = dialog->GetCurrentMarker();
			auto image_pos = ScreenToImage(evt.GetPosition());
			if (drag_mode == DragMode::Move) {
				marker.cx += image_pos.m_x - last_video_pos.m_x;
				marker.cy += image_pos.m_y - last_video_pos.m_y;
				last_video_pos = image_pos;
			}
			else if (drag_mode == DragMode::Handle) {
				double dx = image_pos.m_x - marker.cx;
				double dy = image_pos.m_y - marker.cy;
				double radius = std::sqrt(dx * dx + dy * dy);
				marker.size = std::max(4.0, radius * 2.0);
				marker.search_size = std::max(marker.search_size, marker.size);
				marker.rotation_deg = std::atan2(dy, dx) * 180.0 / pi;
			}
			dialog->SetCurrentMarker(marker);
			return;
		}

		evt.Skip();
	}

	void OnMouseWheel(wxMouseEvent &evt) {
		if (!dialog->GetPreviewImage().IsOk())
			return;

		double factor = evt.GetWheelRotation() > 0 ? 1.15 : 1.0 / 1.15;
		ZoomAt(evt.GetPosition(), factor);
	}

	void OnDoubleClick(wxMouseEvent &) {
		Fit();
	}

	void OnIdle(wxIdleEvent &) {
		if (fast_render && drag_mode == DragMode::NoDrag) {
			fast_render = false;
			Refresh(false);
		}
	}

	void OnKeyDown(wxKeyEvent &evt) {
		int code = evt.GetKeyCode();
		if (dialog->HandleNavigationKey(code))
			return;
		if (code == WXK_DELETE || code == WXK_BACK) {
			dialog->DeleteCurrentMarker();
			return;
		}
		if (code == '1') {
			auto const& image = dialog->GetPreviewImage();
			if (image.IsOk()) {
				wxSize client = GetClientSize();
				scale = 1.0;
				offset.m_x = (client.x - image.GetWidth()) / 2.0;
				offset.m_y = (client.y - image.GetHeight()) / 2.0;
				fit_mode = false;
				fast_render = false;
				Refresh(false);
			}
			return;
		}
		if (code == '+' || code == WXK_NUMPAD_ADD || code == '-' || code == WXK_NUMPAD_SUBTRACT) {
			ZoomAt(ScreenToClient(wxGetMousePosition()), (code == '+' || code == WXK_NUMPAD_ADD) ? 1.15 : 1.0 / 1.15);
			return;
		}
		evt.Skip();
	}

public:
	MotionTrackPreviewPanel(wxWindow *parent, DialogMotionTrack *dialog)
	: wxPanel(parent, -1, wxDefaultPosition, wxSize(640, 360), wxBORDER_SIMPLE | wxFULL_REPAINT_ON_RESIZE)
	, dialog(dialog) {
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		SetMinSize(wxSize(320, 220));
		Bind(wxEVT_PAINT, &MotionTrackPreviewPanel::OnPaint, this);
		Bind(wxEVT_LEFT_DOWN, &MotionTrackPreviewPanel::OnMouse, this);
		Bind(wxEVT_LEFT_UP, &MotionTrackPreviewPanel::OnMouse, this);
		Bind(wxEVT_MIDDLE_DOWN, &MotionTrackPreviewPanel::OnMouse, this);
		Bind(wxEVT_MIDDLE_UP, &MotionTrackPreviewPanel::OnMouse, this);
		Bind(wxEVT_MOTION, &MotionTrackPreviewPanel::OnMouse, this);
		Bind(wxEVT_MOUSEWHEEL, &MotionTrackPreviewPanel::OnMouseWheel, this);
		Bind(wxEVT_LEFT_DCLICK, &MotionTrackPreviewPanel::OnDoubleClick, this);
		Bind(wxEVT_KEY_DOWN, &MotionTrackPreviewPanel::OnKeyDown, this);
		Bind(wxEVT_IDLE, &MotionTrackPreviewPanel::OnIdle, this);
	}

	void Fit() {
		fit_mode = true;
		fast_render = false;
		Refresh(false);
	}
};

class MotionTrackGraphPanel final : public wxPanel {
	DialogMotionTrack *dialog = nullptr;

	void Plot(wxDC &dc, std::vector<motion_tracking::MotionTrackFrame> const& frames, wxRect rect, wxColour colour, double motion_tracking::MotionTrackFrame::*member) {
		if (frames.size() < 2)
			return;

		auto [min_it, max_it] = std::minmax_element(frames.begin(), frames.end(), [member](auto const& a, auto const& b) {
			return a.*member < b.*member;
		});
		double min_v = (*min_it).*member;
		double max_v = (*max_it).*member;
		if (std::abs(max_v - min_v) < 1e-9) {
			max_v += 1.0;
			min_v -= 1.0;
		}

		int start = frames.front().frame;
		int end = frames.back().frame;
		if (end <= start)
			end = start + 1;

		dc.SetPen(wxPen(colour, 1));
		wxPoint prev;
		bool have_prev = false;
		for (auto const& frame : frames) {
			double tx = (frame.frame - start) / static_cast<double>(end - start);
			double ty = (frame.*member - min_v) / (max_v - min_v);
			wxPoint cur(
				rect.x + static_cast<int>(std::lround(tx * rect.width)),
				rect.y + rect.height - static_cast<int>(std::lround(ty * rect.height)));
			if (have_prev)
				dc.DrawLine(prev, cur);
			prev = cur;
			have_prev = true;
		}
	}

	void OnPaint(wxPaintEvent &) {
		wxAutoBufferedPaintDC dc(this);
		dc.SetBackground(wxBrush(GetBackgroundColour()));
		dc.Clear();

		auto frames = motion_tracking::StabilizeMotionTrackFrames(dialog->GetResult(), dialog->GetExportSettings());

		wxSize size = GetClientSize();
		wxRect plot(44, 12, std::max(1, size.x - 58), std::max(1, size.y - 34));
		dc.SetPen(wxPen(wxColour(170, 170, 170), 1));
		dc.SetBrush(*wxTRANSPARENT_BRUSH);
		dc.DrawRectangle(plot);

		dc.SetTextForeground(wxColour(80, 80, 80));
		dc.DrawText(_("X"), 6, 8);
		dc.DrawText(_("Y"), 6, 24);
		dc.DrawText(_("Scale"), 6, 40);
		dc.DrawText(_("Rot"), 6, 56);
		dc.DrawText(_("Conf"), 6, 72);

		if (frames.size() < 2) {
			dc.SetTextForeground(wxColour(110, 110, 110));
			dc.DrawText(_("Place and track a marker to plot data"), plot.x + 8, plot.y + 8);
			return;
		}

		Plot(dc, frames, plot, wxColour(30, 120, 220), &motion_tracking::MotionTrackFrame::x);
		Plot(dc, frames, plot, wxColour(30, 160, 80), &motion_tracking::MotionTrackFrame::y);
		Plot(dc, frames, plot, wxColour(220, 150, 20), &motion_tracking::MotionTrackFrame::scale_x);
		Plot(dc, frames, plot, wxColour(180, 80, 180), &motion_tracking::MotionTrackFrame::rotation_deg);
		Plot(dc, frames, plot, wxColour(210, 60, 60), &motion_tracking::MotionTrackFrame::confidence);
	}

public:
	MotionTrackGraphPanel(wxWindow *parent, DialogMotionTrack *dialog)
	: wxPanel(parent, -1, wxDefaultPosition, wxSize(-1, 120), wxBORDER_SIMPLE)
	, dialog(dialog) {
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		SetMinSize(wxSize(320, 90));
		Bind(wxEVT_PAINT, &MotionTrackGraphPanel::OnPaint, this);
	}
};

DialogMotionTrack::DialogMotionTrack(agi::Context *c)
: wxDialog(c->parent, -1, _("Motion Track"), wxDefaultPosition, wxSize(780, 640), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxMAXIMIZE_BOX | wxMINIMIZE_BOX)
, context(c) {
	if (!context->project->VideoProvider())
		throw agi::UserCancelException("No video loaded");

	SetIcon(GETICON(button_motion_track_16));

	CalculateSelectedFrameRange();
	result.fps = context->project->Timecodes().FPS();
	result.source_width = context->project->VideoProvider()->GetWidth();
	result.source_height = context->project->VideoProvider()->GetHeight();
	other_channel.result = result;
	CaptureSources();
	frame_cache = agi::make_unique<MotionTrackFrameCache>([=](int frame) {
		return context->videoController->GetFrame(frame, true);
	});

	CreateControls();
	BindControls();
	StartFrameCache();
	Fit();
	SetMinSize(wxSize(620, 460));
	CenterOnParent();

	try {
		persist = agi::make_unique<PersistLocation>(this, "Tool/Motion Track");
	}
	catch (agi::InternalError const& e) {
		if (e.GetMessage().find("Tool/Motion Track/") == std::string::npos)
			throw;
	}
	connections = agi::signal::make_vector({
		context->videoController->AddSeekListener(&DialogMotionTrack::OnSeek, this),
		context->project->AddVideoProviderListener([=](AsyncVideoProvider *) { Close(); }),
		context->project->AddTimecodesListener([=](agi::vfr::Framerate const&) {
			invalid_reason = "Video frame timing changed. Start a new tracking session.";
			UpdateApplyStatus();
		}),
		context->selectionController->AddActiveLineListener([=](AssDialogue*) { CheckSession(); }),
		context->selectionController->AddSelectionListener([=] { CheckSession(); }),
		context->ass->AddCommitListener([=](int type, AssDialogue const*) {
			if (!applying && (type == AssFile::COMMIT_NEW || (type & AssFile::COMMIT_SCRIPTINFO)))
				invalid_reason = "Subtitle document or coordinate system changed. Start a new tracking session.";
			CheckSession();
		})
	});

	JumpToFrame(current_frame);
}

DialogMotionTrack::~DialogMotionTrack() {
	StopPlayback();
	StopFrameCache();
}

void DialogMotionTrack::CalculateSelectedFrameRange() {
	auto selection = context->selectionController->GetSortedSelection();
	if (selection.empty()) {
		if (auto active = context->selectionController->GetActiveLine())
			selection.push_back(active);
	}
	if (selection.empty())
		throw agi::UserCancelException("No subtitle lines selected");

	int start_ms = std::numeric_limits<int>::max();
	int end_ms = std::numeric_limits<int>::min();
	for (auto line : selection) {
		start_ms = std::min<int>(start_ms, line->Start);
		end_ms = std::max<int>(end_ms, line->End);
	}

	int last_frame = std::max(0, context->project->VideoProvider()->GetFrameCount() - 1);
	settings.start_frame = mid(0, context->videoController->FrameAtTime(start_ms, agi::vfr::START), last_frame);
	settings.end_frame = mid(0, context->videoController->FrameAtTime(end_ms, agi::vfr::END), last_frame);
	if (settings.end_frame < settings.start_frame)
		std::swap(settings.start_frame, settings.end_frame);

	current_frame = mid(settings.start_frame, context->videoController->GetFrameN(), settings.end_frame);
}

void DialogMotionTrack::CreateControls() {
	auto main_sizer = new wxBoxSizer(wxVERTICAL);

	auto top_row = new wxBoxSizer(wxHORIZONTAL);
	range_label = new wxStaticText(this, -1, "");
	current_label = new wxStaticText(this, -1, "");
	segment_label = new wxStaticText(this, -1, "");
	cache_status_label = new wxStaticText(this, -1, "");
	top_row->Add(range_label, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
	top_row->Add(current_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
	top_row->Add(segment_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
	top_row->Add(cache_status_label, 0, wxALIGN_CENTER_VERTICAL);
	main_sizer->Add(top_row, 0, wxEXPAND | wxALL, 6);

	auto workflow = new wxBoxSizer(wxHORIZONTAL);
	main_track_button = new wxButton(this, -1, _("Track Motion"));
	clip_track_button = new wxButton(this, -1, _("Track for \\clip"));
	target_label = new wxStaticText(this, -1, _("Tracking: subtitle"));
	reference_label = new wxStaticText(this, -1, "");
	workflow->Add(main_track_button, 0, wxRIGHT, 4);
	workflow->Add(clip_track_button, 0, wxRIGHT, 12);
	workflow->Add(target_label, 1, wxALIGN_CENTER_VERTICAL);
	workflow->Add(reference_label, 0, wxALIGN_CENTER_VERTICAL);
	main_sizer->Add(workflow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
	main_track_button->SetToolTip(_("Place a square on the subtitle's object, then track the selected range. Direction buttons also work on this track."));
	clip_track_button->SetToolTip(_("Switch to the separate clip tracker. Place a square on the mask's object, then press again to track the range."));
	main_track_button->Bind(wxEVT_BUTTON, [=](wxCommandEvent&) { TrackMotion(false); });
	clip_track_button->Bind(wxEVT_BUTTON, [=](wxCommandEvent&) { TrackMotion(true); });

	frame_bar = new MotionTrackFrameBar(this, this);
	main_sizer->Add(frame_bar, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

	auto controls = new wxBoxSizer(wxVERTICAL);
	auto track_buttons = new wxBoxSizer(wxHORIZONTAL);
	play_button = new wxButton(this, -1, _("Play"));
	track_to_start = new wxButton(this, -1, _("Track to Start"));
	track_previous = new wxButton(this, -1, _("Track Previous"));
	track_next = new wxButton(this, -1, _("Track Next"));
	track_to_end = new wxButton(this, -1, _("Track to End"));
	play_button->SetToolTip(_("Play the selected frame range inside this dialog"));
	track_to_start->SetToolTip(_("Track backward to the nearest earlier handoff mark, or the selected line start."));
	track_previous->SetToolTip(_("Track previous frame"));
	track_next->SetToolTip(_("Track next frame"));
	track_to_end->SetToolTip(_("Track forward to the nearest later handoff mark, or the selected line end."));
	track_buttons->Add(play_button, 0, wxRIGHT, 10);
	track_buttons->Add(track_to_start, 0, wxRIGHT, 4);
	track_buttons->Add(track_previous, 0, wxRIGHT, 4);
	track_buttons->Add(track_next, 0, wxRIGHT, 4);
	track_buttons->Add(track_to_end, 0);
	controls->Add(track_buttons, 0, wxBOTTOM, 4);

	auto mark_buttons = new wxBoxSizer(wxHORIZONTAL);
	mark_handoff_button = new wxButton(this, -1, _("Mark Handoff Frame"));
	clear_handoff_button = new wxButton(this, -1, _("Clear Mark"));
	mark_handoff_button->SetToolTip(_("Mark the current frame as a handoff point where the next tracker run can start."));
	clear_handoff_button->SetToolTip(_("Remove the handoff mark from the current frame."));
	mark_buttons->Add(mark_handoff_button, 0, wxRIGHT, 4);
	mark_buttons->Add(clear_handoff_button, 0);
	controls->Add(mark_buttons, 0, wxBOTTOM, 4);

	auto settings_row = new wxBoxSizer(wxHORIZONTAL);
	settings_row->Add(new wxStaticText(this, -1, _("Square:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
	square_ctrl = new wxSpinCtrl(this, -1, "", wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 4, 2000, settings.square_size);
	settings_row->Add(square_ctrl, 0, wxRIGHT, 8);
	settings_row->Add(new wxStaticText(this, -1, _("Search:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
	search_ctrl = new wxSpinCtrl(this, -1, "", wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 4, 4000, settings.search_size);
	settings_row->Add(search_ctrl, 0, wxRIGHT, 8);
	settings_row->Add(new wxStaticText(this, -1, _("Threshold:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
	threshold_ctrl = new wxSpinCtrlDouble(this, -1, "", wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 0.0, 1.0, settings.correlation_threshold, 0.05);
	threshold_ctrl->SetDigits(2);
	settings_row->Add(threshold_ctrl, 0, wxRIGHT, 8);
	normalize_check = new wxCheckBox(this, -1, _("Normalize"));
	normalize_check->SetValue(settings.brightness_normalize);
	normalize_check->SetToolTip(_("Normalize pattern and search patches before correlation matching"));
	settings_row->Add(normalize_check, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
	controls->Add(settings_row, 0, wxBOTTOM, 4);

	auto output_row = new wxBoxSizer(wxHORIZONTAL);
	output_row->Add(new wxStaticText(this, -1, _("Output:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
	mode_choice = new wxChoice(this, -1);
	mode_choice->Append(_("Movement only"));
	mode_choice->Append(_("Movement + rotation"));
	mode_choice->Append(_("Movement + size"));
	mode_choice->Append(_("Movement + size + rotation"));
	mode_choice->SetSelection(3);
	mode_choice->SetToolTip(_("Choose which motion components the tracker should calculate and export."));
	output_row->Add(mode_choice, 0);
	controls->Add(output_row, 0, wxBOTTOM, 4);

	auto cleanup_row = new wxBoxSizer(wxHORIZONTAL);
	cleanup_row->Add(new wxStaticText(this, -1, _("Noise Cleanup:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
	cleanup_choice = new wxChoice(this, -1);
	cleanup_choice->Append(_("None"));
	cleanup_choice->Append(_("Remove tiny jitter"));
	cleanup_choice->Append(_("Remove spikes"));
	cleanup_choice->Append(_("Linear cleanup"));
	cleanup_choice->SetSelection(0);
	cleanup_row->Add(cleanup_choice, 0, wxRIGHT, 8);
	cleanup_row->Add(new wxStaticText(this, -1, _("Noise threshold:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
	cleanup_threshold_ctrl = new wxSpinCtrlDouble(this, -1, "", wxDefaultPosition, wxSize(74, -1), wxSP_ARROW_KEYS, 0.0, 25.0, settings.cleanup_threshold, 0.25);
	cleanup_threshold_ctrl->SetDigits(2);
	cleanup_threshold_ctrl->SetToolTip(_("Pixel threshold used by jitter and spike cleanup."));
	cleanup_row->Add(cleanup_threshold_ctrl, 0);
	controls->Add(cleanup_row, 0, wxBOTTOM, 2);

	auto trail_row = new wxBoxSizer(wxHORIZONTAL);
	trail_check = new wxCheckBox(this, -1, _("Show track trail"));
	trail_check->SetValue(show_track_trail);
	trail_check->SetToolTip(_("Show nearby tracked marker positions in the preview."));
	trail_row->Add(trail_check, 0, wxALIGN_CENTER_VERTICAL);
	controls->Add(trail_row, 0, wxBOTTOM, 2);
	main_sizer->Add(controls, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

	preview = new MotionTrackPreviewPanel(this, this);
	main_sizer->Add(preview, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

	graph = new MotionTrackGraphPanel(this, this);
	main_sizer->Add(graph, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

	main_sizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, 6);
	apply_status = new wxStaticText(this, -1, "");
	main_sizer->Add(apply_status, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 6);
	auto apply_row = new wxBoxSizer(wxHORIZONTAL);
	apply_button = new wxButton(this, -1, _("Apply"));
	apply_button->SetDefault();
	advanced_button = new wxButton(this, -1, _("Advanced Apply..."));
	revert_button = new wxButton(this, -1, _("Revert"));
	auto minimize = new wxButton(this, -1, _("Minimize"));
	apply_row->Add(apply_button, 0, wxRIGHT, 4);
	apply_row->Add(advanced_button, 0, wxRIGHT, 4);
	apply_row->Add(revert_button, 0, wxRIGHT, 4);
	apply_row->AddStretchSpacer();
	apply_row->Add(minimize, 0);
	main_sizer->Add(apply_row, 0, wxEXPAND | wxALL, 6);
	apply_button->Bind(wxEVT_BUTTON, [=](wxCommandEvent&) { ApplyMotion(false); });
	advanced_button->Bind(wxEVT_BUTTON, [=](wxCommandEvent&) { ApplyMotion(true); });
	revert_button->Bind(wxEVT_BUTTON, [=](wxCommandEvent&) { RevertMotion(); });
	minimize->SetToolTip(_("Hide this window while positioning/styling in the main video. Reopen Motion Track to restore the same session."));
	minimize->Bind(wxEVT_BUTTON, [=](wxCommandEvent&) { StopPlayback(); Hide(); });
	Bind(wxEVT_ICONIZE, [=](wxIconizeEvent& event) {
		if (event.IsIconized()) { StopPlayback(); Iconize(false); Hide(); }
		else event.Skip();
	});
	auto bottom = new wxBoxSizer(wxHORIZONTAL);
	auto copy = new wxButton(this, -1, _("Copy Motion Data"));
	auto save = new wxButton(this, -1, _("Save Data"));
	auto clear = new wxButton(this, -1, _("Clear"));
	auto new_session = new wxButton(this, -1, _("New Session"));
	auto close = new wxButton(this, wxID_CANCEL, _("Close"));
	bottom->Add(copy, 0, wxRIGHT, 4);
	bottom->Add(save, 0, wxRIGHT, 4);
	bottom->Add(clear, 0, wxRIGHT, 4);
	bottom->Add(new_session, 0, wxRIGHT, 4);
	bottom->AddStretchSpacer(1);
	bottom->Add(close, 0);
	main_sizer->Add(bottom, 0, wxEXPAND | wxALL, 6);

	copy->Bind(wxEVT_BUTTON, [=](wxCommandEvent &) { CopyData(); });
	save->Bind(wxEVT_BUTTON, [=](wxCommandEvent &) { SaveData(); });
	clear->Bind(wxEVT_BUTTON, [=](wxCommandEvent &) { ClearData(); });
	new_session->Bind(wxEVT_BUTTON, [=](wxCommandEvent &) { NewSession(); });

	SetSizer(main_sizer);
	UpdateLabels();
	UpdatePlaybackButton();
}

void DialogMotionTrack::BindControls() {
	auto update_settings = [=] { UpdateSettingsFromControls(); };
	square_ctrl->Bind(wxEVT_SPINCTRL, [=](wxSpinEvent &) { update_settings(); });
	search_ctrl->Bind(wxEVT_SPINCTRL, [=](wxSpinEvent &) { update_settings(); });
	threshold_ctrl->Bind(wxEVT_SPINCTRLDOUBLE, [=](wxSpinDoubleEvent &) { update_settings(); });
	normalize_check->Bind(wxEVT_CHECKBOX, [=](wxCommandEvent &) { update_settings(); });
	mode_choice->Bind(wxEVT_CHOICE, [=](wxCommandEvent &) { update_settings(); });
	cleanup_choice->Bind(wxEVT_CHOICE, [=](wxCommandEvent &) { update_settings(); });
	cleanup_threshold_ctrl->Bind(wxEVT_SPINCTRLDOUBLE, [=](wxSpinDoubleEvent &) { update_settings(); });
	trail_check->Bind(wxEVT_CHECKBOX, [=](wxCommandEvent &) { UpdateTrailControls(); });

	play_button->Bind(wxEVT_BUTTON, [=](wxCommandEvent &) { TogglePlayback(); });
	track_to_start->Bind(wxEVT_BUTTON, [=](wxCommandEvent &) { TrackRange(settings.start_frame); });
	track_previous->Bind(wxEVT_BUTTON, [=](wxCommandEvent &) { TrackOne(current_frame - 1); });
	track_next->Bind(wxEVT_BUTTON, [=](wxCommandEvent &) { TrackOne(current_frame + 1); });
	track_to_end->Bind(wxEVT_BUTTON, [=](wxCommandEvent &) { TrackRange(settings.end_frame); });
	mark_handoff_button->Bind(wxEVT_BUTTON, [=](wxCommandEvent &) { MarkHandoffFrame(); });
	clear_handoff_button->Bind(wxEVT_BUTTON, [=](wxCommandEvent &) { ClearHandoffMark(); });

	Bind(wxEVT_CHAR_HOOK, &DialogMotionTrack::OnCharHook, this);
	cache_timer.Bind(wxEVT_TIMER, &DialogMotionTrack::OnCacheTimer, this);
	playback_timer.Bind(wxEVT_TIMER, &DialogMotionTrack::OnPlaybackTimer, this);
}

void DialogMotionTrack::StartFrameCache() {
	if (!frame_cache)
		return;

	frame_cache->Start(
		settings.start_frame,
		settings.end_frame,
		result.source_width,
		result.source_height,
		current_frame);
	UpdateCacheStatus();
	cache_timer.Start(100);
}

void DialogMotionTrack::StopFrameCache() {
	cache_timer.Stop();
	if (frame_cache) {
		frame_cache->Stop();
		frame_cache->Clear();
	}
}

void DialogMotionTrack::UpdateSettingsFromControls() {
	settings.square_size = square_ctrl->GetValue();
	settings.search_size = std::max(search_ctrl->GetValue(), settings.square_size);
	if (search_ctrl->GetValue() != settings.search_size)
		search_ctrl->SetValue(settings.search_size);
	settings.correlation_threshold = std::clamp(threshold_ctrl->GetValue(), 0.0, 1.0);
	settings.brightness_normalize = normalize_check->IsChecked();
	settings.prepass = true;
	settings.base = motion_tracking::MotionTrackBase::PreviousFrame;
	switch (mode_choice->GetSelection()) {
		case 0: settings.mode = motion_tracking::MotionTrackMode::PositionOnly; break;
		case 1: settings.mode = motion_tracking::MotionTrackMode::PositionRotation; break;
		case 2: settings.mode = motion_tracking::MotionTrackMode::PositionSize; break;
		default: settings.mode = motion_tracking::MotionTrackMode::PositionSizeRotation; break;
	}
	switch (cleanup_choice->GetSelection()) {
		case 1: settings.cleanup = motion_tracking::MotionTrackCleanup::RemoveTinyJitter; break;
		case 2: settings.cleanup = motion_tracking::MotionTrackCleanup::RemoveSpikes; break;
		case 3: settings.cleanup = motion_tracking::MotionTrackCleanup::LinearPerRun; break;
		default: settings.cleanup = motion_tracking::MotionTrackCleanup::Off; break;
	}
	settings.cleanup_threshold = std::max(0.0, cleanup_threshold_ctrl->GetValue());
	if (graph)
		graph->Refresh(false);
}

void DialogMotionTrack::UpdateTrailControls() {
	show_track_trail = trail_check && trail_check->IsChecked();
	track_trail_past = 10;
	track_trail_future = 10;
	RefreshPreview();
}

void DialogMotionTrack::UpdateLabels() {
	range_label->SetLabel(fmt_wx("Selected line frames: %d - %d", settings.start_frame, settings.end_frame));
	current_label->SetLabel(fmt_wx("Current: %d", current_frame));
	if (segment_label) {
		if (handoff_marks.empty()) {
			segment_label->SetLabel(_("Marks: none"));
		}
		else {
			std::string text = "Marks:";
			int shown = 0;
			for (int mark : handoff_marks) {
				if (shown >= 6) {
					text += " ...";
					break;
				}
				text += agi::format(" %d", mark);
				++shown;
			}
			segment_label->SetLabel(to_wx(text));
		}
	}
	UpdateCacheStatus();
	UpdateApplyStatus();
}

void DialogMotionTrack::UpdateCacheStatus() {
	if (!cache_status_label || !frame_cache)
		return;

	int cached = frame_cache->CachedCount();
	int total = frame_cache->TargetCount();
	if (!frame_cache->BackgroundEnabled()) {
		cache_status_label->SetLabel(fmt_wx("Frame cache: lazy (%d frames)", total));
		return;
	}

	cache_status_label->SetLabel(fmt_wx("%s: %d / %d",
		frame_cache->Complete() ? "Cached frames" : "Caching frames",
		cached,
		total));
}

void DialogMotionTrack::UpdatePanels() {
	UpdateLabels();
	if (frame_bar)
		frame_bar->Refresh(false);
	if (preview)
		preview->Refresh(false);
	if (graph)
		graph->Refresh(false);
}

void DialogMotionTrack::RefreshPreview() {
	if (preview)
		preview->Refresh(false);
}

std::shared_ptr<VideoFrame> DialogMotionTrack::GetCachedFrame(int frame) const {
	if (frame_cache)
		return frame_cache->GetFrameBlockingOrLoad(frame);
	return context->videoController->GetFrame(frame, true);
}

void DialogMotionTrack::LoadCurrentFrame() {
	if (preview_frame == current_frame) {
		UpdatePanels();
		return;
	}

	try {
		auto frame = GetCachedFrame(current_frame);
		if (frame) {
			preview_image = GetImage(*frame);
			preview_frame = current_frame;
		}
		else {
			preview_image = wxImage();
			preview_frame = -1;
		}
	}
	catch (...) {
		preview_image = wxImage();
		preview_frame = -1;
	}
	UpdatePanels();
}

void DialogMotionTrack::OnSeek(int frame) {
	current_frame = mid(settings.start_frame, frame, settings.end_frame);
	if (IsShown()) LoadCurrentFrame();
	else UpdateApplyStatus();
}

void DialogMotionTrack::OnCacheTimer(wxTimerEvent &) {
	UpdateCacheStatus();
	if (!frame_cache || !frame_cache->BackgroundEnabled() || frame_cache->Complete())
		cache_timer.Stop();
}

void DialogMotionTrack::OnPlaybackTimer(wxTimerEvent &) {
	if (!playing)
		return;

	using namespace std::chrono;
	auto elapsed_ms = duration_cast<milliseconds>(steady_clock::now() - playback_start_time).count();
	int target_ms = playback_start_ms + static_cast<int>(elapsed_ms);
	int frame = context->videoController->FrameAtTime(target_ms);
	frame = mid(settings.start_frame, frame, settings.end_frame);

	if (frame >= settings.end_frame) {
		ShowFrame(settings.end_frame);
		StopPlayback();
		return;
	}

	if (frame != current_frame)
		ShowFrame(frame);
}

bool DialogMotionTrack::FocusIsTextInput() const {
	wxWindow *focus = wxWindow::FindFocus();
	while (focus && focus != this) {
		if (focus == square_ctrl || focus == search_ctrl || focus == threshold_ctrl ||
			focus == cleanup_threshold_ctrl ||
			dynamic_cast<wxTextCtrl *>(focus))
			return true;
		focus = focus->GetParent();
	}
	return false;
}

void DialogMotionTrack::OnCharHook(wxKeyEvent &evt) {
	if (!FocusIsTextInput() && HandleNavigationKey(evt.GetKeyCode()))
		return;
	evt.Skip();
}

bool DialogMotionTrack::HandleNavigationKey(int key_code) {
	if (key_code == WXK_LEFT) {
		StepFrame(-1);
		return true;
	}
	if (key_code == WXK_RIGHT) {
		StepFrame(1);
		return true;
	}
	if (key_code == WXK_SPACE) {
		TogglePlayback();
		return true;
	}
	return false;
}

void DialogMotionTrack::ShowFrame(int frame) {
	current_frame = mid(settings.start_frame, frame, settings.end_frame);
	LoadCurrentFrame();
}

void DialogMotionTrack::StepFrame(int delta) {
	StopPlayback();
	ShowFrame(current_frame + delta);
}

void DialogMotionTrack::TogglePlayback() {
	if (playing) {
		StopPlayback();
		return;
	}
	StartPlayback();
}

void DialogMotionTrack::StartPlayback() {
	if (current_frame >= settings.end_frame)
		return;

	playing = true;
	playback_start_frame = current_frame;
	playback_start_ms = context->videoController->TimeAtFrame(playback_start_frame);
	playback_start_time = std::chrono::steady_clock::now();
	playback_timer.Start(10);
	UpdatePlaybackButton();
}

void DialogMotionTrack::StopPlayback() {
	if (playback_timer.IsRunning())
		playback_timer.Stop();
	if (!playing)
		return;

	playing = false;
	UpdatePlaybackButton();
}

void DialogMotionTrack::UpdatePlaybackButton() {
	if (play_button)
		play_button->SetLabel(playing ? _("Stop") : _("Play"));
}

bool DialogMotionTrack::HasCurrentMarker() const {
	return markers.find(current_frame) != markers.end();
}

int DialogMotionTrack::GetCurrentSegmentVisualState() const {
	int segment_index = FindSegmentForFrame(current_frame);
	if (segment_index < 0)
		return 0;
	return segment_index == active_segment ? 1 : 2;
}

motion_tracking::MotionTrackMarker DialogMotionTrack::GetCurrentMarker() const {
	return MarkerForFrame(current_frame);
}

motion_tracking::MotionTrackMarker DialogMotionTrack::MarkerForFrame(int frame) const {
	auto it = markers.find(frame);
	if (it != markers.end())
		return it->second;
	return {};
}

std::vector<MotionTrackTrailMarker> DialogMotionTrack::GetTrackTrailMarkers() const {
	std::vector<MotionTrackTrailMarker> trail;
	if (!show_track_trail)
		return trail;

	auto add_frame = [&](int frame) {
		if (frame < settings.start_frame || frame > settings.end_frame || frame == current_frame)
			return;

		auto result_it = std::find_if(result.frames.begin(), result.frames.end(), [=](auto const& tracked) {
			return tracked.frame == frame;
		});
		if (result_it == result.frames.end() || result_it->state == motion_tracking::MotionTrackState::Lost)
			return;

		motion_tracking::MotionTrackMarker marker;
		auto marker_it = markers.find(frame);
		if (marker_it != markers.end()) {
			marker = marker_it->second;
		}
		else {
			marker.cx = result_it->x;
			marker.cy = result_it->y;
			double frame_scale = std::max(result_it->scale_x, result_it->scale_y);
			marker.size = std::max(4.0, initial_marker_size * frame_scale);
			marker.search_size = std::max<double>(settings.search_size, marker.size);
			marker.rotation_deg = result_it->rotation_deg;
		}

		trail.push_back({frame, marker, result_it->state});
	};

	for (int frame = current_frame - track_trail_past; frame < current_frame; ++frame)
		add_frame(frame);
	for (int frame = current_frame + 1; frame <= current_frame + track_trail_future; ++frame)
		add_frame(frame);
	return trail;
}

std::vector<int> DialogMotionTrack::GetHandoffMarks() const {
	return std::vector<int>(handoff_marks.begin(), handoff_marks.end());
}

void DialogMotionTrack::SetCurrentMarker(motion_tracking::MotionTrackMarker marker) {
	apply_summary.clear();
	marker.search_size = std::max(marker.search_size, marker.size);
	markers[current_frame] = marker;
	if (active_segment >= 0 && active_segment < static_cast<int>(segments.size())) {
		auto& segment = segments[active_segment];
		if (segment.enabled && current_frame >= segment.start_frame && current_frame <= segment.end_frame) {
			if (current_frame == segment.start_frame)
				segment.tracker_box_at_start = marker;
			StoreSegmentFrame(active_segment, current_frame, marker, 1.0, motion_tracking::MotionTrackState::Untracked);
			motion_tracking::RecalculateSegmentAccumulatedOffsets(segments);
			result = motion_tracking::BuildStitchedMotionResult(result, segments);
		}
	}
	if (base_frame < 0) {
		base_frame = current_frame;
		initial_marker_size = marker.size;
	}
	UpdatePanels();
}

void DialogMotionTrack::PlaceCurrentMarker(double x, double y) {
	UpdateSettingsFromControls();
	motion_tracking::MotionTrackMarker marker;
	marker.cx = x;
	marker.cy = y;
	marker.size = settings.square_size;
	marker.search_size = std::max(settings.search_size, settings.square_size);
	marker.rotation_deg = 0.0;
	SetCurrentMarker(marker);
}

void DialogMotionTrack::DeleteCurrentMarker() {
	markers.erase(current_frame);
	int segment_index = FindSegmentForFrame(current_frame);
	if (segment_index >= 0 && segment_index < static_cast<int>(segments.size())) {
		auto& segment = segments[segment_index];
		segment.tracked_center_by_frame.erase(
			std::remove_if(segment.tracked_center_by_frame.begin(), segment.tracked_center_by_frame.end(), [=](auto const& sample) {
				return sample.frame == current_frame;
			}),
			segment.tracked_center_by_frame.end());
	}
	result.frames.erase(std::remove_if(result.frames.begin(), result.frames.end(), [=](auto const& frame) {
		return frame.frame == current_frame;
	}), result.frames.end());
	RecalculateMotion();
	if (base_frame == current_frame) {
		base_frame = markers.empty() ? -1 : markers.begin()->first;
		if (base_frame >= 0)
			initial_marker_size = markers.begin()->second.size;
	}
	UpdatePanels();
}

void DialogMotionTrack::JumpToFrame(int frame) {
	StopPlayback();
	ShowFrame(frame);
}

motion_tracking::MotionTrackFrame DialogMotionTrack::MakeFrame(
	int frame,
	motion_tracking::MotionTrackMarker const& marker,
	double confidence,
	motion_tracking::MotionTrackState state) const {

	double scale = initial_marker_size > 0.0 ? marker.size / initial_marker_size : 1.0;
	return motion_tracking::MotionTrackFrame{
		frame,
		marker.cx,
		marker.cy,
		marker.cx,
		marker.cy,
		scale,
		scale,
		marker.rotation_deg,
		confidence,
		state
	};
}

void DialogMotionTrack::StoreFrame(int frame, motion_tracking::MotionTrackMarker const& marker, double confidence, motion_tracking::MotionTrackState state) {
	auto output = MakeFrame(frame, marker, confidence, state);
	auto it = std::find_if(result.frames.begin(), result.frames.end(), [=](auto const& existing) {
		return existing.frame == frame;
	});
	if (it == result.frames.end())
		result.frames.push_back(output);
	else
		*it = output;
}

motion_tracking::MotionTrackImage DialogMotionTrack::GetTrackImage(int frame_number) const {
	auto frame = GetCachedFrame(frame_number);
	motion_tracking::MotionTrackImage image;
	if (!frame)
		return image;

	image.width = static_cast<int>(frame->width);
	image.height = static_cast<int>(frame->height);
	image.pitch = static_cast<int>(frame->pitch);
	image.flipped = frame->flipped;
	image.bgra = frame->data;
	return image;
}

int DialogMotionTrack::FindSegmentForFrame(int frame) const {
	if (active_segment >= 0 && active_segment < static_cast<int>(segments.size())) {
		auto const& segment = segments[active_segment];
		if (segment.enabled && frame >= segment.start_frame && frame <= segment.end_frame)
			return active_segment;
	}

	int found = -1;
	for (int i = 0; i < static_cast<int>(segments.size()); ++i) {
		auto const& segment = segments[i];
		if (!segment.enabled || frame < segment.start_frame || frame > segment.end_frame)
			continue;
		if (found < 0 ||
			segment.start_frame > segments[found].start_frame ||
			(segment.start_frame == segments[found].start_frame && i > found))
			found = i;
	}
	return found;
}

int DialogMotionTrack::ResolveHandoffTarget(int requested_target) const {
	if (requested_target > current_frame) {
		auto mark = handoff_marks.upper_bound(current_frame);
		if (mark != handoff_marks.end() && *mark < requested_target)
			return *mark;
		return requested_target;
	}

	if (requested_target < current_frame) {
		auto mark = handoff_marks.lower_bound(current_frame);
		if (mark != handoff_marks.begin()) {
			--mark;
			if (*mark > requested_target)
				return *mark;
		}
		return requested_target;
	}

	return requested_target;
}

void DialogMotionTrack::StoreSegmentFrame(
	int segment_index,
	int frame,
	motion_tracking::MotionTrackMarker const& marker,
	double confidence,
	motion_tracking::MotionTrackState state) {

	if (segment_index < 0 || segment_index >= static_cast<int>(segments.size()))
		return;

	auto& segment = segments[segment_index];
	motion_tracking::UpsertSegmentSample(segment, motion_tracking::MotionTrackSegmentSample{frame, marker, confidence, state});
}

void DialogMotionTrack::RebuildMarkersFromSegments() {
	markers.clear();
	for (auto const& segment : segments) {
		if (!segment.enabled)
			continue;
		for (auto const& sample : segment.tracked_center_by_frame) {
			if (sample.state == motion_tracking::MotionTrackState::Lost)
				continue;
			if (sample.frame < segment.start_frame || sample.frame > segment.end_frame)
				continue;
			markers[sample.frame] = sample.marker;
		}
	}
}

void DialogMotionTrack::RecalculateMotion() {
	apply_summary.clear();
	motion_tracking::RecalculateSegmentAccumulatedOffsets(segments);
	result = motion_tracking::BuildStitchedMotionResult(result, segments);
	RebuildMarkersFromSegments();
	UpdatePanels();
}

int DialogMotionTrack::StartTrackRunHere(int target_frame) {
	if (!HasCurrentMarker()) {
		wxMessageBox(_("Place a tracker marker before tracking."), _("Motion Track"), wxOK | wxICON_INFORMATION, this);
		return -1;
	}

	auto marker = GetCurrentMarker();
	motion_tracking::MotionTrackSegment segment;
	segment.anchor_frame = current_frame;
	segment.target_frame = target_frame;
	segment.direction = target_frame < current_frame ? -1 : 1;
	segment.start_frame = std::min(current_frame, target_frame);
	segment.end_frame = std::max(current_frame, target_frame);
	segment.tracker_box_at_start = marker;
	segment.name = agi::format("Track run %d", static_cast<int>(segments.size()) + 1);
	segment.end_frame_manual = true;
	segment.tracked_center_by_frame.push_back(motion_tracking::MotionTrackSegmentSample{
		current_frame,
		marker,
		1.0,
		motion_tracking::MotionTrackState::Untracked
	});

	int existing = -1;
	for (int i = 0; i < static_cast<int>(segments.size()); ++i) {
		if (segments[i].enabled && segments[i].anchor_frame == current_frame && segments[i].direction == segment.direction) {
			existing = i;
			break;
		}
	}
	if (existing >= 0) {
		segments[existing] = segment;
		active_segment = existing;
	}
	else {
		segments.push_back(segment);
		active_segment = static_cast<int>(segments.size()) - 1;
	}

	base_frame = current_frame;
	if (segments.size() == 1)
		initial_marker_size = marker.size;
	RecalculateMotion();
	return active_segment;
}

void DialogMotionTrack::MarkHandoffFrame() {
	if (current_frame > settings.start_frame && current_frame < settings.end_frame)
		handoff_marks.insert(current_frame);
	motion_tracking::RecalculateSegmentAccumulatedOffsets(segments);
	result = motion_tracking::BuildStitchedMotionResult(result, segments);
	UpdatePanels();
}

void DialogMotionTrack::ClearHandoffMark() {
	handoff_marks.erase(current_frame);
	motion_tracking::RecalculateSegmentAccumulatedOffsets(segments);
	result = motion_tracking::BuildStitchedMotionResult(result, segments);
	UpdatePanels();
}

int DialogMotionTrack::FindSegmentForTracking(int target_frame) {
	if (active_segment >= 0 && active_segment < static_cast<int>(segments.size())) {
		auto const& segment = segments[active_segment];
		if (segment.enabled && current_frame >= segment.start_frame && current_frame <= segment.end_frame) {
			bool target_in_run = target_frame >= segment.start_frame && target_frame <= segment.end_frame;
			bool same_direction = (target_frame - current_frame) * segment.direction >= 0;
			if (target_in_run && same_direction)
				return active_segment;
		}
	}

	if (!HasCurrentMarker()) {
		wxMessageBox(_("Place a tracker marker before tracking."), _("Motion Track"), wxOK | wxICON_INFORMATION, this);
		return -1;
	}

	return StartTrackRunHere(target_frame);
}

void DialogMotionTrack::TrackOne(int target_frame) {
	StopPlayback();
	UpdateSettingsFromControls();
	if (!motion_tracking::MotionTrackEngine::IsAvailable()) {
		wxMessageBox(_("Motion Track requires OpenCV, but this build was configured without OpenCV."), _("Motion Track"), wxOK | wxICON_INFORMATION, this);
		return;
	}

	if (target_frame < settings.start_frame || target_frame > settings.end_frame)
		return;
	int segment_index = FindSegmentForTracking(target_frame);
	if (segment_index < 0)
		return;
	auto& segment = segments[segment_index];
	if (target_frame < segment.start_frame || target_frame > segment.end_frame) {
		wxMessageBox(_("The current tracker run has reached its handoff frame. Place a new tracker square to continue."), _("Motion Track"), wxOK | wxICON_INFORMATION, this);
		return;
	}

	int source_frame = current_frame;
	auto search_marker = GetCurrentMarker();
	auto source_marker = search_marker;
	StoreSegmentFrame(segment_index, source_frame, source_marker, 1.0, motion_tracking::MotionTrackState::Untracked);

	try {
		motion_tracking::MotionTrackEngine engine;
		auto step = engine.TrackFrame(GetTrackImage(source_frame), GetTrackImage(target_frame), source_marker, search_marker, target_frame, settings);

		if (!ModeHasSize(settings.mode))
			step.marker.size = search_marker.size;
		if (!ModeHasRotation(settings.mode))
			step.marker.rotation_deg = search_marker.rotation_deg;
		step.marker.search_size = std::max(step.marker.search_size, step.marker.size);

		if (step.frame.state == motion_tracking::MotionTrackState::Lost) {
			auto& failed_segment = segments[segment_index];
			failed_segment.tracked_center_by_frame.erase(
				std::remove_if(failed_segment.tracked_center_by_frame.begin(), failed_segment.tracked_center_by_frame.end(), [=](auto const& sample) {
					return target_frame > current_frame ? sample.frame >= target_frame : sample.frame <= target_frame;
				}),
				failed_segment.tracked_center_by_frame.end());
			wxMessageBox(_("Tracking was lost before the segment end. Motion data was preserved up to the last valid frame."), _("Motion Track"), wxOK | wxICON_WARNING, this);
			RecalculateMotion();
			return;
		}

		markers[target_frame] = step.marker;
		StoreSegmentFrame(segment_index, target_frame, step.marker, step.frame.confidence, step.frame.state);
		bool reached_target = target_frame == segment.target_frame;
		RecalculateMotion();
		if (reached_target)
			active_segment = -1;
		JumpToFrame(target_frame);
	}
	catch (...) {
		wxMessageBox(_("Tracking failed while reading video frames."), _("Motion Track"), wxOK | wxICON_ERROR, this);
	}
}

void DialogMotionTrack::TrackRange(int target_frame) {
	target_frame = ResolveHandoffTarget(target_frame);
	if (target_frame == current_frame)
		return;
	if (!HasCurrentMarker()) {
		wxMessageBox(_("Place a tracker marker before tracking."), _("Motion Track"), wxOK | wxICON_INFORMATION, this);
		return;
	}

	int segment_index = FindSegmentForTracking(target_frame);
	if (segment_index < 0)
		return;

	tracking = true;
	UpdateApplyStatus();
	wxBusyCursor busy;
	int step = target_frame > current_frame ? 1 : -1;
	while (current_frame != target_frame) {
		int next = current_frame + step;
		TrackOne(next);
		if (!markers.count(next))
			break;
		// Paint the preview without dispatching arbitrary input which could
		// close the modeless dialog or swap channels midway through a run.
		preview->Update();
		frame_bar->Update();
	}
	tracking = false;
	UpdateApplyStatus();
}

motion_tracking::MotionTrackExportSettings DialogMotionTrack::GetExportSettings() const {
	motion_tracking::MotionTrackExportSettings export_settings;
	export_settings.mode = settings.mode;
	export_settings.cleanup = settings.cleanup;
	export_settings.cleanup_threshold = settings.cleanup_threshold;
	for (auto const& segment : segments) {
		if (!segment.enabled)
			continue;
		int anchor = segment.anchor_frame >= 0 ? segment.anchor_frame : segment.start_frame;
		int target = segment.target_frame >= 0 ? segment.target_frame : segment.end_frame;
		export_settings.locked_ranges.emplace_back(anchor, target);
	}
	return export_settings;
}

void DialogMotionTrack::CopyData() {
	RecalculateMotion();
	if (result.frames.empty()) {
		wxMessageBox(_("No motion data to copy."), _("Motion Track"), wxOK | wxICON_INFORMATION, this);
		return;
	}
	SetClipboard(motion_tracking::ExportAfterEffectsKeyframes(result, settings.start_frame, GetExportSettings()));
}

void DialogMotionTrack::SaveData() {
	RecalculateMotion();
	if (result.frames.empty()) {
		wxMessageBox(_("No motion data to save."), _("Motion Track"), wxOK | wxICON_INFORMATION, this);
		return;
	}

	wxString filename = wxFileSelector(
		_("Save motion track data"),
		"",
		"motion_track.txt",
		"txt",
		_("Text files (*.txt)|*.txt|All files (*.*)|*.*"),
		wxFD_SAVE | wxFD_OVERWRITE_PROMPT,
		this);
	if (filename.empty())
		return;

	std::ofstream file(from_wx(filename), std::ios::binary);
	if (!file) {
		wxMessageBox(_("Could not open the selected file for writing."), _("Motion Track"), wxOK | wxICON_ERROR, this);
		return;
	}
	file << motion_tracking::ExportAfterEffectsKeyframes(result, settings.start_frame, GetExportSettings());
}

void DialogMotionTrack::ClearData() {
	apply_summary.clear();
	markers.clear();
	segments.clear();
	handoff_marks.clear();
	result.frames.clear();
	base_frame = -1;
	active_segment = -1;
	initial_marker_size = settings.square_size;
	UpdatePanels();
}

void DialogMotionTrack::CaptureSources() {
	source_identity.clear();
	for (auto line : context->selectionController->GetSortedSelection())
		source_identity.push_back(motion_tracking::IdentifyMotionSource(*line));
	auto active = context->selectionController->GetActiveLine();
	source_active_id = active ? active->Id : 0;
	invalid_reason.clear();
	apply_summary.clear();
}

void DialogMotionTrack::CheckSession() {
	if (applying) return;
	std::vector<AssDialogue const*> selected;
	for (auto line : context->selectionController->GetSortedSelection()) selected.push_back(line);
	auto active = context->selectionController->GetActiveLine();
	auto reason = motion_tracking::ValidateMotionSources(source_identity,selected,source_active_id,
		active ? active->Id : 0,!ClipTrack().frames.empty());
	if (!reason.empty()) invalid_reason = reason;
	UpdateApplyStatus();
}

motion_tracking::MotionTrackResult const& DialogMotionTrack::MainTrack() const {
	return editing_clip ? other_channel.result : result;
}

motion_tracking::MotionTrackResult const& DialogMotionTrack::ClipTrack() const {
	return editing_clip ? result : other_channel.result;
}

void DialogMotionTrack::UpdateApplyStatus() {
	if (!apply_status) return;
	int reference = context->videoController->GetFrameN();
	reference_label->SetLabel(fmt_wx("Main video reference: %d",reference));
	target_label->SetLabel(editing_clip ? _("Tracking: \\clip") : _("Tracking: subtitle"));
	std::string reason = invalid_reason;
	if (reason.empty() && MainTrack().frames.empty()) reason = "Track the subtitle object first.";
	auto contains = [&](auto const& data, int frame) {
		auto it = std::lower_bound(data.frames.begin(),data.frames.end(),frame,[](auto const& sample,int f) { return sample.frame < f; });
		return it != data.frames.end() && it->frame == frame && it->state != motion_tracking::MotionTrackState::Lost;
	};
	if (reason.empty() && !contains(MainTrack(),reference)) reason = "Seek the main video to a tracked reference frame.";
	if (reason.empty() && !ClipTrack().frames.empty() && !contains(ClipTrack(),reference))
		reason = "The clip track does not cover the main video reference frame.";
	if (reason.empty()) {
		for (auto const& source : source_identity) {
			int first = context->videoController->FrameAtTime(source.start,agi::vfr::START);
			int last = context->videoController->FrameAtTime(source.end,agi::vfr::END);
			for (int f = first; f <= last; ++f) {
				if (!contains(MainTrack(),f) || (!source.clip.empty() && !ClipTrack().frames.empty() && !contains(ClipTrack(),f))) {
					reason = "Track the full selected subtitle range before Apply."; break;
				}
			}
			if (!reason.empty()) break;
		}
	}
	apply_button->Enable(reason.empty() && !tracking);
	// Advanced Apply can deliberately ignore an incomplete clip track.
	advanced_button->Enable(invalid_reason.empty() && !MainTrack().frames.empty() && !tracking);
	bool revert = false, clip = false;
	for (auto line : context->selectionController->GetSortedSelection()) {
		revert |= motion_tracking::CanRevertMotion(*context->ass,*line);
		clip |= motion_tracking::HasMotionClip(*line);
	}
	revert_button->Enable(revert && !tracking);
	main_track_button->Enable(!tracking);
	clip_track_button->Enable(clip && !tracking);
	std::string status = "Main track: " + std::string(MainTrack().frames.empty() ? "not available" : "available") +
		"; clip track: " + (ClipTrack().frames.empty() ? "not available" : "available");
	if (!apply_summary.empty()) status += "\n" + apply_summary;
	if (!reason.empty()) status += "\n" + reason;
	else if (!apply_summary.empty()) { }
	else status += "\nPosition/style at the main video frame, then Apply.";
	apply_status->SetLabel(to_wx(status));
}

void DialogMotionTrack::SwitchTrack(bool clip) {
	if (clip == editing_clip || tracking) return;
	StopPlayback();
	using std::swap;
	swap(result,other_channel.result);
	swap(segments,other_channel.segments);
	swap(markers,other_channel.markers);
	swap(handoff_marks,other_channel.handoff_marks);
	swap(base_frame,other_channel.base_frame);
	swap(active_segment,other_channel.active_segment);
	swap(initial_marker_size,other_channel.initial_marker_size);
	swap(settings.mode,other_channel.mode);
	mode_choice->SetSelection(static_cast<int>(settings.mode));
	editing_clip = clip;
	UpdatePanels();
}

void DialogMotionTrack::TrackMotion(bool clip) {
	if (tracking) return;
	CheckSession();
	// Replacing/removing a clip invalidates only its pass. A new clip pass can
	// bind the current shape while retaining the already tracked subtitle motion.
	if (clip) {
		std::vector<AssDialogue const*> selected;
		for (auto line : context->selectionController->GetSortedSelection()) selected.push_back(line);
		auto active = context->selectionController->GetActiveLine();
		auto core = motion_tracking::ValidateMotionSources(source_identity,selected,source_active_id,active ? active->Id : 0,false);
		if (core.empty() && (invalid_reason.empty() || invalid_reason == "The tracked clip changed. Track the clip again.")) {
			if (!invalid_reason.empty()) { SwitchTrack(true); ClearData(); }
			for (auto& identity : source_identity) for (auto line : selected)
				if (line->Id == identity.id) identity.clip = motion_tracking::MotionClipSignature(*line);
			invalid_reason.clear();
		}
	}
	if (!invalid_reason.empty()) {
		wxMessageBox(to_wx(invalid_reason),_("Motion Track"),wxOK | wxICON_INFORMATION,this);
		return;
	}
	if (clip) {
		bool applicable = false;
		for (auto line : context->selectionController->GetSortedSelection()) applicable |= motion_tracking::HasMotionClip(*line);
		if (!applicable) return;
	}
	SwitchTrack(clip);
	if (!HasCurrentMarker()) {
		apply_status->SetLabel(clip ? _("Place a tracker square on the clip's object, then press Track for \\clip again.") :
			_("Place a tracker square on the subtitle's object, then press Track Motion again."));
		return;
	}
	int anchor = current_frame;
	TrackRange(settings.end_frame);
	JumpToFrame(anchor);
	TrackRange(settings.start_frame);
	JumpToFrame(anchor);
}

void DialogMotionTrack::NewSession() {
	if (tracking) return;
	StopPlayback();
	StopFrameCache();
	CalculateSelectedFrameRange();
	ClearData();
	other_channel = TrackChannel{};
	other_channel.result = result;
	editing_clip = false;
	CaptureSources();
	StartFrameCache();
	preview_frame = -1;
	LoadCurrentFrame();
}

namespace {
bool AdvancedMotionApply(wxWindow* parent, motion_tracking::MotionApplyOptions& options, bool mangetsu) {
	using namespace motion_tracking;
	wxDialog dialog(parent,-1,_("Advanced Motion Apply"));
	auto layout = new wxBoxSizer(wxVERTICAL);
	auto encoding = new wxChoice(&dialog,-1);
	encoding->Append(_("Automatic"));
	encoding->Append(_("Force optimized"));
	encoding->Append(_("Force frame-by-frame"));
	encoding->SetSelection(static_cast<int>(options.encoding));
	layout->Add(new wxStaticText(&dialog,-1,_("Motion encoding")),0,wxALL,6);
	layout->Add(encoding,0,wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,6);
	layout->Add(new wxStaticText(&dialog,-1,_("Uses the current main video frame. Force optimized permits piecewise regions.")),0,wxALL,6);
	auto grid = new wxFlexGridSizer(2,4,12);
	std::vector<std::pair<wxCheckBox*,bool*>> checks;
	auto check = [&](wxString label, bool& value) {
		auto control = new wxCheckBox(&dialog,-1,label);
		control->SetValue(value);
		grid->Add(control,0,wxALL,2);
		checks.emplace_back(control,&value);
		return control;
	};
	check(_("Position X"),options.position_x);
	check(_("Position Y"),options.position_y);
	check(_("Scale"),options.scale);
	check(_("Rotation"),options.rotation);
	check(_("Move existing origin with object"),options.follow_origin);
	check(_("Scale border"),options.border);
	check(_("Scale shadow"),options.shadow);
	check(_("Scale blur"),options.blur);
	check(_("Preserve source scalar animations"),options.preserve_transforms);
	check(_("Apply rectangular clips"),options.rectangular_clips);
	check(_("Apply vector clips"),options.vector_clips);
	auto clippos = check(_("Mangetsu \\clippos for translation"),options.mangetsu_clippos);
	clippos->Enable(mangetsu);
	if (!mangetsu) { clippos->SetValue(false); options.mangetsu_clippos = false; }
	layout->Add(grid,0,wxALL,6);
	auto clip = new wxChoice(&dialog,-1);
	clip->Append(_("Separate Track for \\clip"));
	clip->Append(_("Follow main motion track"));
	clip->Append(_("Keep clip unchanged"));
	clip->SetSelection(static_cast<int>(options.clip_source));
	layout->Add(new wxStaticText(&dialog,-1,_("Clip behavior")),0,wxLEFT | wxRIGHT,6);
	layout->Add(clip,0,wxEXPAND | wxALL,6);
	auto tolerances = new wxFlexGridSizer(2,4,8);
	std::vector<std::pair<wxSpinCtrlDouble*,double*>> fields;
	auto field = [&](wxString label, double& value, double min, double max) {
		tolerances->Add(new wxStaticText(&dialog,-1,label),0,wxALIGN_CENTER_VERTICAL);
		auto control = new wxSpinCtrlDouble(&dialog,-1,"",wxDefaultPosition,wxDefaultSize,wxSP_ARROW_KEYS,min,max,value,0.01);
		control->SetDigits(2);
		tolerances->Add(control);
		fields.emplace_back(control,&value);
	};
	field(_("Position/clip tolerance (script px)"),options.tolerance.position,0.01,2);
	field(_("Scale tolerance (percentage points)"),options.tolerance.scale,0.01,1);
	field(_("Rotation tolerance (degrees)"),options.tolerance.rotation,0.01,0.5);
	field(_("Border/shadow/blur tolerance (px)"),options.tolerance.outline,0.01,0.5);
	layout->Add(tolerances,0,wxALL,6);
	layout->Add(dialog.CreateStdDialogButtonSizer(wxOK | wxCANCEL),0,wxEXPAND | wxALL,6);
	dialog.SetSizerAndFit(layout);
	dialog.CenterOnParent();
	if (dialog.ShowModal() != wxID_OK) return false;
	options.encoding = static_cast<MotionEncoding>(encoding->GetSelection());
	options.clip_source = static_cast<ClipMotionSource>(clip->GetSelection());
	for (auto const& item : checks) *item.second = item.first->GetValue();
	for (auto const& item : fields) *item.second = item.first->GetValue();
	return true;
}
}

void DialogMotionTrack::ApplyMotion(bool advanced) {
	if (tracking) return;
	StopPlayback();
	CheckSession();
	if (!invalid_reason.empty()) {
		wxMessageBox(to_wx(invalid_reason),_("Motion Apply"),wxOK | wxICON_INFORMATION,this);
		return;
	}
	auto providers = SubtitlesProviderFactory::GetClasses();
	bool mangetsu = OPT_GET("Subtitle/Provider")->GetString() == "Mangetsu" &&
		std::find(providers.begin(),providers.end(),"Mangetsu") != providers.end();
	motion_tracking::MotionApplyOptions options;
	options.mangetsu_clippos = mangetsu;
	if (advanced) {
		options = advanced_options;
		options.mangetsu_clippos = mangetsu && options.mangetsu_clippos;
		if (!AdvancedMotionApply(this,options,mangetsu)) return;
		advanced_options = options;
		CheckSession(); // modal dialog may have dispatched selection/document changes
		if (!invalid_reason.empty()) return;
	}
	// Read directly at Apply time; tracker preview/current_frame is never a reference.
	int reference = context->videoController->GetFrameN();
	int width, height;
	context->ass->GetResolution(width,height);
	auto selection = context->selectionController->GetSortedSelection();
	auto active = context->selectionController->GetActiveLine();
	int active_id = active ? active->Id : 0;
	std::vector<motion_tracking::MotionPlannedSource> plans;
	try {
		for (auto line : selection) {
			auto application = motion_tracking::BuildMotionApplication(*line,
				[&](std::string const& name) { return context->ass->GetStyle(name); },
				MainTrack(),ClipTrack().frames.empty() ? nullptr : &ClipTrack(),reference,
				context->project->Timecodes(),width,height,options);
			apply_summary = application.summary;
			plans.push_back({line->Id,std::move(application)});
		}
		applying = true;
		auto installed = motion_tracking::InstallMotionApplications(*context->ass,plans,active_id,context->videoController->TimeAtFrame(reference));
		context->selectionController->SetSelectionAndActive(Selection(installed.selected.begin(),installed.selected.end()),installed.active);
		context->ass->Commit(_("apply native motion tracking"),AssFile::COMMIT_DIAG_ADDREM | AssFile::COMMIT_DIAG_FULL | AssFile::COMMIT_EXTRADATA);
		// Subtitle-sync may seek when selection changes. Keep the authoritative
		// main playhead at the frame the user chose for this operation.
		context->videoController->JumpToFrame(reference);
		applying = false;
		invalid_reason = "Motion applied. Revert or start a new session before applying again.";
		UpdateApplyStatus();
	}
	catch (std::exception const& error) {
		applying = false;
		wxMessageBox(to_wx(error.what()),_("Motion Apply"),wxOK | wxICON_INFORMATION,this);
		UpdateApplyStatus();
	}
}

void DialogMotionTrack::RevertMotion() {
	if (tracking) return;
	auto selected = context->selectionController->GetSortedSelection();
	auto active = context->selectionController->GetActiveLine();
	int video_frame = context->videoController->GetFrameN();
	try {
		applying = true;
		auto restored = motion_tracking::RevertMotionFamilies(*context->ass,selected,active ? active->Id : 0);
		context->selectionController->SetSelectionAndActive(Selection(restored.selected.begin(),restored.selected.end()),restored.active);
		context->ass->Commit(_("revert native motion tracking"),AssFile::COMMIT_DIAG_ADDREM | AssFile::COMMIT_DIAG_FULL | AssFile::COMMIT_EXTRADATA);
		context->videoController->JumpToFrame(video_frame);
		applying = false;
		CaptureSources(); // keep both tracks; the restored source can be positioned again
		UpdateApplyStatus();
	}
	catch (std::exception const& error) {
		applying = false;
		wxMessageBox(to_wx(error.what()),_("Motion Revert"),wxOK | wxICON_INFORMATION,this);
	}
}
