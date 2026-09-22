// Copyright (c) 2026, JibunSenyou contributors. ISC license.
#include "audio_timing.h"
#include "timing39_karaoke.h"
#include "timing39_session_setup.h"
#include "timing39_ui.h"
#include "frame_main.h"
#include "video_controller.h"
#include "ass_dialogue.h"
#include "ass_file.h"
#include "audio_box.h"
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
#include <algorithm>
#include <map>
#include <set>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/eventfilter.h>
#include <wx/listbox.h>
#include <wx/panel.h>
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

class AudioTimingController39 final : public AudioTimingController, public wxEventFilter {
 agi::Context* c;
 t39::Timing39Session session;
 t39::SessionPlaybackStart full_start;
 std::map<uint64_t,AssDialogue*> events;
 t39ui::TimelineIntervalIndex ordinary_boundaries,target_boundaries;
 std::vector<agi::signal::Connection> connections;
 wxTimer countdown;
 wxDialog *panel=nullptr,*inspector=nullptr;
 Timing39ResultsList *rows=nullptr;
 wxPanel *correction_pane=nullptr;
 wxBoxSizer *choice_sizer=nullptr;
 wxListBox *assignments=nullptr;
 wxChoice *lane_choice=nullptr,*paths=nullptr;
 wxStaticText *status=nullptr,*line_title=nullptr,*reason=nullptr,*offset_direction=nullptr;
 wxSpinCtrl *offset_spin=nullptr;
 wxTimer correction_timer;
 std::vector<wxButton*> candidate_buttons;
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
 void ShowResults(){if(!panel)MakePanel();Update();panel->Show();panel->Raise();}
 void FileChanged(int type,AssDialogue const*) {
  if(committing||!(type&(AssFile::COMMIT_DIAG_FULL|AssFile::COMMIT_DIAG_ADDREM)))return;
   c->audioController->Stop();countdown.Stop();session.Discard();events.clear();ordinary_boundaries.Reset({});target_boundaries.Reset({});
  notice="Subtitle edit or undo invalidated the cached targets. Toggle 39 Mode to start a new session.";
  if(inspector)inspector->Hide();Update();Notify();
 }
 void Update() {
   if(!panel)return;refreshing=true;
   if(offset_spin && !correction_timer.IsRunning() && offset_spin->GetValue()!=session.TimingCorrection())
    offset_spin->SetValue(session.TimingCorrection());
   if(offset_spin)offset_spin->Enable(std::none_of(session.Results().begin(),session.Results().end(),
    [](t39::SessionResult const& r){return r.committed;}));
   UpdateOffsetDirection();
   if(rows->RowCount()!=session.Results().size()) {
    rows->ResetRows(session.Results());
   }
   else for(size_t i=0;i<session.Results().size();++i)
    rows->SetStatus(i,session.Results()[i].GetConfidence());
   if(auto r=Current()) {
    if(rows->GetSelection()!=int(selected))rows->SetSelection(int(selected));
    lane_choice->SetSelection(r->lane+1);
    status->SetLabel(to_wx(notice));
  }else status->SetLabel(to_wx(notice.empty()?"No captured lyric targets. Toggle 39 Mode to start again.":notice));
  UpdatePane();
  if(inspector&&inspector->IsShown())UpdateInspector();
  panel->Layout();refreshing=false;
 }
 void UpdatePane() {
  if(!correction_pane)return;
  auto r=Current();
  if(!r){line_title->SetLabel("");reason->SetLabel("");choice_sizer->Clear(true);candidate_buttons.clear();correction_pane->Layout();return;}
  auto readable=t39ui::PrepareLyricDisplay(r->target.analysis).plain;
  line_title->SetLabel(to_wx(readable));
  std::string explanation=t39ui::CompactResultReason(*r,selected_lane);
  if(r->resolution==t39::ResolutionSource::UserSelected && r->GetConfidence()==t39::Confidence::Green)
   explanation="Selected assignment resolved this line.";
  if(r->manual_invalidated)explanation="Previous selected assignment no longer fits the corrected taps.\n"+explanation;
  reason->SetLabel(to_wx(explanation));
  line_title->Wrap(std::max(250,correction_pane->GetClientSize().x-24));
  reason->Wrap(std::max(250,correction_pane->GetClientSize().x-24));
  choice_sizer->Clear(true);candidate_buttons.clear();
  auto& lane=r->lanes[selected_lane];
  if(r->GetConfidence()==t39::Confidence::Yellow &&
     lane.capture.status!=t39::PartitionStatus::AmbiguousSungCrossing && !lane.match.paths.empty()) {
   for(size_t i=0;i<lane.match.paths.size();++i) {
    if(i && (lane.match.confidence!=t39::Confidence::Yellow ||
       lane.match.paths[i].cost-lane.match.paths[0].cost>=t39::ScoringModel{}.ambiguity_margin))break;
    auto label=t39ui::CompactPathChoice(r->target.analysis,lane.match,i);
    if(label.empty())label="Assignment "+std::to_string(i+1);
    auto option=new wxButton(correction_pane,wxID_ANY,to_wx(label));
    choice_sizer->Add(option,0,wxEXPAND|wxALL,4);candidate_buttons.push_back(option);
    option->Bind(wxEVT_BUTTON,[this,i](wxCommandEvent&){
     if(session.ChooseAssignment(selected,selected_lane,i)){notice.clear();panel->CallAfter([this]{Update();});}
    });
   }
  }
  else if(r->GetConfidence()==t39::Confidence::Green) {
   choice_sizer->Add(new wxStaticText(correction_pane,wxID_ANY,_("Assignment resolved — ready to commit")),0,wxEXPAND|wxALL,4);
  }
  correction_pane->Layout();
 }
  std::string Group(t39::Analysis const& a,t39::TimingAssignment const& x) const {
  std::string out;for(size_t i=0;i<x.mora_count;++i)out+=a.morae[x.first_mora+i].text;return out;
  }
  static std::string PartitionName(t39::PartitionedCapture const& capture) {
   if(capture.status==t39::PartitionStatus::AmbiguousSungCrossing)return "ambiguous sung checkpoint crossing";
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
   text+="Resolution source: "+std::string(r.resolution==t39::ResolutionSource::UserSelected?"user selected":
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
   if(show_full_session_raw) {
    text+="\nSESSION RAW F/J\n";for(auto const& b:session.Raw(0))text+=std::to_string(b.start)+" "+std::to_string(b.end)+(b.gap?" gap\n":" sung\n");
    text+="SESSION RAW D/K\n";for(auto const& b:session.Raw(1))text+=std::to_string(b.start)+" "+std::to_string(b.end)+(b.gap?" gap\n":" sung\n");
    for(auto const& take:session.retakes){text+="RETAKE target="+std::to_string(take.target)+" lane="+std::to_string(take.lane)+"\n";for(auto const& b:take.raw)text+=std::to_string(b.start)+" "+std::to_string(b.end)+(b.gap?" gap\n":" sung\n");}
   }
   return text;
  }
  void UpdateInspector() {
  auto r=Current();if(!r)return;auto& l=r->lanes[selected_lane];
  reading->ChangeValue(to_wx(r->target.analysis.surface));
  int divider=assignments->GetSelection();assignments->Clear();paths->Clear();
  for(auto const& x:l.editor.Get()) {
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
  if(!offset_spin)return;
  int offset=offset_spin->GetValue();
  if(!session.SetTimingCorrection(offset))notice="Timing correction is available after capture, before committing lines.";
  else notice.clear();
  Update();
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
    selected=candidate;selected_lane=std::max(0,session.Results()[selected].lane);notice.clear();Update();return;
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
  auto right=new wxBoxSizer(wxVERTICAL);
  right->Add(new wxStaticText(correction_pane,wxID_ANY,_("Session timing correction")),0,wxLEFT|wxRIGHT|wxTOP,8);
  auto timing_row=new wxBoxSizer(wxHORIZONTAL);
  offset_spin=new wxSpinCtrl(correction_pane,wxID_ANY,"",wxDefaultPosition,wxSize(105,-1),wxSP_ARROW_KEYS|wxTE_PROCESS_ENTER,-2000,2000,0);
  timing_row->Add(offset_spin,0,wxALL,5);
  offset_direction=new wxStaticText(correction_pane,wxID_ANY,_("No timing correction"));
  timing_row->Add(offset_direction,1,wxALIGN_CENTER_VERTICAL|wxALL,5);
  right->Add(timing_row,0,wxEXPAND|wxLEFT|wxRIGHT,5);
  right->Add(new wxStaticText(correction_pane,wxID_ANY,_("Negative = earlier; positive = later. Raw capture is unchanged.")),0,wxLEFT|wxRIGHT|wxBOTTOM,8);
  line_title=new wxStaticText(correction_pane,wxID_ANY,"");right->Add(line_title,0,wxEXPAND|wxALL,8);
  lane_choice=new wxChoice(correction_pane,wxID_ANY);lane_choice->Append(_("Unresolved lane"));lane_choice->Append(_("F/J primary"));lane_choice->Append(_("D/K secondary"));right->Add(lane_choice,0,wxLEFT|wxRIGHT|wxBOTTOM,8);
  reason=new wxStaticText(correction_pane,wxID_ANY,"");right->Add(reason,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,8);
  choice_sizer=new wxBoxSizer(wxVERTICAL);right->Add(choice_sizer,1,wxEXPAND|wxALL,6);
  auto actions=new wxBoxSizer(wxHORIZONTAL);
  Button(correction_pane,actions,_("Retake"),[this]{Retake();});
  Button(correction_pane,actions,_("Advanced / Inspector"),[this]{ShowInspector();});
  right->Add(actions,0,wxLEFT|wxRIGHT|wxBOTTOM,5);
  auto navigation=new wxBoxSizer(wxHORIZONTAL);
  Button(correction_pane,navigation,_("Previous problem"),[this]{Navigate(-1,true);});
  Button(correction_pane,navigation,_("Next problem"),[this]{Navigate(1,true);});
  right->Add(navigation,0,wxLEFT|wxRIGHT|wxBOTTOM,5);
  correction_pane->SetSizer(right);
  splitter->SetMinimumPaneSize(260);splitter->SplitVertically(rows,correction_pane,520);
  root->Add(splitter,1,wxEXPAND|wxALL,5);
  auto commit=new wxBoxSizer(wxHORIZONTAL);Button(panel,commit,_("Commit all GREEN"),[this]{CommitResults();});root->Add(commit,0,wxALL,5);
  panel->SetSizer(root);
  rows->Bind(wxEVT_LISTBOX,[this](wxCommandEvent&){selected=size_t(rows->GetSelection());if(auto r=Current())selected_lane=std::max(0,r->lane);notice.clear();Update();});
  lane_choice->Bind(wxEVT_CHOICE,[this](wxCommandEvent&){if(refreshing)return;if(auto r=Current()){r->lane=lane_choice->GetSelection()-1;selected_lane=std::max(0,r->lane);r->reviewed=r->lane>=0;notice.clear();Update();}});
  correction_timer.Bind(wxEVT_TIMER,[this](wxTimerEvent&){ApplyTimingCorrection();});
  offset_spin->Bind(wxEVT_SPINCTRL,[this](wxSpinEvent&){if(refreshing)return;UpdateOffsetDirection();correction_timer.Start(140,true);});
  offset_spin->Bind(wxEVT_TEXT_ENTER,[this](wxCommandEvent&){correction_timer.Stop();UpdateOffsetDirection();ApplyTimingCorrection();});
  panel->Bind(wxEVT_CHAR_HOOK,[this](wxKeyEvent& e){
   int key=e.GetKeyCode();
   if(wxWindow::FindFocus()==offset_spin&&(key==WXK_UP||key==WXK_DOWN)){e.Skip();return;}
   if(key==WXK_ESCAPE){panel->Hide();return;}
   if(key=='R'&&!e.ControlDown()&&!e.AltDown()){Retake();return;}
   if((key==WXK_UP||key==WXK_DOWN)&&!e.AltDown()){
    Navigate(key==WXK_DOWN?1:-1,e.ControlDown());return;
   }
   if(key==WXK_RETURN){
    auto focus=wxWindow::FindFocus();
    for(size_t i=0;i<candidate_buttons.size();++i)if(focus==candidate_buttons[i]){
     if(session.ChooseAssignment(selected,selected_lane,i)){notice.clear();Update();}return;
    }
   }
   e.Skip();
  });
  panel->Bind(wxEVT_CLOSE_WINDOW,[this](wxCloseEvent& e){if(e.CanVeto()){e.Veto();panel->Hide();}else e.Skip();});
 }
 void ShowInspector() {
  if(!Current())return;
  if(!inspector) {
   inspector=new wxDialog(panel,wxID_ANY,_("39 Mode — selected line Inspector"),wxDefaultPosition,wxSize(800,650),wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER);
   auto root=new wxBoxSizer(wxVERTICAL);reading=new wxTextCtrl(inspector,wxID_ANY);root->Add(reading,0,wxEXPAND|wxALL,6);
   Button(inspector,root,_("Apply explicit reading"),[this]{auto r=Current();if(!r)return;auto a=t39::Analyze(from_wx(reading->GetValue()));if(!a.error.empty()){notice=a.error;Update();return;}a.source=r->target.analysis.source;r->target.analysis=std::move(a);session.Rematch(selected,0);session.Rematch(selected,1);rows->RebuildRow(selected,r->target.analysis,r->GetConfidence());Update();});
    root->Add(new wxStaticText(inspector,wxID_ANY,_("Advanced candidate paths (full line)")),0,wxLEFT|wxRIGHT|wxTOP,6);
    paths=new wxChoice(inspector,wxID_ANY);root->Add(paths,0,wxEXPAND|wxALL,6);
   assignments=new wxListBox(inspector,wxID_ANY);root->Add(assignments,1,wxEXPAND|wxALL,6);
   auto edit=new wxBoxSizer(wxHORIZONTAL);Button(inspector,edit,_("← Mora"),[this]{Move(-1);});Button(inspector,edit,_("Mora →"),[this]{Move(1);});Button(inspector,edit,_("Undo"),[this]{History(false);});Button(inspector,edit,_("Redo"),[this]{History(true);});root->Add(edit,0);
    details=new wxTextCtrl(inspector,wxID_ANY,"",wxDefaultPosition,wxSize(-1,240),wxTE_MULTILINE|wxTE_READONLY|wxTE_DONTWRAP);root->Add(details,1,wxEXPAND|wxALL,6);
    raw_toggle=new wxButton(inspector,wxID_ANY,_("Show full session raw capture"));root->Add(raw_toggle,0,wxLEFT|wxRIGHT|wxBOTTOM,6);
    raw_toggle->Bind(wxEVT_BUTTON,[this](wxCommandEvent&){show_full_session_raw=!show_full_session_raw;UpdateInspector();});
   Button(inspector,root,_("Save inspection"),[this]{wxFileDialog save(inspector,_("Save inspection"),"","39-mode.txt",_("Text files (*.txt)|*.txt"),wxFD_SAVE|wxFD_OVERWRITE_PROMPT);if(save.ShowModal()==wxID_OK){wxFFile f(save.GetPath(),"w");if(f.IsOpened())f.Write(details->GetValue(),wxConvUTF8);}});
   paths->Bind(wxEVT_CHOICE,[this](wxCommandEvent&){int p=paths->GetSelection();if(p<0)return;if(session.ChooseAssignment(selected,selected_lane,size_t(p))){notice.clear();Update();}});
   assignments->Bind(wxEVT_KEY_DOWN,[this](wxKeyEvent& e){if(e.GetKeyCode()==WXK_LEFT)Move(-1);else if(e.GetKeyCode()==WXK_RIGHT)Move(1);else e.Skip();});
   inspector->SetSizer(root);inspector->Bind(wxEVT_CLOSE_WINDOW,[this](wxCloseEvent& e){if(e.CanVeto()){e.Veto();inspector->Hide();}else e.Skip();});
  }
  UpdateInspector();inspector->Show();inspector->Raise();
 }
 void Move(int delta){if(auto r=Current()){if(!r->lanes[selected_lane].editor.Move(r->target.analysis,size_t(std::max(0,assignments->GetSelection())),delta))notice="Protected divider; choose a complete alternative path.";else {notice="Assignment moved; raw timestamps unchanged";r->resolution=t39::ResolutionSource::UserSelected;r->reviewed=true;}Update();}}
 void History(bool redo){if(auto r=Current()){auto& e=r->lanes[selected_lane].editor;if(redo)e.Redo();else e.Undo();r->resolution=t39::ResolutionSource::UserSelected;r->reviewed=true;Update();}}
 void Retake(){
  auto current=Current();
  if(!current||current->committed||session.State()!=t39::SessionState::Results)return;
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
  struct Write{t39::SessionResult* result;AssDialogue* event;std::string text;};std::vector<Write> writes;
  for(auto& r:session.Results()) {
   if(r.committed||r.lane<0)continue;
   auto confidence=r.GetConfidence();
   if(confidence!=t39::Confidence::Green)continue;
   auto it=events.find(r.target.id);if(it==events.end()||it->second->Text.get()!=r.target.analysis.source){notice="Source changed; restart session before committing";Update();return;}
   auto& l=r.lanes[r.lane];std::string output,error;
   if(!t39::Serialize(*it->second,r.target.analysis,l.capture.blocks,l.editor.Get(),output,error)){notice=error;Update();return;}
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
  connections.push_back(c->audioController->AddPlaybackPositionListener([this](int ms){last_position=ms;session.Advance(ms);}));
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
  return TimingKey(key.GetKeyCode(),down,c->audioController->GetPlaybackPosition(),false,false)?Event_Processed:Event_Skip;
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
  void Revert() override{c->audioController->Stop();session.Discard();ordinary_boundaries.Reset({});target_boundaries.Reset({});Update();Notify();}
 void PlaybackStarting(int ms) override {
  if(session.State()==t39::SessionState::Capturing)PlaybackStopped(c->audioController->GetPlaybackPosition());
  else if(session.State()==t39::SessionState::Countdown)PlaybackStopped(session.StartTime());
  if(session.Start(ms))physical_held.fill(false);
  last_position=ms;Notify();
 }
 void PlaybackStopped(int ms) override {HideCountdown();physical_held.fill(false);if(session.Stop(ms)){c->videoController->Stop();last_position=ms;ShowResults();Notify();}}
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
