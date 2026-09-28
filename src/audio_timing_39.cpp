// Copyright (c) 2026, JibunSenyou contributors. ISC license.
#include "audio_timing.h"
#include "timing39_karaoke.h"
#include "timing39_session_setup.h"
#include "timing39_ui.h"
#include "timing39_event_clock.h"
#include "toshiki_timing_draft.h"
#include "frame_main.h"
#include "video_controller.h"
#include "ass_dialogue.h"
#include "ass_file.h"
#include "audio_box.h"
#include "audio_perf.h"
#include "audio_review_plan.h"
#include "audio_controller.h"
#include "audio_rendering_style.h"
#include "compat.h"
#include "options.h"
#include "project.h"
#include "include/aegisub/context.h"
#include "selection_controller.h"
#include <libaegisub/audio/provider.h>
#include <libaegisub/make_unique.h>
#include <libaegisub/timing39_session.h>
#include <libaegisub/timing39_input_clock.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <set>
#include <wx/button.h>
#include <wx/dcbuffer.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/eventfilter.h>
#include <wx/listbox.h>
#include <wx/log.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/splitter.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/timer.h>
#include <wx/vlbox.h>
#include <wx/filedlg.h>
#include <wx/ffile.h>
#include <wx/font.h>

namespace {
namespace t39 = agi::timing39;
namespace t39ui = agi::timing39::ui;

class Timing39ResultsList final : public wxVListBox {
	struct Segment { wxString base, ruby; };
	struct Row { std::vector<Segment> lyric; wxString status_text; t39::Confidence status; };
	std::vector<Row> items;
	static wxColour Colour(t39ui::Rgb rgb) {return {rgb.red,rgb.green,rgb.blue};}
	static wxColour Blend(wxColour base,wxColour tint,int tint_percent) {
		auto channel=[&](int a,int b){return static_cast<unsigned char>((a*(100-tint_percent)+b*tint_percent)/100);};
		return {channel(base.Red(),tint.Red()),channel(base.Green(),tint.Green()),channel(base.Blue(),tint.Blue())};
	}
	wxCoord OnMeasureItem(size_t) const override {return std::max<wxCoord>(46,GetCharHeight()*3+12);}
	void OnDrawBackground(wxDC&,wxRect const&,size_t) const override { }
	void OnDrawItem(wxDC& dc,wxRect const& rect,size_t item) const override {
		auto visual=t39ui::StatusStyle(items[item].status);
		auto accent=Colour(visual.accent);
		auto window=wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
		bool selected=IsSelected(item);
		wxColour background;
		if(selected)background=wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT);
		else {
			int luminance=(window.Red()*299+window.Green()*587+window.Blue()*114)/1000;
			background=luminance>128?Colour(visual.tint):Blend(window,accent,18);
		}
		dc.SetPen(*wxTRANSPARENT_PEN);dc.SetBrush(wxBrush(background));dc.DrawRectangle(rect);
		dc.SetBrush(wxBrush(accent));dc.DrawRectangle(rect.x,rect.y,6,rect.height);
		auto base_font=GetFont(), ruby_font=base_font;
		ruby_font.SetPointSize(std::max(6,base_font.GetPointSize()*3/4));
		auto foreground=selected?wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT):wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
		dc.SetTextForeground(foreground);
		dc.SetClippingRegion(rect);
		dc.SetFont(base_font);
		dc.DrawText(items[item].status_text,rect.x+13,rect.y+(rect.height-dc.GetCharHeight())/2);
		int x=rect.x+std::max(85,dc.GetTextExtent(items[item].status_text).x+30);
		int ruby_y=rect.y+3;
		int base_y=rect.y+rect.height-dc.GetCharHeight()-5;
		for(auto const& segment:items[item].lyric) {
			dc.SetFont(base_font);int base_width=dc.GetTextExtent(segment.base).x;
			dc.SetFont(ruby_font);int ruby_width=segment.ruby.empty()?0:dc.GetTextExtent(segment.ruby).x;
			int width=std::max(base_width,ruby_width);
			if(!segment.ruby.empty())dc.DrawText(segment.ruby,x+(width-ruby_width)/2,ruby_y);
			dc.SetFont(base_font);dc.DrawText(segment.base,x+(width-base_width)/2,base_y);
			x+=width;
		}
		dc.DestroyClippingRegion();
	}
public:
	explicit Timing39ResultsList(wxWindow* parent):wxVListBox(parent,wxID_ANY) {SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));}
	size_t RowCount() const {return items.size();}
	void ResetRows(std::vector<t39::SessionResult> const& results){
		items.clear();items.reserve(results.size());
		for(auto const& result:results){
			auto status=result.GetConfidence();auto prepared=t39ui::PrepareLyricDisplay(result.target.analysis);
			Row row;row.status=status;row.status_text=to_wx(status==t39::Confidence::Green?"GREEN":status==t39::Confidence::Yellow?"YELLOW":"RED");
			row.lyric.reserve(prepared.segments.size());
			for(auto const& part:prepared.segments)row.lyric.push_back({to_wx(part.base),to_wx(part.ruby)});
			items.push_back(std::move(row));
		}
		SetItemCount(items.size());Refresh();
	}
	void SetStatus(size_t item,t39::Confidence status){
		if(item>=items.size()||items[item].status==status)return;
		items[item].status=status;
		items[item].status_text=to_wx(status==t39::Confidence::Green?"GREEN":status==t39::Confidence::Yellow?"YELLOW":"RED");
		RefreshRow(item);
	}
	void RebuildRow(size_t item,t39::Analysis const& analysis,t39::Confidence status){
		if(item>=items.size())return;
		auto prepared=t39ui::PrepareLyricDisplay(analysis);
		auto& row=items[item];row.lyric.clear();row.status=status;
		row.status_text=to_wx(status==t39::Confidence::Green?"GREEN":status==t39::Confidence::Yellow?"YELLOW":"RED");
		for(auto const& part:prepared.segments)row.lyric.push_back({to_wx(part.base),to_wx(part.ruby)});
		RefreshRow(item);
	}
};

class Timing39ReviewView final : public wxPanel {
	AudioBox *audio_box;
	t39ui::LocalReviewModel model;
	std::vector<std::string> morae;
	std::vector<size_t> disputed;
	std::vector<size_t> pending;
	t39::Confidence confidence=t39::Confidence::Red;
	wxBitmap audio_bitmap;
	t39ui::ReviewBitmapKey cached_key;
	bool cache_valid=false;
	int playback_cursor=-1;
	bool rebuild_queued=false;
	agi::signal::Connection review_change_connection;
	wxTimer resize_timer;
	uint64_t render_serial=0;
	int render_slice=0;
	int render_slice_count=0;
	toshiki_timing::Draft *timing_draft=nullptr;
	int drag_index=-1;
	bool drag_start=false;
	std::vector<t39::TimingBlock> drag_original;
	int X(int ms) const {
		return model.end<=model.begin?0:int(int64_t(ms-model.begin)*GetClientSize().x/(model.end-model.begin));
	}
	int TimeFromX(int x) const {
		return model.begin+int(int64_t(std::max(0,std::min(x,GetClientSize().x)))*
			(model.end-model.begin)/std::max(1,GetClientSize().x));
	}
	std::pair<int,bool> NearbyMarker(int x,int sensitivity=8) const {
		if(!timing_draft)return {-1,false};
		int best=sensitivity+1,index=-1;bool at_start=false;
		for(size_t i=0;i<timing_draft->Blocks().size();++i){
			auto const& block=timing_draft->Blocks()[i];
			for(bool start:{true,false}){
				int distance=std::abs(X(start?block.start:block.end)-x);
				if(distance<best){best=distance;index=int(i);at_start=start;}
			}
		}
		return {index,at_start};
	}
	void OnLeftDown(wxMouseEvent& event){
		if(!timing_draft||event.GetY()<18||event.GetY()>118){event.Skip();return;}
		auto marker=NearbyMarker(event.GetX());
		if(marker.first>=0){
			drag_index=marker.first;drag_start=marker.second;
			drag_original=timing_draft->Blocks();CaptureMouse();
		}
		else if(timing_draft->Split(TimeFromX(event.GetX())))Refresh(false);
	}
	void OnMotion(wxMouseEvent& event){
		if(!timing_draft||drag_index<0||!event.Dragging())return;
		if(timing_draft->PreviewMove(size_t(drag_index),drag_start,TimeFromX(event.GetX())))Refresh(false);
	}
	void OnLeftUp(wxMouseEvent& event){
		if(drag_index<0){event.Skip();return;}
		if(HasCapture())ReleaseMouse();
		if(timing_draft)timing_draft->FinishPreview(drag_original);
		drag_index=-1;drag_original.clear();Refresh(false);
	}
	void OnRightDown(wxMouseEvent& event){
		if(!timing_draft||event.GetY()<18||event.GetY()>118){event.Skip();return;}
		auto const& blocks=timing_draft->Blocks();
		for(size_t i=0;i+1<blocks.size();++i)
			if(std::abs(X((blocks[i].end+blocks[i+1].start)/2)-event.GetX())<=10){
				if(timing_draft->Join(i))Refresh(false);return;
			}
	}
	void RebuildAudio() {
		AudioPerf::Scope timer(AudioPerf::ReviewBitmap);
		auto size=GetClientSize();
		wxSize image_size(size.x,100);
		if(size.x<=0||model.end<=model.begin){++render_serial;cache_valid=false;audio_bitmap=wxBitmap();return;}
		t39ui::ReviewBitmapKey key{model.begin,model.end,image_size.x,image_size.y,
			audio_box->ReviewGeneration()};
		AudioReviewPlan plan{model.begin,model.end,image_size.x};
		bool hit=cache_valid&&audio_bitmap.IsOk()&&cached_key==key;
		AudioPerf::Instance().ReviewRequested(hit,image_size.x,
			plan.MillisecondsPerPixel(),plan.SliceCount());
		if(hit)return;
		++render_serial;
		audio_bitmap=wxBitmap(image_size.x,image_size.y);
		if(audio_bitmap.IsOk()){
			wxMemoryDC dc(audio_bitmap);
			dc.SetBackground(wxBrush(GetBackgroundColour()));dc.Clear();
			dc.SelectObject(wxNullBitmap);
		}
		cached_key=key;cache_valid=audio_bitmap.IsOk();
		render_slice=0;
		render_slice_count=audio_box->ReviewAudioSliceCount(model.begin,model.end,image_size.x);
		if(cache_valid&&render_slice_count>0){
			auto serial=render_serial;
			CallAfter([this,serial]{RenderNextSlice(serial);});
		}
		Refresh(false);
	}
	void RenderNextSlice(uint64_t serial){
		if(serial!=render_serial||!cache_valid||render_slice>=render_slice_count)return;
		if(cached_key.generation!=audio_box->ReviewGeneration()){
			QueueRebuild();return;
		}
		auto part=audio_box->RenderReviewAudioSlice(cached_key.begin,cached_key.end,
			wxSize(cached_key.width,cached_key.height),render_slice++);
		if(part.second.IsOk()){
			wxMemoryDC dc(audio_bitmap);
			dc.DrawBitmap(part.second,part.first,0);
			dc.SelectObject(wxNullBitmap);
			RefreshRect(wxRect(part.first,18,part.second.GetWidth(),part.second.GetHeight()),false);
		}
		if(render_slice<render_slice_count)CallAfter([this,serial]{RenderNextSlice(serial);});
	}
	void QueueRebuild(){
		if(rebuild_queued)return;
		rebuild_queued=true;
		CallAfter([this]{rebuild_queued=false;RebuildAudio();});
	}
	void OnPaint(wxPaintEvent&) {
		wxAutoBufferedPaintDC dc(this);
		auto size=GetClientSize();
		auto base=wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
		auto text=wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
		bool dark=(base.Red()*299+base.Green()*587+base.Blue()*114)/1000<128;
		wxColour neutral=dark?wxColour(150,156,164):wxColour(105,112,119);
		wxColour accent=dark?wxColour(81,204,196):wxColour(28,125,119);
		wxColour amber=dark?wxColour(241,188,73):wxColour(183,121,31);
		wxColour red=dark?wxColour(237,112,112):wxColour(198,40,40);
		dc.SetBackground(wxBrush(base));dc.Clear();
		if(model.end<=model.begin)return;
		if(audio_bitmap.IsOk())dc.DrawBitmap(audio_bitmap,0,18);
		else {dc.SetTextForeground(neutral);dc.DrawText(_("Audio preview unavailable"),6,50);}
		dc.SetTextForeground(text);
		dc.DrawText(_("Raw"),5,123);
		dc.DrawText(_("Adjusted"),5,150);
		dc.DrawText(_("Assignment"),5,180);
		for(int checkpoint:{model.line_start,model.line_end}) {
			int x=X(checkpoint);
			dc.SetPen(wxPen(amber,2));dc.DrawLine(x,17,x,210);
			wxString label=checkpoint==model.line_start?_("Line start"):_("Line end");
			dc.SetTextForeground(amber);
			dc.DrawText(label,std::max(0,std::min(x+3,size.x-dc.GetTextExtent(label).x)),0);
		}
		for(auto const& item:model.raw) {
			if(item.raw.gap)continue;
			int x=X(item.raw.start),end=X(item.raw.end);
			bool excluded=item.ownership==t39ui::ReviewOwnership::ExcludedPreviousTail ||
				item.ownership==t39ui::ReviewOwnership::ExcludedOtherLine;
			wxColour colour=item.ownership==t39ui::ReviewOwnership::CrossingNeedsReview?red:neutral;
			dc.SetPen(wxPen(colour,1));
			dc.SetBrush(excluded?
				wxBrush(colour,wxBRUSHSTYLE_FDIAGONAL_HATCH):*wxTRANSPARENT_BRUSH);
			dc.DrawRectangle(x,123,std::max(2,end-x),17);
		}
		for(size_t i=0;i<model.local.size();++i) {
			auto const& b=model.local[i];if(b.gap)continue;
			int x=X(b.start),end=X(b.end);
			dc.SetPen(wxPen(confidence==t39::Confidence::Red?red:accent,2));
			dc.SetBrush(*wxTRANSPARENT_BRUSH);
			dc.DrawRectangle(x,150,std::max(2,end-x),20);
		}
		for(auto const& item:model.raw) {
			if(item.ownership==t39ui::ReviewOwnership::ExcludedPreviousTail ||
				item.ownership==t39ui::ReviewOwnership::ExcludedOtherLine) {
				dc.SetPen(wxPen(neutral,1,wxPENSTYLE_DOT));
				dc.SetBrush(wxBrush(neutral,wxBRUSHSTYLE_FDIAGONAL_HATCH));
				dc.DrawRectangle(X(item.adjusted.start),150,
					std::max(2,X(item.adjusted.end)-X(item.adjusted.start)),20);
			}
			else {
				dc.SetPen(wxPen(neutral,1,wxPENSTYLE_DOT));
				if(item.adjusted.start<model.line_start)
					dc.DrawLine(X(item.adjusted.start),173,X(model.line_start),173);
				if(item.adjusted.end>model.line_end)
					dc.DrawLine(X(model.line_end),173,X(item.adjusted.end),173);
			}
		}
		for(auto block:pending)if(block<model.local.size()){
			auto const& b=model.local[block];
			dc.SetPen(wxPen(amber,3));dc.DrawLine(X(b.start),146,X(b.end),146);
		}
		for(auto const& assignment:model.assignments) {
			if(assignment.timing_block>=model.local.size())continue;
			auto const& b=model.local[assignment.timing_block];
			int x=X(b.start),end=X(b.end);
			dc.SetPen(wxPen(accent,2));dc.SetBrush(*wxTRANSPARENT_BRUSH);
			dc.DrawRectangle(x,180,std::max(2,end-x),19);
			std::string label;
			for(size_t i=0;i<assignment.mora_count&&assignment.first_mora+i<morae.size();++i)
				label+=morae[assignment.first_mora+i];
			if(end-x>dc.GetTextExtent(to_wx(label)).x+5) {
				dc.SetTextForeground(text);dc.DrawText(to_wx(label),x+3,199);
			}
		}
		if(timing_draft){
			wxColour edit=dark?wxColour(203,143,238):wxColour(117,54,156);
			dc.SetPen(wxPen(edit,2));dc.SetBrush(*wxTRANSPARENT_BRUSH);
			for(auto const& block:timing_draft->Blocks()){
				int left=X(block.start),right=X(block.end);
				dc.DrawRectangle(left,76,std::max(2,right-left),38);
				dc.DrawCircle(left,76,3);dc.DrawCircle(right,76,3);
			}
		}
		for(auto boundary:disputed)if(boundary<model.assignments.size()) {
			auto block=model.assignments[boundary].timing_block;
			if(block<model.local.size()){
				int x=X(model.local[block].start);
				dc.SetPen(wxPen(amber,3));dc.DrawLine(x,145,x,201);
			}
		}
		if(playback_cursor>=model.begin&&playback_cursor<model.end){
			int x=X(playback_cursor);dc.SetPen(wxPen(text,1));dc.DrawLine(x,18,x,118);
		}
		dc.SetTextForeground(neutral);
		dc.DrawText(_("Hatch: sung block owned elsewhere   ·   dotted: harmless clamp"),5,218);
	}
public:
	Timing39ReviewView(wxWindow* parent,AudioBox* audio):wxPanel(parent,wxID_ANY,wxDefaultPosition,wxSize(-1,238)),audio_box(audio){
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		review_change_connection=agi::signal::Connection(audio_box->AddReviewAudioChangedListener(
			[this]{QueueRebuild();}));
		resize_timer.Bind(wxEVT_TIMER,[this](wxTimerEvent&){RebuildAudio();});
		Bind(wxEVT_PAINT,&Timing39ReviewView::OnPaint,this);
		Bind(wxEVT_SIZE,[this](wxSizeEvent& event){
			if(GetClientSize().x!=cached_key.width){
				++render_serial;cache_valid=false;resize_timer.StartOnce(80);
			}
			event.Skip();
		});
		Bind(wxEVT_LEFT_DOWN,&Timing39ReviewView::OnLeftDown,this);
		Bind(wxEVT_LEFT_UP,&Timing39ReviewView::OnLeftUp,this);
		Bind(wxEVT_MOTION,&Timing39ReviewView::OnMotion,this);
		Bind(wxEVT_RIGHT_DOWN,&Timing39ReviewView::OnRightDown,this);
	}
	void SetReview(t39ui::LocalReviewModel next,t39::Analysis const& analysis,
		std::vector<size_t> boundaries,t39::Confidence status) {
		model=std::move(next);morae.clear();morae.reserve(analysis.morae.size());
		for(auto const& mora:analysis.morae)morae.push_back(mora.text);
		disputed=std::move(boundaries);confidence=status;playback_cursor=-1;
		RebuildAudio();Refresh(false);
	}
	void SetPlaybackCursor(int ms){
		if(ms==playback_cursor)return;
		int old=playback_cursor;
		playback_cursor=ms;
		if(!IsShownOnScreen())return;
		for(int position:{old,ms})if(position>=model.begin&&position<model.end)
			RefreshRect(wxRect(X(position)-2,18,5,101),false);
	}
	void SetDraft(std::vector<t39::TimingAssignment> assignments,std::vector<size_t> next_blocks){
		model.assignments=std::move(assignments);pending=std::move(next_blocks);Refresh(false);
	}
	void ClearDraft(){pending.clear();Refresh(false);}
	void SetTimingDraft(toshiki_timing::Draft *draft){
		if(HasCapture())ReleaseMouse();drag_index=-1;timing_draft=draft;Refresh(false);
	}
	std::pair<int,int> PlaybackRange(bool context) const {
		return t39ui::ReviewPlaybackRange(model,context);
	}
};

class AudioTimingController39 final : public AudioTimingController, public wxEventFilter {
 agi::Context* c;
 t39::Timing39Session session;
 t39::InputClockMapper input_clock;
 t39::SessionPlaybackStart full_start;
 std::map<uint64_t,AssDialogue*> events;
 t39ui::TimelineIntervalIndex ordinary_boundaries,target_boundaries;
 std::vector<agi::signal::Connection> connections;
 wxTimer countdown;
 wxDialog *panel=nullptr,*inspector=nullptr;
 Timing39ResultsList *rows=nullptr;
 wxPanel *correction_pane=nullptr;
 wxScrolledWindow *review_scroll=nullptr;
 Timing39ReviewView *review_view=nullptr;
 wxPanel *manual_pane=nullptr;
 wxPanel *placement_pane=nullptr;
 t39::MoraPlacementDraft placement_draft;
 bool placement_mode=false;
 size_t placement_row=0;
 int placement_lane=0;
 wxListBox *commit_blocks=nullptr;
 wxTextCtrl *commit_preview=nullptr;
 wxChoice *latin_words=nullptr;
 wxSpinCtrl *latin_cut=nullptr;
 wxStaticText *placement_hint=nullptr,*candidate_guidance=nullptr;
 std::vector<size_t> commit_row_assignments,latin_mora_indices;
 wxPanel *timing_pane=nullptr;
 wxButton *reset_timing=nullptr;
 toshiki_timing::Draft timing_draft;
 bool timing_mode=false;
 size_t timing_row=0;
 int timing_lane=0;
 wxButton *reset_manual=nullptr;
 wxStaticText *manual_progress=nullptr;
 bool manual_mode=false;
 size_t manual_row=0;
 int manual_lane=0,manual_timing_count=1,manual_mora_count=1;
 std::vector<std::pair<int,int>> manual_groups;
 wxBoxSizer *choice_sizer=nullptr;
 wxListBox *assignments=nullptr;
 wxChoice *lane_choice=nullptr,*paths=nullptr;
 wxStaticText *status=nullptr,*line_title=nullptr,*reason=nullptr,*offset_direction=nullptr;
 wxSpinCtrl *offset_spin=nullptr;
 wxTimer correction_timer;
 std::vector<wxButton*> candidate_buttons;
 wxStaticText *resolved_label=nullptr;
 wxButton *raw_toggle=nullptr;
 wxTextCtrl *reading=nullptr,*details=nullptr;
 size_t selected=0;
 int selected_lane=0,last_position=0;
 unsigned rhythm_serial=0;
 std::array<bool,4> physical_held{{false,false,false,false}};
 bool committing=false,refreshing=false,show_full_session_raw=false;
 std::string notice;
 int Preroll() const {return std::max(750,int(OPT_GET("Audio/Lead/IN")->GetInt()));}
 void Notify(){AnnounceMarkerMoved();AnnounceLabelChanged();}
 t39::SessionResult* Current(){return selected<session.Results().size()?&session.Results()[selected]:nullptr;}
 static std::string Color(t39::Confidence s){return s==t39::Confidence::Green?"GREEN":s==t39::Confidence::Yellow?"YELLOW":"RED";}
 void HideCountdown() {countdown.Stop();c->frame->Show39Countdown(0);}
 void BeginCountdown() {
  // AudioPlayer has no paused seek: retain the exact absolute cursor and open
  // the device only after Ready. Video can display its containing frame now.
  last_position=session.StartTime();
  c->audioController->SeekWhileStopped(last_position);
  c->videoController->JumpToTime(last_position);
  c->frame->Show39Countdown(3);
  countdown.Start(1000);
 }
 void Prepare() {
  auto active=c->selectionController->GetActiveLine();
  auto provider=c->project->AudioProvider();
  if(!active||!provider){notice="Select a lyric line and open audio";return;}
  c->videoController->Stop();
  c->audioController->Stop();
  std::vector<AssDialogue*> all;
  for(auto& line:c->ass->Events)all.push_back(&line);
  auto const& chosen=c->selectionController->GetSelectedSet();
  int media_end=int(provider->GetNumSamples()*1000/provider->GetSampleRate());
   auto setup=t39::BuildSessionSetup(all,{chosen.begin(),chosen.end()},active,media_end);
   full_start=setup.playback_start;
   for(auto const& target:setup.targets)events[target.id]=all[target.id-1];
  if(full_start.time>=media_end) {
   notice="39 Mode start is outside the media; move the comment marker to a playable time";
    c->frame->StatusTimeout(to_wx(notice));return;
   }
   auto preview_targets=t39::DiscoverTargets(setup.targets,setup.explicit_scope,setup.active_style,full_start.time,setup.playback_end);
   std::set<uint64_t> target_ids;std::vector<std::pair<int,int>> target_spans,ordinary_spans;
   for(auto const& target:preview_targets){target_ids.insert(target.id);target_spans.emplace_back(target.start,target.end);}
   for(size_t i=0;i<all.size();++i)if(!all[i]->Comment&&int(all[i]->End)>int(all[i]->Start)) {
    if(!target_ids.count(i+1))ordinary_spans.emplace_back(int(all[i]->Start),int(all[i]->End));
   }
   target_boundaries.Reset(std::move(target_spans));ordinary_boundaries.Reset(std::move(ordinary_spans));
   session.Prepare(std::move(setup.targets),setup.explicit_scope,setup.active_style,full_start.time,setup.playback_end);
  BeginCountdown();
 }
 void Tick(wxTimerEvent&) {
  if(session.State()!=t39::SessionState::Countdown){HideCountdown();return;}
  if(session.TickCountdown()) {
   HideCountdown();
   try {
    if(c->project->VideoProvider())c->videoController->PlayRange(session.StartTime(),session.EndTime());
    else c->audioController->PlayRange(TimeRange(session.StartTime(),session.EndTime()));
    if(!c->audioController->IsPlaying())PlaybackStopped(session.StartTime());
   }
   catch(...) {
    notice="Playback could not start. Capture stopped; check the media/audio device.";
    c->audioController->Stop();PlaybackStopped(session.StartTime());
   }
  }
  else c->frame->Show39Countdown(session.Countdown());
  Notify();
 }
 void ShowResults(){if(!panel)MakePanel();Update();panel->Layout();if(review_scroll)review_scroll->FitInside();panel->Show();panel->Raise();}
 void FileChanged(int type,AssDialogue const*) {
  if(committing||!(type&(AssFile::COMMIT_DIAG_FULL|AssFile::COMMIT_DIAG_ADDREM)))return;
  if(timing_mode)CloseTimingDraft();
   c->audioController->Stop();countdown.Stop();session.Discard();events.clear();ordinary_boundaries.Reset({});target_boundaries.Reset({});
  notice="Subtitle edit or undo invalidated the cached targets. Toggle 39 Mode to start a new session.";
  if(inspector)inspector->Hide();Update();Notify();
 }
	void Update(bool all_rows=false) {
   AudioPerf::Scope update_timer(AudioPerf::ResultsUpdate);
   if(!panel)return;refreshing=true;
   if(offset_spin && !correction_timer.IsRunning() && offset_spin->GetValue()!=session.TimingCorrection())
    offset_spin->SetValue(session.TimingCorrection());
   if(offset_spin)offset_spin->Enable(std::none_of(session.Results().begin(),session.Results().end(),
    [](t39::SessionResult const& r){return r.committed;}));
   UpdateOffsetDirection();
   if(rows->RowCount()!=session.Results().size()) {
    rows->ResetRows(session.Results());
   }
	   else if(all_rows)for(size_t i=0;i<session.Results().size();++i)
	    rows->SetStatus(i,session.Results()[i].GetConfidence());
	   else if(auto current=Current())rows->SetStatus(selected,current->GetConfidence());
   if(auto r=Current()) {
    if(rows->GetSelection()!=int(selected))rows->SetSelection(int(selected));
    lane_choice->SetSelection(r->lane+1);
    status->SetLabel(to_wx(notice));
  }else status->SetLabel(to_wx(notice.empty()?"No captured lyric targets. Toggle 39 Mode to start again.":notice));
  UpdatePane();
  if(inspector&&inspector->IsShown())UpdateInspector();
  refreshing=false;
  if(auto summary=AudioPerf::Instance().MaybeSummary();!summary.empty())
   wxLogMessage(wxString::FromUTF8(summary.c_str()));
 }
 void RefreshCommitPreview() {
  if(!commit_blocks||!commit_preview)return;
  auto r=Current();int selected_block=commit_blocks->GetSelection();
  commit_blocks->Clear();commit_row_assignments.clear();
  if(!r||selected_lane<0||selected_lane>1){commit_preview->ChangeValue("");return;}
  auto event=events.find(r->target.id);
  if(event==events.end()){commit_preview->ChangeValue(_("Source event unavailable"));return;}
  bool draft=placement_mode&&placement_row==selected&&placement_lane==selected_lane;
  auto const& analysis=draft?placement_draft.CurrentAnalysis():r->target.analysis;
  auto const& lane=r->lanes[selected_lane];
  auto const& mapping=draft?placement_draft.Assignments():lane.Assignments();
  t39::CommitPlan plan;std::string output,error;
  if(!t39::BuildCommitPlan(*event->second,analysis,lane.capture.blocks,mapping,plan,error,
   draft||lane.manual_active)||!t39::SerializeCommitPlan(*event->second,plan,output,error)){
   commit_preview->ChangeValue(to_wx("No complete commit preview: "+error));return;
  }
  for(auto const& atom:plan.atoms){
   std::string role=atom.kind==t39::CommitAtomKind::Sung?"SUNG  ":
    atom.kind==t39::CommitAtomKind::CapturedGap?"GAP    ":"ZERO   ";
   commit_blocks->Append(to_wx(role+atom.fragment));
   commit_row_assignments.push_back(atom.assignment);
  }
  if(selected_block>=0&&selected_block<int(commit_blocks->GetCount()))
   commit_blocks->SetSelection(selected_block);
  commit_preview->ChangeValue(to_wx(output));
  if(draft&&latin_words){
   int prior=latin_words->GetSelection();latin_words->Clear();latin_mora_indices.clear();
   for(size_t i=0;i<analysis.morae.size();++i){
    auto const& word=analysis.morae[i].text;
    if(word.empty()||!std::isalpha(static_cast<unsigned char>(word.front()))||
     !std::all_of(word.begin(),word.end(),[](unsigned char ch){
      return ch<128&&(std::isalpha(ch)||ch=='\''||ch=='-');}))continue;
    latin_mora_indices.push_back(i);
    latin_words->Append(to_wx(word+" (mora "+std::to_string(i+1)+")"));
   }
   if(!latin_mora_indices.empty())latin_words->SetSelection(std::max(0,std::min(prior,int(latin_mora_indices.size()-1))));
   if(latin_cut&&latin_words->GetSelection()>=0){
    auto const& word=analysis.morae[latin_mora_indices[size_t(latin_words->GetSelection())]].text;
    latin_cut->SetRange(1,std::max(1,int(word.size())-1));
    latin_cut->Enable(word.size()>1);
   }
   else if(latin_cut)latin_cut->Disable();
  }
 }
 void UpdatePane() {
  AudioPerf::Scope pane_timer(AudioPerf::ResultsPane);
  if(!correction_pane)return;
  auto r=Current();
  if(!r){
   line_title->SetLabel("");reason->SetLabel("");
   for(auto button:candidate_buttons)button->Hide();
   if(resolved_label)resolved_label->Hide();
   if(reset_manual)reset_manual->Disable();
   if(review_view)review_view->SetReview({},t39::Analysis{}, {},t39::Confidence::Red);
   RefreshCommitPreview();
   if(review_scroll)review_scroll->FitInside();return;
  }
  auto readable=t39ui::PrepareLyricDisplay(r->target.analysis).plain;
  line_title->SetLabel(to_wx(readable));
  std::string explanation=t39ui::CompactResultReason(*r,selected_lane);
  if(r->resolution==t39::ResolutionSource::UserSelected && r->GetConfidence()==t39::Confidence::Green)
   explanation="Selected assignment resolved this line.";
  if(r->resolution==t39::ResolutionSource::UserManualRepair && r->GetConfidence()==t39::Confidence::Green)
   explanation="Mora placement resolved this line; raw attacks are unchanged.";
  if(r->resolution==t39::ResolutionSource::UserEnglishSegmentation && r->GetConfidence()==t39::Confidence::Green)
   explanation="English segmentation resolved this line; spelling and raw attacks are unchanged.";
  if(r->resolution==t39::ResolutionSource::UserManualTiming && r->GetConfidence()==t39::Confidence::Green)
   explanation="Manual timing resolved this line; raw attacks are unchanged.";
  if(r->manual_invalidated)explanation="Previous selected assignment no longer fits the corrected taps.\n"+explanation;
  reason->SetLabel(to_wx(explanation));
  if(candidate_guidance){
   candidate_guidance->SetLabel(to_wx(t39ui::PossibleGroupingGuidance(r->target.analysis)));
   candidate_guidance->Wrap(std::max(250,review_scroll->GetClientSize().x-24));
  }
  line_title->Wrap(std::max(250,review_scroll->GetClientSize().x-24));
  reason->Wrap(std::max(250,review_scroll->GetClientSize().x-24));
  AudioPerf::Scope candidate_timer(AudioPerf::ResultsCandidates);
  auto& lane=r->lanes[selected_lane];
  if(reset_manual)reset_manual->Enable(lane.manual_active&&!r->committed);
  if(reset_timing)reset_timing->Enable(lane.timing_override_active&&!r->committed);
  if(review_view)review_view->SetReview(t39ui::BuildLocalReviewModel(*r,selected_lane,
   session.RawForResult(selected,selected_lane),session.TimingCorrection()),r->target.analysis,
   lane.match.uncertain_boundaries,r->GetConfidence());
  if(manual_mode&&(manual_row!=selected||manual_lane!=selected_lane))CloseManual();
  if(placement_mode&&(placement_row!=selected||placement_lane!=selected_lane))ClosePlacement();
  UpdateManualProgress();
  RefreshCommitPreview();
  size_t visible_choices=0;
  if(r->GetConfidence()==t39::Confidence::Yellow &&
     lane.capture.status!=t39::PartitionStatus::NeedsReview && !lane.match.paths.empty()) {
   for(size_t i=0;i<lane.match.paths.size();++i) {
    if(i && (lane.match.confidence!=t39::Confidence::Yellow ||
       lane.match.paths[i].cost-lane.match.paths[0].cost>=t39::ScoringModel{}.ambiguity_margin))break;
    auto label=t39ui::CompactPathChoice(r->target.analysis,lane.match,i);
    if(label.empty())label="Assignment "+std::to_string(i+1);
    if(i>=candidate_buttons.size()){
     auto option=new wxButton(review_scroll,wxID_ANY,to_wx(label));
     choice_sizer->Add(option,0,wxEXPAND|wxALL,4);candidate_buttons.push_back(option);
     option->Bind(wxEVT_BUTTON,[this,i](wxCommandEvent&){
      if(!MayLeavePlacement())return;
      if(session.ChooseAssignment(selected,selected_lane,i)){notice.clear();panel->CallAfter([this]{Update();});}
     });
    }
    auto option=candidate_buttons[i];
    if(option->GetLabel()!=to_wx(label))option->SetLabel(to_wx(label));
    option->Show();++visible_choices;
   }
  }
  for(size_t i=visible_choices;i<candidate_buttons.size();++i)candidate_buttons[i]->Hide();
  if(resolved_label)resolved_label->Show(r->GetConfidence()==t39::Confidence::Green);
  candidate_timer.Stop();
  {AudioPerf::Scope layout_timer(AudioPerf::ResultsLayout);review_scroll->Layout();review_scroll->FitInside();}
 }
 void CloseManual() {
  manual_mode=false;manual_groups.clear();
  if(review_view&&Current())review_view->SetDraft(Current()->lanes[selected_lane].Assignments(),{});
  if(manual_pane){manual_pane->Hide();review_scroll->FitInside();}
 }
 void ClosePlacement() {
  placement_mode=false;
  if(placement_pane){placement_pane->Hide();review_scroll->FitInside();}
  if(review_view&&Current())review_view->SetDraft(Current()->lanes[selected_lane].Assignments(),{});
 }
 bool MayLeavePlacement() {
  if(!placement_mode)return true;
  if(placement_draft.Changed()){
   wxMessageDialog confirm(panel,_("Discard unapplied mora placement changes?"),
    _("39 Mode mora placement"),wxYES_NO|wxNO_DEFAULT|wxICON_QUESTION);
   if(confirm.ShowModal()!=wxID_YES)return false;
  }
  ClosePlacement();return true;
 }
 void StartPlacement() {
  auto r=Current();if(!r||r->committed)return;
  if(placement_mode&&!MayLeavePlacement())return;
  if(timing_mode&&!MayLeaveTimingDraft())return;
  if(manual_mode)CloseManual();
  auto const& lane=r->lanes[selected_lane];
  if(!t39ui::SungTapCount(lane.capture)||r->target.analysis.morae.empty()){
   notice="Mora placement needs at least one sung attack and readable source text.";Update();return;
  }
  placement_draft.Begin(r->target.analysis,lane.capture.blocks,lane.Assignments());
  placement_mode=true;placement_row=selected;placement_lane=selected_lane;
  placement_pane->Show();
  review_view->SetDraft(placement_draft.Assignments(),{});
  notice="Mora placement is a draft until Apply placement.";Update();
 }
 void UpdatePlacementDraft() {
  if(!placement_mode)return;
  if(review_view)review_view->SetDraft(placement_draft.Assignments(),{});
  RefreshCommitPreview();
  placement_pane->Layout();review_scroll->FitInside();
 }
 size_t SelectedPlacementDivider() const {
  if(!commit_blocks)return t39::unknown;
  int row=commit_blocks->GetSelection();
  return row>=0&&size_t(row)<commit_row_assignments.size()?
   commit_row_assignments[size_t(row)]:t39::unknown;
 }
 void MovePlacement(int delta) {
  auto divider=SelectedPlacementDivider();
  if(!placement_mode||divider==t39::unknown||!placement_draft.Move(divider,delta)){
   placement_hint->SetLabel(_("Select the sung block on the right of a movable boundary."));return;
  }
  placement_hint->SetLabel(_("Boundary moved; capture timestamps and source anchors are unchanged."));
  UpdatePlacementDraft();
 }
 void MergePlacement() {
  auto divider=SelectedPlacementDivider();
  if(!placement_mode||divider==t39::unknown||!placement_draft.Merge(divider)){
   placement_hint->SetLabel(_("Select the right sung block of two adjacent assignments."));return;
  }
  placement_hint->SetLabel(_("Neighboring attacks now share one source group."));
  UpdatePlacementDraft();
 }
 void CutLatin(bool remove) {
  int choice=latin_words?latin_words->GetSelection():-1;
  if(!placement_mode||choice<0||size_t(choice)>=latin_mora_indices.size())return;
  size_t mora=latin_mora_indices[size_t(choice)];
  bool changed=remove?placement_draft.RemoveLatinCut(mora):
   placement_draft.CutLatin(mora,size_t(latin_cut->GetValue()));
  placement_hint->SetLabel(changed?_("Latin cut changed; spelling and source order are unchanged."):
   _("Choose a character boundary inside a Latin segment, or move the assignment boundary before removing a cut."));
  if(changed)UpdatePlacementDraft();
 }
 void ApplyPlacement() {
  if(!placement_mode||placement_row!=selected||placement_lane!=selected_lane)return;
  auto r=Current();if(!r)return;
  auto old_analysis=r->target.analysis;
  auto old_original=r->original_analysis;
  auto& lane=r->lanes[selected_lane];
  auto old_match=lane.match;
  auto old_editor=lane.editor;
  if(placement_draft.HasLatinCuts()&&!r->original_analysis)
   r->original_analysis=std::make_shared<t39::Analysis>(old_analysis);
  r->target.analysis=placement_draft.CurrentAnalysis();
  lane.match=t39::Match(r->target.analysis,lane.capture.blocks);
  lane.editor.Reset(lane.match.paths.empty()?std::vector<t39::TimingAssignment>{}:
   lane.match.paths[0].assignments);
  if(!session.SetManualAssignment(selected,selected_lane,placement_draft.Assignments())){
   r->target.analysis=std::move(old_analysis);r->original_analysis=std::move(old_original);
   lane.match=std::move(old_match);lane.editor=std::move(old_editor);
   placement_hint->SetLabel(_("Placement is incomplete; no source text was changed."));return;
  }
  if(placement_draft.HasLatinCuts())r->resolution=t39::ResolutionSource::UserEnglishSegmentation;
  notice="Mora placement applied locally; source will change only on Commit all GREEN.";
  ClosePlacement();Update();
 }
 void CloseTimingDraft() {
  timing_mode=false;
  if(review_view)review_view->SetTimingDraft(nullptr);
  if(timing_pane){timing_pane->Hide();review_scroll->FitInside();}
 }
 bool MayLeaveTimingDraft() {
  if(!timing_mode)return true;
  if(timing_draft.Changed()){
   wxMessageDialog confirm(panel,_("Discard the unapplied manual timing changes?"),
    _("39 Mode manual timing"),wxYES_NO|wxNO_DEFAULT|wxICON_QUESTION);
   if(confirm.ShowModal()!=wxID_YES)return false;
  }
  CloseTimingDraft();return true;
 }
 void StartTimingDraft() {
  auto r=Current();if(!r||r->committed)return;
  if(!MayLeavePlacement())return;
  if(timing_mode&&!MayLeaveTimingDraft())return;
  if(manual_mode)CloseManual();
  timing_draft.Begin(r->target.start,r->target.end,r->lanes[selected_lane].capture.blocks);
  timing_mode=true;timing_row=selected;timing_lane=selected_lane;
  review_view->SetTimingDraft(&timing_draft);
  timing_pane->Show();review_scroll->FitInside();
 }
 void ApplyTimingDraft() {
  if(!timing_mode||timing_row!=selected||timing_lane!=selected_lane)return;
  auto draft=timing_draft.Blocks();
  if(session.SetManualTiming(selected,selected_lane,draft)){
   notice="Manual Toshiki timing applied; raw rhythm capture is unchanged.";
   CloseTimingDraft();Update();
  }
  else {notice="Manual timing is outside the line or has overlapping blocks.";Update();}
 }
 void StartManual() {
  auto r=Current();if(!r||r->committed)return;
  if(!MayLeavePlacement())return;
  if(timing_mode&&!MayLeaveTimingDraft())return;
  if(!t39ui::SungTapCount(r->lanes[selected_lane].capture)||r->target.analysis.morae.empty()){
   notice="Manual repair needs a sung attack and a readable mora; retake or provide a reading.";Update();return;
  }
  manual_mode=true;manual_row=selected;manual_lane=selected_lane;
  manual_groups.clear();manual_timing_count=manual_mora_count=1;
  manual_pane->Show();UpdateManualProgress();review_scroll->FitInside();
 }
 void UpdateManualProgress() {
  AudioPerf::Scope timer(AudioPerf::ResultsManual);
  if(!manual_pane||!manual_mode||!Current())return;
  auto const& result=*Current();
  size_t taps=t39ui::SungTapCount(result.lanes[manual_lane].capture);
  size_t used_taps=0,used_morae=0;
  for(auto group:manual_groups){used_taps+=group.first;used_morae+=group.second;}
  manual_timing_count=std::max(1,std::min(manual_timing_count,int(taps-used_taps)));
  manual_mora_count=std::max(1,std::min(manual_mora_count,int(result.target.analysis.morae.size()-used_morae)));
  std::string selected_reading;
  for(size_t i=used_morae;i<std::min(result.target.analysis.morae.size(),used_morae+manual_mora_count);++i)
   selected_reading+=result.target.analysis.morae[i].text;
  std::string label=std::to_string(used_taps)+" / "+std::to_string(taps)+" attacks linked; "+
   std::to_string(used_morae)+" / "+std::to_string(result.target.analysis.morae.size())+" morae linked\n"+
   "Next: "+std::to_string(manual_timing_count)+" attack(s) → ["+selected_reading+"] ("+
   std::to_string(manual_mora_count)+" morae)\n"+
   "←/→ attacks   ↑/↓ morae   Enter link   Backspace undo";
  manual_progress->SetLabel(to_wx(label));manual_progress->Wrap(std::max(250,review_scroll->GetClientSize().x-25));
  if(review_view){
   std::vector<t39::TimingAssignment> draft;
   std::vector<size_t> next_blocks;
   size_t next_mora=0,sung=0;
   auto const& capture=result.lanes[manual_lane].capture.blocks;
   std::vector<size_t> sung_indices; sung_indices.reserve(taps);
   for(size_t i=0;i<capture.size();++i)if(!capture[i].gap)sung_indices.push_back(i);
   for(auto group:manual_groups){
    for(int i=0;i<group.first;++i)draft.push_back({sung_indices[sung++],next_mora,size_t(group.second)});
    next_mora+=group.second;
   }
   for(int i=0;i<manual_timing_count&&sung+i<sung_indices.size();++i)
    next_blocks.push_back(sung_indices[sung+i]);
   review_view->SetDraft(std::move(draft),std::move(next_blocks));
  }
  manual_pane->Layout();
 }
 void LinkManual() {
  auto r=Current();if(!manual_mode||!r)return;
  auto const& capture=r->lanes[manual_lane].capture;
  size_t taps=t39ui::SungTapCount(capture),morae=r->target.analysis.morae.size();
  size_t used_taps=0,used_morae=0;
  for(auto group:manual_groups){used_taps+=group.first;used_morae+=group.second;}
  if(!taps||!morae||used_taps+manual_timing_count>taps||used_morae+manual_mora_count>morae)return;
  manual_groups.emplace_back(manual_timing_count,manual_mora_count);
  used_taps+=manual_timing_count;used_morae+=manual_mora_count;
  if(used_taps==taps&&used_morae==morae){
   std::vector<t39::TimingAssignment> mapping;mapping.reserve(taps);
   size_t block=0,first_mora=0;
   for(auto group:manual_groups){
    for(int i=0;i<group.first;++i){
     while(block<capture.blocks.size()&&capture.blocks[block].gap)++block;
     if(block>=capture.blocks.size())break;
     mapping.push_back({block++,first_mora,size_t(group.second)});
    }
    first_mora+=group.second;
   }
   if(session.SetManualAssignment(selected,manual_lane,std::move(mapping))){
   notice="Mora placement resolved; raw capture unchanged.";CloseManual();Update();
   }else {manual_groups.pop_back();notice="Manual mapping could not be completed; check the remaining groups.";UpdateManualProgress();}
   return;
  }
  if(used_taps==taps||used_morae==morae){manual_groups.pop_back();notice="Both attacks and morae must finish together.";}
  manual_timing_count=manual_mora_count=1;UpdateManualProgress();
 }
 void UndoManual(){if(!manual_groups.empty())manual_groups.pop_back();UpdateManualProgress();}
  std::string Group(t39::Analysis const& a,t39::TimingAssignment const& x) const {
  std::string out;for(size_t i=0;i<x.mora_count;++i)out+=a.morae[x.first_mora+i].text;return out;
  }
  static std::string PartitionName(t39::PartitionedCapture const& capture) {
   if(capture.status==t39::PartitionStatus::Invalid)return "invalid sung block";
   if(capture.status==t39::PartitionStatus::NeedsReview)return "equal-overlap owner tie; review needed";
   if(capture.status==t39::PartitionStatus::HarmlessClamp)return "harmless checkpoint clamp";
   return "clean";
  }
  std::string InspectorSummary(t39::SessionResult const& r,t39::SessionLaneResult const& l) const {
   std::string text=full_start.Describe();
   text+="Primary session segments: "+std::to_string(session.Raw(0).size())+"\n";
   text+="Secondary session segments: "+std::to_string(session.Raw(1).size())+"\n";
   text+="Local segments: "+std::to_string(l.capture.blocks.size())+"\n";
   text+="Local sung taps: "+std::to_string(t39ui::SungTapCount(l.capture))+"\n";
   text+="Previous-line sung tails excluded: "+std::to_string(l.capture.preceding_sung_tails)+"\n";
  text+="Partition: "+PartitionName(l.capture)+"\n\n";
   text+="Timing correction: "+std::to_string(session.TimingCorrection())+" ms (negative earlier; raw unchanged)\n";
   text+="Resolution source: "+std::string(r.resolution==t39::ResolutionSource::UserEnglishSegmentation?"user English segmentation":
    r.resolution==t39::ResolutionSource::UserManualRepair?"user mora placement":
    r.resolution==t39::ResolutionSource::UserManualTiming?"user manual timing":
    r.resolution==t39::ResolutionSource::UserSelected?"user selected":
    r.resolution==t39::ResolutionSource::Retake?"retake":"automatic")+"\n\n";
   text+=t39ui::CompactResultReason(r,selected_lane)+"\n\nMORAE\n";
   for(size_t i=0;i<r.target.analysis.morae.size();++i)text+=std::to_string(i)+" ["+r.target.analysis.morae[i].text+"]\n";
   text+="\nLOCAL BLOCKS\n";
   for(size_t i=0;i<l.capture.blocks.size();++i){auto const& b=l.capture.blocks[i];text+=std::to_string(i)+" "+std::to_string(b.start)+".."+std::to_string(b.end)+(b.gap?" gap\n":" sung\n");}
   text+="\nRELEVANT CANDIDATES\n";
   bool any=false;
   for(auto const& edges:r.target.analysis.graph)for(auto const& edge:edges)if(edge.count>1){
    any=true;std::string group;for(size_t i=0;i<edge.count;++i)group+=r.target.analysis.morae[edge.first+i].text;
    text+="["+group+"] "+(edge.features.strength==t39::CandidateStrength::Soft?"SOFT ":"STRONG ")+edge.reason+"\n";
   }
   if(!any)text+="(none)\n";
   text+="\nLOCAL RAW INDICES ";for(auto index:l.capture.raw_indices)text+=std::to_string(index)+" ";text+="\n";
   auto const& raw=session.RawForResult(size_t(&r-session.Results().data()),selected_lane);
   auto const& ownership=l.timing_override_active?l.automatic_capture:l.capture;
   text+="\nLOCAL OWNERSHIP\n";
   for(auto index:ownership.raw_indices)if(index<raw.size()&&!raw[index].gap){
    auto shifted=t39::ShiftCapture({raw[index]},session.TimingCorrection())[0];
    auto inside=t39::SungOverlapDuration(shifted,r.target.start,r.target.end);
    auto duration=int64_t(shifted.end)-shifted.start;
    text+=std::to_string(index)+" "+std::to_string(raw[index].start)+".."+std::to_string(raw[index].end)+
     " -> "+std::to_string(shifted.start)+".."+std::to_string(shifted.end)+
     " overlap "+std::to_string(inside)+"/"+std::to_string(duration)+" ms ("+
     std::to_string(duration>0?int(inside*100/duration):0)+"%), current-owned; local "+
     std::to_string(std::max(shifted.start,r.target.start))+".."+
     std::to_string(std::min(shifted.end,r.target.end))+
     (std::binary_search(ownership.disputed_raw_indices.begin(),
      ownership.disputed_raw_indices.end(),index)?" [owner tie]":"")+"\n";
   }
   auto first=std::lower_bound(raw.begin(),raw.end(),r.target.start-session.TimingCorrection(),
    [](t39::TimingBlock const& b,int t){return b.end<t;});
   for(auto at=first;at!=raw.end()&&at->start<r.target.end-session.TimingCorrection();++at){
    auto index=size_t(at-raw.begin());
    if(at->gap||std::binary_search(ownership.raw_indices.begin(),ownership.raw_indices.end(),index))continue;
    auto shifted=t39::ShiftCapture({*at},session.TimingCorrection())[0];
    auto inside=t39::SungOverlapDuration(shifted,r.target.start,r.target.end);
    if(!inside)continue;
    auto duration=int64_t(shifted.end)-shifted.start;
    text+=std::to_string(index)+" "+std::to_string(at->start)+".."+std::to_string(at->end)+
     " overlap "+std::to_string(inside)+"/"+std::to_string(duration)+" ms ("+
     std::to_string(duration>0?int(inside*100/duration):0)+"%), excluded from this line\n";
   }
   if(show_full_session_raw) {
    text+="\nSESSION RAW F/J\n";for(auto const& b:session.Raw(0))text+=std::to_string(b.start)+" "+std::to_string(b.end)+(b.gap?" gap\n":" sung\n");
    text+="SESSION RAW D/K\n";for(auto const& b:session.Raw(1))text+=std::to_string(b.start)+" "+std::to_string(b.end)+(b.gap?" gap\n":" sung\n");
    for(auto const& take:session.retakes){text+="RETAKE target="+std::to_string(take.target)+" lane="+std::to_string(take.lane)+"\n";for(auto const& b:take.raw)text+=std::to_string(b.start)+" "+std::to_string(b.end)+(b.gap?" gap\n":" sung\n");}
   }
   return text;
  }
  void UpdateInspector() {
  AudioPerf::Scope timer(AudioPerf::ResultsInspector);
  auto r=Current();if(!r)return;auto& l=r->lanes[selected_lane];
  reading->ChangeValue(to_wx(r->target.analysis.surface));
  int divider=assignments->GetSelection();assignments->Clear();paths->Clear();
  for(auto const& x:l.Assignments()) {
   auto const& b=l.capture.blocks[x.timing_block];
   assignments->Append(to_wx(std::to_string(b.start)+"–"+std::to_string(b.end)+"  ["+Group(r->target.analysis,x)+"]"));
  }
  if(assignments->GetCount())assignments->SetSelection(std::max(0,std::min(divider,int(assignments->GetCount()-1))));
  for(size_t i=0;i<l.match.paths.size();++i) {
   std::string text;for(auto const& x:l.match.paths[i].assignments)text+="["+Group(r->target.analysis,x)+"]";
   paths->Append(to_wx(text));if(l.editor.Get()==l.match.paths[i].assignments)paths->SetSelection(int(i));
  }
   if(raw_toggle)raw_toggle->SetLabel(show_full_session_raw ? _("Hide full session raw capture") : _("Show full session raw capture"));
   details->ChangeValue(to_wx(InspectorSummary(*r,l)));
 }
 void Button(wxWindow* parent,wxSizer* s,wxString const& label,std::function<void()> fn) {
  auto b=new wxButton(parent,wxID_ANY,label);s->Add(b,0,wxALL,3);b->Bind(wxEVT_BUTTON,[fn](wxCommandEvent&){fn();});
 }
 void ApplyTimingCorrection() {
  AudioPerf::Scope timer(AudioPerf::ResultsCorrection);
  if(!offset_spin)return;
  if(!MayLeaveTimingDraft()||!MayLeavePlacement()){
   offset_spin->SetValue(session.TimingCorrection());UpdateOffsetDirection();return;
  }
  int offset=offset_spin->GetValue();
  if(!session.SetTimingCorrection(offset))notice="Timing correction is available after capture, before committing lines.";
  else notice.clear();
  Update(true);
 }
 void UpdateOffsetDirection() {
  if(!offset_direction||!offset_spin)return;
  int value=offset_spin->GetValue();
  offset_direction->SetLabel(to_wx(value<0?"Move taps earlier "+std::to_string(-value)+" ms":
   value>0?"Move taps later "+std::to_string(value)+" ms":"No timing correction"));
 }
 void Navigate(int delta,bool problems_only) {
  if(session.Results().empty())return;
  size_t count=session.Results().size();
  for(size_t step=1;step<=count;++step) {
   size_t candidate=size_t((int(selected)+int(count)+delta*int(step)%int(count))%int(count));
   if(!problems_only||session.Results()[candidate].GetConfidence()!=t39::Confidence::Green) {
    if(!MayLeaveTimingDraft()||!MayLeavePlacement())return;
    selected=candidate;selected_lane=std::max(0,session.Results()[selected].lane);
    if(review_scroll)review_scroll->Scroll(0,0);
    notice.clear();Update();return;
   }
  }
 }
 void MakePanel() {
  panel=new wxDialog(c->parent,wxID_ANY,_("39 Mode — session results"),wxDefaultPosition,wxSize(1100,650),wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER);
  auto root=new wxBoxSizer(wxVERTICAL);
  status=new wxStaticText(panel,wxID_ANY,"");root->Add(status,0,wxEXPAND|wxLEFT|wxRIGHT|wxTOP,8);
  auto splitter=new wxSplitterWindow(panel,wxID_ANY,wxDefaultPosition,wxDefaultSize,wxSP_LIVE_UPDATE);
  rows=new Timing39ResultsList(splitter);
  correction_pane=new wxPanel(splitter);
  auto fixed_and_scroll=new wxBoxSizer(wxVERTICAL);
  auto right=new wxBoxSizer(wxVERTICAL);
  fixed_and_scroll->Add(new wxStaticText(correction_pane,wxID_ANY,_("Session timing correction")),0,wxLEFT|wxRIGHT|wxTOP,8);
  auto timing_row=new wxBoxSizer(wxHORIZONTAL);
  offset_spin=new wxSpinCtrl(correction_pane,wxID_ANY,"",wxDefaultPosition,wxSize(105,-1),wxSP_ARROW_KEYS|wxTE_PROCESS_ENTER,-2000,2000,0);
  timing_row->Add(offset_spin,0,wxALL,5);
  offset_direction=new wxStaticText(correction_pane,wxID_ANY,_("No timing correction"));
  timing_row->Add(offset_direction,1,wxALIGN_CENTER_VERTICAL|wxALL,5);
  fixed_and_scroll->Add(timing_row,0,wxEXPAND|wxLEFT|wxRIGHT,5);
  fixed_and_scroll->Add(new wxStaticText(correction_pane,wxID_ANY,_("Negative = earlier; positive = later. Raw capture is unchanged.")),0,wxLEFT|wxRIGHT|wxBOTTOM,8);
  review_scroll=new wxScrolledWindow(correction_pane,wxID_ANY,wxDefaultPosition,wxDefaultSize,wxVSCROLL|wxTAB_TRAVERSAL);
  review_scroll->SetScrollRate(0,12);
  line_title=new wxStaticText(review_scroll,wxID_ANY,"");right->Add(line_title,0,wxEXPAND|wxALL,8);
  lane_choice=new wxChoice(review_scroll,wxID_ANY);lane_choice->Append(_("Unresolved lane"));lane_choice->Append(_("F/J primary"));lane_choice->Append(_("D/K secondary"));right->Add(lane_choice,0,wxLEFT|wxRIGHT|wxBOTTOM,8);
  reason=new wxStaticText(review_scroll,wxID_ANY,"");right->Add(reason,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,8);
  right->Add(new wxStaticText(review_scroll,wxID_ANY,_("Possible grouping (39 Mode candidates)")),0,wxLEFT|wxRIGHT|wxTOP,8);
  candidate_guidance=new wxStaticText(review_scroll,wxID_ANY,"");
  right->Add(candidate_guidance,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,8);
  choice_sizer=new wxBoxSizer(wxVERTICAL);right->Add(choice_sizer,0,wxEXPAND|wxLEFT|wxRIGHT,6);
  resolved_label=new wxStaticText(review_scroll,wxID_ANY,_("Assignment resolved — ready to commit"));
  choice_sizer->Add(resolved_label,0,wxEXPAND|wxALL,4);
  review_view=new Timing39ReviewView(review_scroll,c->audioBox);
  right->Add(review_view,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,8);
  auto playback=new wxBoxSizer(wxHORIZONTAL);
  Button(review_scroll,playback,_("Play line"),[this]{
   if(!Current()||!review_view)return;
   auto range=review_view->PlaybackRange(false);
   c->audioController->PlayRange(TimeRange(range.first,range.second));
  });
  Button(review_scroll,playback,_("Play context"),[this]{
   if(!Current()||!review_view)return;
   auto range=review_view->PlaybackRange(true);
   c->audioController->PlayRange(TimeRange(range.first,range.second));
  });
  Button(review_scroll,playback,_("Stop"),[this]{c->audioController->Stop();if(review_view)review_view->SetPlaybackCursor(-1);});
  right->Add(playback,0,wxLEFT|wxRIGHT|wxBOTTOM,5);
  right->Add(new wxStaticText(review_scroll,wxID_ANY,_("Will commit — exact serialized blocks (select the right SUNG block to edit its left boundary)")),
   0,wxLEFT|wxRIGHT|wxTOP,8);
  commit_blocks=new wxListBox(review_scroll,wxID_ANY,wxDefaultPosition,wxSize(-1,150));
  commit_blocks->SetFont(wxFontInfo(9).Family(wxFONTFAMILY_TELETYPE));
  right->Add(commit_blocks,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,8);
  right->Add(new wxStaticText(review_scroll,wxID_ANY,_("Full commit preview — literal ASS/Mangetsu text")),0,wxLEFT|wxRIGHT,8);
  commit_preview=new wxTextCtrl(review_scroll,wxID_ANY,"",wxDefaultPosition,wxSize(-1,85),
   wxTE_MULTILINE|wxTE_READONLY|wxTE_DONTWRAP);
  commit_preview->SetFont(wxFontInfo(9).Family(wxFONTFAMILY_TELETYPE));
  right->Add(commit_preview,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,8);
  commit_blocks->Bind(wxEVT_LISTBOX,[this](wxCommandEvent&){
   int row=commit_blocks->GetSelection();
   if(row>=0)commit_blocks->SetToolTip(commit_blocks->GetString(size_t(row)));
  });
  placement_pane=new wxPanel(review_scroll);
  auto placement_sizer=new wxBoxSizer(wxVERTICAL);
  placement_hint=new wxStaticText(placement_pane,wxID_ANY,
   _("Select a SUNG block above. These are draft text assignments, not timestamp edits."));
  placement_sizer->Add(placement_hint,0,wxEXPAND|wxALL,4);
  auto placement_actions=new wxBoxSizer(wxHORIZONTAL);
  Button(placement_pane,placement_actions,_("Move boundary ←"),[this]{MovePlacement(-1);});
  Button(placement_pane,placement_actions,_("Move boundary →"),[this]{MovePlacement(1);});
  Button(placement_pane,placement_actions,_("Merge neighboring"),[this]{MergePlacement();});
  placement_sizer->Add(placement_actions,0,wxEXPAND);
  auto latin_actions=new wxBoxSizer(wxHORIZONTAL);
  latin_words=new wxChoice(placement_pane,wxID_ANY);latin_actions->Add(latin_words,1,wxALL,3);
  latin_cut=new wxSpinCtrl(placement_pane,wxID_ANY,"",wxDefaultPosition,wxSize(68,-1),wxSP_ARROW_KEYS,1,100,1);
  latin_actions->Add(latin_cut,0,wxALL,3);
  Button(placement_pane,latin_actions,_("Cut word"),[this]{CutLatin(false);});
  Button(placement_pane,latin_actions,_("Remove cut before"),[this]{CutLatin(true);});
  placement_sizer->Add(latin_actions,0,wxEXPAND);
  auto placement_finish=new wxBoxSizer(wxHORIZONTAL);
  Button(placement_pane,placement_finish,_("Undo"),[this]{if(placement_draft.Undo())UpdatePlacementDraft();});
  Button(placement_pane,placement_finish,_("Redo"),[this]{if(placement_draft.Redo())UpdatePlacementDraft();});
  Button(placement_pane,placement_finish,_("Reset draft"),[this]{if(placement_draft.Reset())UpdatePlacementDraft();});
  Button(placement_pane,placement_finish,_("Apply placement"),[this]{ApplyPlacement();});
  Button(placement_pane,placement_finish,_("Cancel"),[this]{ClosePlacement();Update();});
  placement_sizer->Add(placement_finish,0,wxEXPAND);
  placement_pane->SetSizer(placement_sizer);right->Add(placement_pane,0,wxEXPAND|wxALL,6);
  placement_pane->Hide();
  manual_pane=new wxPanel(review_scroll);
  auto manual_sizer=new wxBoxSizer(wxVERTICAL);
  manual_progress=new wxStaticText(manual_pane,wxID_ANY,"");
  manual_sizer->Add(manual_progress,0,wxEXPAND|wxALL,4);
  auto manual_actions=new wxBoxSizer(wxHORIZONTAL);
  Button(manual_pane,manual_actions,_("Link next"),[this]{LinkManual();});
  Button(manual_pane,manual_actions,_("Undo link"),[this]{UndoManual();});
  Button(manual_pane,manual_actions,_("Cancel"),[this]{CloseManual();});
  manual_sizer->Add(manual_actions,0,wxEXPAND);
  manual_pane->SetSizer(manual_sizer);right->Add(manual_pane,0,wxEXPAND|wxALL,6);
  manual_pane->Hide();
  timing_pane=new wxPanel(review_scroll);
  auto timing_sizer=new wxBoxSizer(wxVERTICAL);
  timing_sizer->Add(new wxStaticText(timing_pane,wxID_ANY,
   _("Toshiki draft: click audio to split; drag purple edges to move; right-click between blocks to join.")),
   0,wxEXPAND|wxALL,4);
  auto timing_buttons=new wxBoxSizer(wxHORIZONTAL);
  Button(timing_pane,timing_buttons,_("Apply timing"),[this]{ApplyTimingDraft();});
  Button(timing_pane,timing_buttons,_("Cancel"),[this]{CloseTimingDraft();});
  Button(timing_pane,timing_buttons,_("Undo"),[this]{if(timing_draft.Undo())review_view->Refresh(false);});
  Button(timing_pane,timing_buttons,_("Redo"),[this]{if(timing_draft.Redo())review_view->Refresh(false);});
  timing_sizer->Add(timing_buttons,0,wxEXPAND);
  timing_pane->SetSizer(timing_sizer);right->Add(timing_pane,0,wxEXPAND|wxALL,6);
  timing_pane->Hide();
  auto actions=new wxBoxSizer(wxHORIZONTAL);
  Button(review_scroll,actions,_("Retake"),[this]{Retake();});
  Button(review_scroll,actions,_("Fix mora placement"),[this]{StartPlacement();});
  Button(review_scroll,actions,_("Manual timing"),[this]{StartTimingDraft();});
  right->Add(actions,0,wxLEFT|wxRIGHT|wxBOTTOM,5);
  auto reset_actions=new wxBoxSizer(wxHORIZONTAL);
  reset_manual=new wxButton(review_scroll,wxID_ANY,_("Reset mora placement"));
  reset_actions->Add(reset_manual,0,wxALL,3);
  reset_manual->Bind(wxEVT_BUTTON,[this](wxCommandEvent&){
   if(placement_mode)ClosePlacement();
   if(session.ResetManualAssignment(selected,selected_lane)){notice="Automatic assignment restored; raw capture unchanged.";CloseManual();Update();}
  });
  reset_timing=new wxButton(review_scroll,wxID_ANY,_("Reset manual timing"));
  reset_actions->Add(reset_timing,0,wxALL,3);
  reset_timing->Bind(wxEVT_BUTTON,[this](wxCommandEvent&){
   if(session.ResetManualTiming(selected,selected_lane)){notice="Raw-derived timing restored.";CloseTimingDraft();Update();}
  });
  Button(review_scroll,reset_actions,_("Advanced / Inspector"),[this]{ShowInspector();});
  right->Add(reset_actions,0,wxLEFT|wxRIGHT|wxBOTTOM,5);
  auto navigation=new wxBoxSizer(wxHORIZONTAL);
  Button(review_scroll,navigation,_("Previous problem"),[this]{Navigate(-1,true);});
  Button(review_scroll,navigation,_("Next problem"),[this]{Navigate(1,true);});
  right->Add(navigation,0,wxLEFT|wxRIGHT|wxBOTTOM,5);
  review_scroll->SetSizer(right);
  fixed_and_scroll->Add(review_scroll,1,wxEXPAND);
  correction_pane->SetSizer(fixed_and_scroll);
  splitter->SetMinimumPaneSize(260);splitter->SplitVertically(rows,correction_pane,520);
  root->Add(splitter,1,wxEXPAND|wxALL,5);
  auto commit=new wxBoxSizer(wxHORIZONTAL);Button(panel,commit,_("Commit all GREEN"),[this]{CommitResults();});root->Add(commit,0,wxALL,5);
  panel->SetSizer(root);
  rows->Bind(wxEVT_LISTBOX,[this](wxCommandEvent&){
   AudioPerf::Scope timer(AudioPerf::ResultsRowSelect);
   if(!MayLeaveTimingDraft()||!MayLeavePlacement()){rows->SetSelection(int(selected));return;}
   selected=size_t(rows->GetSelection());if(auto r=Current())selected_lane=std::max(0,r->lane);
   if(review_scroll)review_scroll->Scroll(0,0);notice.clear();Update();
  });
  lane_choice->Bind(wxEVT_CHOICE,[this](wxCommandEvent&){
   AudioPerf::Scope timer(AudioPerf::ResultsLaneSwitch);
   if(refreshing)return;
   if(!MayLeaveTimingDraft()||!MayLeavePlacement()){if(auto r=Current())lane_choice->SetSelection(r->lane+1);return;}
   if(auto r=Current()){r->lane=lane_choice->GetSelection()-1;selected_lane=std::max(0,r->lane);r->reviewed=r->lane>=0;notice.clear();Update();}
  });
  correction_timer.Bind(wxEVT_TIMER,[this](wxTimerEvent&){ApplyTimingCorrection();});
  offset_spin->Bind(wxEVT_SPINCTRL,[this](wxSpinEvent&){if(refreshing)return;UpdateOffsetDirection();correction_timer.Start(140,true);});
  offset_spin->Bind(wxEVT_TEXT_ENTER,[this](wxCommandEvent&){correction_timer.Stop();UpdateOffsetDirection();ApplyTimingCorrection();});
  panel->Bind(wxEVT_CHAR_HOOK,[this](wxKeyEvent& e){
   int key=e.GetKeyCode();
   if(placement_mode){
    if(key==WXK_ESCAPE){ClosePlacement();Update();return;}
    if(key=='Z'&&e.ControlDown()){if(placement_draft.Undo())UpdatePlacementDraft();return;}
    if(key=='Y'&&e.ControlDown()){if(placement_draft.Redo())UpdatePlacementDraft();return;}
    if(key==WXK_LEFT&&e.AltDown()){MovePlacement(-1);return;}
    if(key==WXK_RIGHT&&e.AltDown()){MovePlacement(1);return;}
   }
   if(key==WXK_UP||key==WXK_DOWN)
    for(auto focus=wxWindow::FindFocus();focus;focus=focus->GetParent())
     if(focus==offset_spin){e.Skip();return;}
   if(manual_mode){
    if(key==WXK_LEFT){manual_timing_count=std::max(1,manual_timing_count-1);UpdateManualProgress();return;}
    if(key==WXK_RIGHT){++manual_timing_count;UpdateManualProgress();return;}
    if(key==WXK_DOWN){manual_mora_count=std::max(1,manual_mora_count-1);UpdateManualProgress();return;}
    if(key==WXK_UP){++manual_mora_count;UpdateManualProgress();return;}
    if(key==WXK_RETURN){LinkManual();return;}
    if(key==WXK_BACK){UndoManual();return;}
    if(key==WXK_ESCAPE){CloseManual();return;}
   }
   if(timing_mode){
    if(key==WXK_ESCAPE){CloseTimingDraft();return;}
    if(key==WXK_RETURN){ApplyTimingDraft();return;}
    if(key=='Z'&&e.ControlDown()){if(timing_draft.Undo())review_view->Refresh(false);return;}
    if(key=='Y'&&e.ControlDown()){if(timing_draft.Redo())review_view->Refresh(false);return;}
   }
   if(key==WXK_ESCAPE){if(MayLeaveTimingDraft()&&MayLeavePlacement())panel->Hide();return;}
   if(key=='R'&&!e.ControlDown()&&!e.AltDown()){Retake();return;}
   if((key==WXK_UP||key==WXK_DOWN)&&!e.AltDown()){
    Navigate(key==WXK_DOWN?1:-1,e.ControlDown());return;
   }
   if(key==WXK_RETURN){
    auto focus=wxWindow::FindFocus();
    for(size_t i=0;i<candidate_buttons.size();++i)if(focus==candidate_buttons[i]){
     if(MayLeavePlacement()&&session.ChooseAssignment(selected,selected_lane,i)){
      notice.clear();panel->CallAfter([this]{Update();});}return;
    }
   }
   e.Skip();
  });
  panel->Bind(wxEVT_CLOSE_WINDOW,[this](wxCloseEvent& e){
   if(e.CanVeto()){
    e.Veto();if(MayLeaveTimingDraft()&&MayLeavePlacement())panel->Hide();
   }else e.Skip();
  });
 }
 void ShowInspector() {
  if(!Current())return;
  if(!inspector) {
   inspector=new wxDialog(panel,wxID_ANY,_("39 Mode — selected line Inspector"),wxDefaultPosition,wxSize(800,650),wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER);
   auto root=new wxBoxSizer(wxVERTICAL);reading=new wxTextCtrl(inspector,wxID_ANY);root->Add(reading,0,wxEXPAND|wxALL,6);
   Button(inspector,root,_("Apply explicit reading"),[this]{auto r=Current();if(!r||!MayLeavePlacement())return;auto a=t39::Analyze(from_wx(reading->GetValue()));if(!a.error.empty()){notice=a.error;Update();return;}a.source=r->target.analysis.source;r->target.analysis=std::move(a);r->original_analysis.reset();session.Rematch(selected,0);session.Rematch(selected,1);rows->RebuildRow(selected,r->target.analysis,r->GetConfidence());Update();});
    root->Add(new wxStaticText(inspector,wxID_ANY,_("Advanced candidate paths (full line)")),0,wxLEFT|wxRIGHT|wxTOP,6);
    paths=new wxChoice(inspector,wxID_ANY);root->Add(paths,0,wxEXPAND|wxALL,6);
   assignments=new wxListBox(inspector,wxID_ANY);root->Add(assignments,1,wxEXPAND|wxALL,6);
   auto edit=new wxBoxSizer(wxHORIZONTAL);Button(inspector,edit,_("← Mora"),[this]{Move(-1);});Button(inspector,edit,_("Mora →"),[this]{Move(1);});Button(inspector,edit,_("Undo"),[this]{History(false);});Button(inspector,edit,_("Redo"),[this]{History(true);});root->Add(edit,0);
    details=new wxTextCtrl(inspector,wxID_ANY,"",wxDefaultPosition,wxSize(-1,240),wxTE_MULTILINE|wxTE_READONLY|wxTE_DONTWRAP);root->Add(details,1,wxEXPAND|wxALL,6);
    raw_toggle=new wxButton(inspector,wxID_ANY,_("Show full session raw capture"));root->Add(raw_toggle,0,wxLEFT|wxRIGHT|wxBOTTOM,6);
    raw_toggle->Bind(wxEVT_BUTTON,[this](wxCommandEvent&){show_full_session_raw=!show_full_session_raw;UpdateInspector();});
   Button(inspector,root,_("Save inspection"),[this]{wxFileDialog save(inspector,_("Save inspection"),"","39-mode.txt",_("Text files (*.txt)|*.txt"),wxFD_SAVE|wxFD_OVERWRITE_PROMPT);if(save.ShowModal()==wxID_OK){wxFFile f(save.GetPath(),"w");if(f.IsOpened())f.Write(details->GetValue(),wxConvUTF8);}});
   paths->Bind(wxEVT_CHOICE,[this](wxCommandEvent&){int p=paths->GetSelection();if(p<0||!MayLeavePlacement())return;if(session.ChooseAssignment(selected,selected_lane,size_t(p))){notice.clear();Update();}});
   assignments->Bind(wxEVT_KEY_DOWN,[this](wxKeyEvent& e){if(e.GetKeyCode()==WXK_LEFT)Move(-1);else if(e.GetKeyCode()==WXK_RIGHT)Move(1);else e.Skip();});
   inspector->SetSizer(root);inspector->Bind(wxEVT_CLOSE_WINDOW,[this](wxCloseEvent& e){if(e.CanVeto()){e.Veto();inspector->Hide();}else e.Skip();});
  }
  UpdateInspector();inspector->Show();inspector->Raise();
 }
 void Move(int delta){if(auto r=Current()){if(!MayLeavePlacement())return;if(!r->lanes[selected_lane].editor.Move(r->target.analysis,size_t(std::max(0,assignments->GetSelection())),delta))notice="Protected divider; choose a complete alternative path.";else {notice="Assignment moved; raw timestamps unchanged";r->resolution=t39::ResolutionSource::UserSelected;r->reviewed=true;}Update();}}
 void History(bool redo){if(auto r=Current()){if(!MayLeavePlacement())return;auto& e=r->lanes[selected_lane].editor;if(redo)e.Redo();else e.Undo();r->resolution=t39::ResolutionSource::UserSelected;r->reviewed=true;Update();}}
 void Retake(){
  auto current=Current();
  if(!current||current->committed||session.State()!=t39::SessionState::Results)return;
  if(!MayLeaveTimingDraft()||!MayLeavePlacement())return;
  if(correction_timer.IsRunning()){correction_timer.Stop();ApplyTimingCorrection();}
  c->videoController->Stop();c->audioController->Stop();
  if(session.Retake(selected,selected_lane,Preroll())){
   notice.clear();
   panel->Hide();if(inspector)inspector->Hide();BeginCountdown();
   c->audioBox->FocusAudio();AnnounceUpdatedPrimaryRange();Notify();
  }
 }
 void CommitResults() {
  if(Is39SessionActive())return;
  if(placement_mode){notice="Apply or cancel the mora placement draft before committing.";Update();return;}
  struct Write{t39::SessionResult* result;AssDialogue* event;std::string text;};std::vector<Write> writes;
  for(auto& r:session.Results()) {
   if(r.committed||r.lane<0)continue;
   auto confidence=r.GetConfidence();
   if(confidence!=t39::Confidence::Green)continue;
   auto it=events.find(r.target.id);if(it==events.end()||it->second->Text.get()!=r.target.analysis.source){notice="Source changed; restart session before committing";Update();return;}
   auto& l=r.lanes[r.lane];std::string output,error;
   if(!t39::Serialize(*it->second,r.target.analysis,l.capture.blocks,l.Assignments(),output,error,
    l.manual_active)){notice=error;Update();return;}
   writes.push_back({&r,it->second,std::move(output)});
  }
  if(writes.empty()){notice="No eligible lines to commit";Update();return;}
  committing=true;
  for(auto& w:writes){w.event->Text=w.text;auto& l=w.result->lanes[w.result->lane];if(!l.editor.corrections.empty())c->ass->SetExtradataValue(*w.event,"39-mode-correction",t39::Inspect(w.result->target.analysis,l.capture.blocks,l.match,&l.editor));}
  c->ass->Commit(_("39 Mode session timing"),AssFile::COMMIT_DIAG_TEXT|AssFile::COMMIT_EXTRADATA);committing=false;
  for(auto& w:writes){w.result->committed=true;w.result->target.analysis.source=w.text;}
  notice=std::to_string(writes.size())+" lines committed in one subtitle undo step";Update();Notify();
 }
public:
 explicit AudioTimingController39(agi::Context* context):c(context) {
  countdown.Bind(wxEVT_TIMER,&AudioTimingController39::Tick,this);Prepare();wxEvtHandler::AddFilter(this);
  connections.push_back(c->ass->AddCommitListener(&AudioTimingController39::FileChanged,this));
  connections.push_back(c->audioController->AddPlaybackPositionListener([this](int ms){
   // Keep the start anchor for this playback. Re-anchoring after a UI stall
   // could place an older queued key before the new anchor and lose its time.
   if(session.State()==t39::SessionState::Capturing && c->audioController->IsPlaying() &&
      !input_clock.IsAnchored())
    input_clock.Anchor(t39::InputClockNow(),ms);
   else if(session.State()!=t39::SessionState::Capturing)input_clock.Reset();
   last_position=ms;session.Advance(ms);
   if(review_view&&panel&&panel->IsShown())review_view->SetPlaybackCursor(ms);
  }));
  connections.push_back(c->audioController->AddPlaybackStopListener([this]{
   if(review_view)review_view->SetPlaybackCursor(-1);
  }));
 }
 ~AudioTimingController39() override {HideCountdown();correction_timer.Stop();session.Discard();physical_held.fill(false);wxEvtHandler::RemoveFilter(this);connections.clear();delete inspector;delete panel;}
 int FilterEvent(wxEvent& event) override {
  if(Is39SessionActive()&&event.GetEventType()==wxEVT_ACTIVATE_APP&&!static_cast<wxActivateEvent&>(event).GetActive()){physical_held.fill(false);c->audioController->Stop();return Event_Skip;}
  if(event.GetEventType()!=wxEVT_CHAR_HOOK&&event.GetEventType()!=wxEVT_KEY_DOWN&&event.GetEventType()!=wxEVT_KEY_UP)return Event_Skip;
  auto window=dynamic_cast<wxWindow*>(event.GetEventObject());
  while(window&&window!=c->parent)window=window->GetParent();if(!window)return Event_Skip;
  auto& key=static_cast<wxKeyEvent&>(event);bool down=event.GetEventType()!=wxEVT_KEY_UP;
  int code=key.GetKeyCode();
  if(down&&code==WXK_ESCAPE&&session.IsRetake()) {
   session.CancelRetake();c->audioController->Stop();HideCountdown();physical_held.fill(false);
   ShowResults();Notify();return Event_Processed;
  }
  int index=code=='F'?0:code=='J'?1:code=='D'?2:code=='K'?3:-1;
  if(index>=0&&!down)physical_held[index]=false;
  if(!Is39SessionActive())return Event_Skip;
#if wxCHECK_VERSION(3,1,0)
  if(down&&index>=0&&key.IsAutoRepeat())return Event_Processed;
#endif
  if(down&&(key.AltDown()||key.ControlDown()||key.ShiftDown()))return Event_Skip;
  if(index>=0&&down){if(physical_held[index])return Event_Processed;physical_held[index]=true;}
  int event_ms=0;
  uint32_t event_tick=0;
  bool mapped=index>=0 && t39::InputEventTick(key,event_tick) &&
   input_clock.Map(event_tick,t39::InputClockNow(),session.StartTime(),session.EndTime(),event_ms);
  if(!mapped)event_ms=c->audioController->GetPlaybackPosition();
  return TimingKey(key.GetKeyCode(),down,event_ms,false,false)?Event_Processed:Event_Skip;
 }
 bool Is39Mode() const override{return true;}
 bool Is39SessionActive() const override{return session.State()==t39::SessionState::Countdown||session.State()==t39::SessionState::Ready||session.State()==t39::SessionState::Capturing;}
 unsigned RhythmSerial() const override{return rhythm_serial;}
 wxString GetWarningMessage() const override{return Get39Status();}
 wxString Get39Status() const override {
  if(session.State()==t39::SessionState::Countdown)return _("39 Mode • preparing playback • F/J primary • D/K secondary");
  if(auto armed=session.ArmedOwner())return to_wx(std::string(1,armed)+" armed — first block begins at line start");
  if(session.State()==t39::SessionState::Capturing)return _("39 Mode • F/J primary • D/K secondary • Pause/stop to review");
  return _("39 Mode • right-click audio to reopen session results");
 }
 TimeRange GetActiveLineRange() const override{return TimeRange(session.StartTime(),session.EndTime());}
 TimeRange GetIdealVisibleTimeRange() const override{return TimeRange(session.StartTime(),std::min(session.EndTime(),session.StartTime()+5000));}
 TimeRange GetPrimaryPlaybackRange() const override{return GetActiveLineRange();}
 void GetMarkers(TimeRange const&,AudioMarkerVector&) const override{}
 void GetLabels(TimeRange const&,std::vector<AudioLabel>&) const override{}
 void GetRenderingStyles(AudioRenderingStyleRanges& ranges) const override{ranges.AddRange(session.StartTime(),session.EndTime(),AudioStyle_Selected);}
 bool IsNearbyMarker(int,int,bool) const override{return false;}
 std::vector<AudioMarker*> OnLeftClick(int,bool,bool,int,int) override{return {};}
 std::vector<AudioMarker*> OnRightClick(int,bool,int,int) override{if(!Is39SessionActive())ShowResults();return {};}
 void OnMarkerDrag(std::vector<AudioMarker*> const&,int,int) override{}
 void AddLeadIn() override{} void AddLeadOut() override{} void ModifyLength(int,bool) override{} void ModifyStart(int) override{}
 void Next(NextMode) override{if(!Is39SessionActive())c->selectionController->NextLine();}
 void Prev() override{if(!Is39SessionActive())c->selectionController->PrevLine();}
  void Revert() override{c->audioController->Stop();if(timing_mode)CloseTimingDraft();session.Discard();ordinary_boundaries.Reset({});target_boundaries.Reset({});Update();Notify();}
 void PlaybackStarting(int ms) override {
  input_clock.Reset();
  if(session.State()==t39::SessionState::Capturing)PlaybackStopped(c->audioController->GetPlaybackPosition());
  else if(session.State()==t39::SessionState::Countdown)PlaybackStopped(session.StartTime());
  if(session.Start(ms))physical_held.fill(false);
  last_position=ms;Notify();
 }
 void PlaybackStopped(int ms) override {input_clock.Reset();HideCountdown();physical_held.fill(false);if(session.Stop(ms)){c->videoController->Stop();last_position=ms;ShowResults();Notify();}}
 void TimingFocusLost(int) override{} // widget focus changes do not end the session; app deactivation does
 bool TimingKey(int key,bool down,int ms,bool control,bool shift) override {
  if(!Is39SessionActive())return false;
  if(key!='F'&&key!='J'&&key!='D'&&key!='K')return false;
  if(down&&(control||shift))return false;
  if(session.Key(key,down,ms)){if(down&&!session.ArmedOwner())++rhythm_serial;Notify();}return true;
 }
  void Get39Overlay(std::vector<Timing39Overlay>& out,int ms,TimeRange const& visible) const override {
   ordinary_boundaries.Visit(visible.begin(),visible.end(),[&](int start,int end){out.push_back({start,end,0,false,false,Timing39OverlayKind::ReferenceDialogue});});
   target_boundaries.Visit(visible.begin(),visible.end(),[&](int start,int end){out.push_back({start,end,0,false,false,Timing39OverlayKind::TargetLyric});});
   for(int lane=0;lane<2;++lane)session.VisitPreview(lane,ms,visible.begin(),visible.end(),[&](agi::timing39::TimingBlock const& b){
    out.push_back({b.start,b.end,lane,b.gap,false,Timing39OverlayKind::CapturedBlock});
   });
  }
 void Commit() override{CommitResults();}
};
}
std::unique_ptr<AudioTimingController> Create39TimingController(agi::Context* c){return agi::make_unique<AudioTimingController39>(c);}
