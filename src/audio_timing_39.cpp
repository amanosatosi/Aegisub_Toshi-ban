// Copyright (c) 2026, JibunSenyou contributors. ISC license.
#include "audio_timing.h"
#include "timing39_karaoke.h"
#include "ass_dialogue.h"
#include "ass_file.h"
#include "audio_box.h"
#include "audio_controller.h"
#include "audio_rendering_style.h"
#include "compat.h"
#include "include/aegisub/context.h"
#include "selection_controller.h"
#include <libaegisub/make_unique.h>
#include <algorithm>
#include <map>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/listbox.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/filedlg.h>
#include <wx/ffile.h>

namespace {
namespace t39 = agi::timing39;
struct LaneReview {
	AssDialogue* target = nullptr;
	t39::Analysis analysis;
	t39::MatchResult match;
	t39::AssignmentEditor editor;
};
struct LineReview {
	int start=0,end=0;
	t39::TimingCaptureSession capture;
	std::array<LaneReview,2> lanes;
};

class AudioTimingController39 final : public AudioTimingController {
	agi::Context* c;
	std::vector<agi::signal::Connection> connections;
	std::map<AssDialogue*,LineReview> lines;
	AssDialogue* active=nullptr;
	wxDialog* panel=nullptr;
	wxStaticText* status=nullptr;
	wxChoice *lane_choice=nullptr,*target_choice=nullptr,*path_choice=nullptr;
	wxListBox* assignments=nullptr;
	wxTextCtrl *reading=nullptr,*details=nullptr;
	std::vector<AssDialogue*> targets;
	int selected_lane=0,armed=0,pending_arm=3,last_position=0;
	bool committing=false,refreshing=false;
	std::string notice;

	LineReview* Current() {auto it=lines.find(active);return it==lines.end()?nullptr:&it->second;}
	LineReview const* Current() const {auto it=lines.find(active);return it==lines.end()?nullptr:&it->second;}
	void Notify() {AnnounceMarkerMoved();AnnounceLabelChanged();}
	void AnalyzeTarget(LaneReview& lane,AssDialogue* target) {
		lane.target=target;
		lane.analysis=target?t39::AnalyzeDialogue(*target):t39::Analysis{};
		if(!target) lane.analysis.error="Choose a relevant subtitle event for this lane";
		lane.match={};lane.editor.Reset({});
	}
	void SelectLine() {
		if(c->audioController->IsPlaying()) c->audioController->Stop();
		active=c->selectionController->GetActiveLine();armed=0;pending_arm=0;
		if(active && !lines.count(active)) {
			auto& line=lines[active];line.start=active->Start;line.end=active->End;
			AnalyzeTarget(line.lanes[0],active);AnalyzeTarget(line.lanes[1],nullptr);pending_arm=3;
		}
		if(auto line=Current())reading->ChangeValue(to_wx(line->lanes[selected_lane].analysis.surface));
		notice.clear();Update();AnnounceUpdatedPrimaryRange();Notify();
	}
	void FileChanged(int type,AssDialogue const*) {
		if(committing || !(type&AssFile::COMMIT_DIAG_FULL)) return;
		// Undo/deletion may replace dialogue objects. Never retain stale pointers.
		if(c->audioController->IsPlaying()) c->audioController->Stop();
		lines.clear();SelectLine();notice="Subtitle edit/undo reloaded the preview";Update();
	}
	void Resolve(int mask) {
		auto line=Current();if(!line)return;
		for(size_t i=0;i<2;++i) {
			if(!(mask&(1<<i)))continue;
			auto& lane=line->lanes[i];lane.match=t39::Match(lane.analysis,line->capture.lanes[i].Blocks());
			lane.editor.Reset(lane.match.paths.empty()?std::vector<t39::TimingAssignment>{}:lane.match.paths[0].assignments);
		}
		InferTargets();Update();Notify();
	}
	static std::vector<int> Boundaries(std::vector<t39::TimingBlock> const& blocks) {
		std::vector<int> out;for(auto const& b:blocks)if(!b.gap) {out.push_back(b.start);out.push_back(b.end);}
		std::sort(out.begin(),out.end());out.erase(std::unique(out.begin(),out.end()),out.end());return out;
	}
	bool Relevant(AssDialogue const& d,LineReview const& line) const {
		return !d.Comment && int(d.Start)<line.end && int(d.End)>line.start;
	}
	void InferTargets() {
		auto line=Current();if(!line)return;
		std::vector<t39::StyleEvidence> evidence;
		for(auto const& checkpoint:lines) {
			auto const& review=checkpoint.second;
			auto p=Boundaries(review.capture.lanes[0].Blocks()),s=Boundaries(review.capture.lanes[1].Blocks());
			if(p.empty()||s.empty())continue;
			for(auto const& d:c->ass->Events) if(Relevant(d,review)) evidence.push_back({d.Style,t39::KaraokeBoundaries(d),p,s});
		}
		auto inferred=t39::InferStyles(evidence);
		if(Boundaries(line->capture.lanes[1].Blocks()).empty())return;
		if(inferred.ambiguous) {if(!line->lanes[1].target)notice="YELLOW: style evidence is ambiguous; choose the D/K target event";return;}
		if(line->lanes[0].target && line->lanes[0].target->Style!=inferred.primary) {
			notice="YELLOW: primary target conflicts with timing evidence; resolve both target events manually";return;
		}
		// There must be exactly one relevant event per inferred style. Never pick
		// an arbitrary row when overlapping duplicates share a style.
		for(size_t i=0;i<2;++i) {
			if(line->lanes[i].target)continue;
			AssDialogue* target=nullptr;size_t count=0;
			for(auto& d:c->ass->Events) if(Relevant(d,*line) && d.Style==(i?inferred.secondary:inferred.primary)) {target=&d;++count;}
			if(count==1 && target!=line->lanes[1-i].target) {
				AnalyzeTarget(line->lanes[i],target);
				auto& lane=line->lanes[i];lane.match=t39::Match(lane.analysis,line->capture.lanes[i].Blocks());
				if(!lane.match.paths.empty())lane.editor.Reset(lane.match.paths[0].assignments);
			}
		}
	}
	std::string Group(t39::Analysis const& a,t39::TimingAssignment const& x) const {
		std::string out;for(size_t i=0;i<x.mora_count;++i)out+=a.morae[x.first_mora+i].text;return out;
	}
	void Update() {
		if(!panel)return;refreshing=true;
		auto line=Current();targets.clear();target_choice->Clear();target_choice->Append(_("Choose target event"));
		if(line) {
			for(auto& d:c->ass->Events) if(Relevant(d,*line)) {
				targets.push_back(&d);target_choice->Append(to_wx(d.Style.get()+"  "+d.Text.get().substr(0,180)));
			}
			auto& lane=line->lanes[selected_lane];int selection=0;
			for(size_t i=0;i<targets.size();++i)if(targets[i]==lane.target)selection=int(i+1);
			target_choice->SetSelection(selection);
			std::string message=armed?"Capturing: F/J primary, D/K secondary. Space stops.":lane.match.reason;
			if(!lane.analysis.error.empty())message=lane.analysis.error;
			if(!notice.empty())message=notice;
			if(message.empty())message="Space starts capture. F/J primary, D/K secondary. R retakes this lane.";
			std::string color=lane.match.confidence==t39::Confidence::Green?"GREEN":lane.match.confidence==t39::Confidence::Yellow?"YELLOW":"RED";
			status->SetLabel(to_wx("39 Mode — "+(armed?std::string("LIVE"):color)+"\n"+message));
			status->SetForegroundColour(armed?wxColour(57,197,187):lane.match.confidence==t39::Confidence::Green?wxColour(30,160,110):lane.match.confidence==t39::Confidence::Yellow?wxColour(175,125,0):wxColour(200,60,60));
			int divider=assignments->GetSelection();assignments->Clear();
			for(size_t i=0;i<lane.editor.Get().size();++i) {
				auto const& x=lane.editor.Get()[i];auto const& b=line->capture.lanes[selected_lane].Blocks()[x.timing_block];
				bool uncertain=std::find(lane.match.uncertain_boundaries.begin(),lane.match.uncertain_boundaries.end(),i)!=lane.match.uncertain_boundaries.end();
				assignments->Append(to_wx((uncertain?"? ":"  ")+std::to_string(i+1)+"  "+std::to_string(b.start)+"–"+std::to_string(b.end)+" ms  ["+Group(lane.analysis,x)+"]"));
			}
			if(assignments->GetCount())assignments->SetSelection(std::max(0,std::min(divider,int(assignments->GetCount()-1))));
			path_choice->Clear();
			for(size_t p=0;p<lane.match.paths.size();++p) {
				std::string text=std::to_string(p+1)+": ";for(auto const& x:lane.match.paths[p].assignments)text+="["+Group(lane.analysis,x)+"]";
				path_choice->Append(to_wx(text));if(lane.editor.Get()==lane.match.paths[p].assignments)path_choice->SetSelection(int(p));
			}
			details->ChangeValue(to_wx(t39::Inspect(lane.analysis,line->capture.lanes[selected_lane].Blocks(),lane.match,&lane.editor)));
		}
		else status->SetLabel(_("39 Mode — select a dialogue line"));
		panel->Layout();refreshing=false;
	}
	void Retake(int mask) {
		auto line=Current();if(!line)return;
		c->audioController->Stop();pending_arm=mask;notice.clear();
		for(int i=0;i<2;++i)if(mask&(1<<i)) {line->capture.lanes[i].Clear();line->lanes[i].match={};line->lanes[i].editor.Reset({});}
		c->audioBox->FocusAudio();c->audioController->PlayRange(TimeRange(std::max(0,line->start-500),line->end));
	}
	void Move(int delta) {
		auto line=Current();if(!line||armed)return;auto& lane=line->lanes[selected_lane];
		int divider=assignments->GetSelection();
		if(divider<=0 || !lane.editor.Move(lane.analysis,size_t(divider),delta))notice="This divider cannot move through a protected boundary. Choose a complete alternative path.";
		else notice="Assignment moved; captured timestamps unchanged";
		Update();Notify();
	}
	void History(bool redo) {
		auto line=Current();if(!line||armed)return;auto& editor=line->lanes[selected_lane].editor;
		if(redo)editor.Redo();else editor.Undo();Update();Notify();
	}
	void ApplyReading() {
		auto line=Current();if(!line||armed)return;auto& lane=line->lanes[selected_lane];
		// Accept a complete source/ruby expression so explicit pronunciation is
		// never silently overwritten by a dictionary or guessed alignment.
		auto replacement=t39::Analyze(from_wx(reading->GetValue()));
		if(!replacement.error.empty()) {notice=replacement.error;Update();return;}
		if(!lane.target) {notice="Choose the target event before editing its reading";Update();return;}
		replacement.source=lane.target->Text;lane.analysis=std::move(replacement);
		lane.match=t39::Match(lane.analysis,line->capture.lanes[selected_lane].Blocks());
		lane.editor.Reset(lane.match.paths.empty()?std::vector<t39::TimingAssignment>{}:lane.match.paths[0].assignments);
		notice="Reading preview updated; capture preserved. Enter commits the displayed source and timing.";Update();Notify();
	}
	void SaveInspection() {
		auto line=Current();if(!line)return;
		wxFileDialog save(panel,_("Save 39 Mode analysis and corrections"),"","39-mode.txt",_("Text files (*.txt)|*.txt"),wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
		if(save.ShowModal()!=wxID_OK)return;
		wxFFile file(save.GetPath(),"w");if(file.IsOpened())file.Write(details->GetValue(),wxConvUTF8);
	}
	void MakePanel() {
		panel=new wxDialog(c->parent,wxID_ANY,_("39 Mode"),wxDefaultPosition,wxSize(740,650),wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER);
		auto root=new wxBoxSizer(wxVERTICAL);status=new wxStaticText(panel,wxID_ANY,_("39 Mode"));root->Add(status,0,wxEXPAND|wxALL,8);
		auto row=new wxBoxSizer(wxHORIZONTAL);lane_choice=new wxChoice(panel,wxID_ANY);lane_choice->Append(_("F/J primary"));lane_choice->Append(_("D/K secondary"));lane_choice->SetSelection(0);row->Add(lane_choice,0,wxRIGHT,5);
		target_choice=new wxChoice(panel,wxID_ANY);row->Add(target_choice,1);root->Add(row,0,wxEXPAND|wxALL,8);
		root->Add(new wxStaticText(panel,wxID_ANY,_("Source / reading preview (use <display|reading>; explicit readings always win):")),0,wxLEFT|wxRIGHT,8);
		reading=new wxTextCtrl(panel,wxID_ANY);root->Add(reading,0,wxEXPAND|wxALL,8);
		auto actions=new wxBoxSizer(wxHORIZONTAL);
		auto button=[&](wxSizer* s,wxString const& label,std::function<void()> fn) {auto b=new wxButton(panel,wxID_ANY,label);s->Add(b,0,wxRIGHT,4);b->Bind(wxEVT_BUTTON,[fn](wxCommandEvent&){fn();});};
		button(actions,_("Apply reading"),[this]{ApplyReading();});button(actions,_("Capture both"),[this]{Retake(3);});button(actions,_("Retake lane (R)"),[this]{Retake(1<<selected_lane);});button(actions,_("Stop"),[this]{c->audioController->Stop();});root->Add(actions,0,wxALL,8);
		path_choice=new wxChoice(panel,wxID_ANY);root->Add(path_choice,0,wxEXPAND|wxALL,8);
		root->Add(new wxStaticText(panel,wxID_ANY,_("Select the block after a divider. Left/Right moves mora placement, not time.")),0,wxLEFT|wxRIGHT,8);
		assignments=new wxListBox(panel,wxID_ANY);root->Add(assignments,1,wxEXPAND|wxALL,8);
		auto edits=new wxBoxSizer(wxHORIZONTAL);button(edits,_("← Mora"),[this]{Move(-1);});button(edits,_("Mora →"),[this]{Move(1);});button(edits,_("Undo"),[this]{History(false);});button(edits,_("Redo"),[this]{History(true);});button(edits,_("Commit (Enter)"),[this]{Commit();});button(edits,_("Discard"),[this]{Revert();});root->Add(edits,0,wxALL,8);
		details=new wxTextCtrl(panel,wxID_ANY,"",wxDefaultPosition,wxSize(-1,140),wxTE_MULTILINE|wxTE_READONLY|wxTE_DONTWRAP);root->Add(details,1,wxEXPAND|wxALL,8);
		auto footer=new wxBoxSizer(wxHORIZONTAL);button(footer,_("Save inspection"),[this]{SaveInspection();});button(footer,_("Focus audio"),[this]{c->audioBox->FocusAudio();});button(footer,_("Next line"),[this]{Next(LINE);});root->Add(footer,0,wxALL,8);
		panel->SetSizer(root);
		lane_choice->Bind(wxEVT_CHOICE,[this](wxCommandEvent&){selected_lane=lane_choice->GetSelection();notice.clear();Update();auto line=Current();if(line)reading->ChangeValue(to_wx(line->lanes[selected_lane].analysis.surface));});
		target_choice->Bind(wxEVT_CHOICE,[this](wxCommandEvent&){
			if(refreshing||armed)return;auto line=Current();int n=target_choice->GetSelection();if(!line||n<1)return;
			auto target=targets[size_t(n-1)];if(target==line->lanes[1-selected_lane].target){notice="The two lanes require distinct events";Update();return;}
			AnalyzeTarget(line->lanes[selected_lane],target);auto& lane=line->lanes[selected_lane];lane.match=t39::Match(lane.analysis,line->capture.lanes[selected_lane].Blocks());if(!lane.match.paths.empty())lane.editor.Reset(lane.match.paths[0].assignments);
			notice="Target event selected manually";reading->ChangeValue(to_wx(lane.analysis.surface));Update();Notify();
		});
		path_choice->Bind(wxEVT_CHOICE,[this](wxCommandEvent&){auto line=Current();int p=path_choice->GetSelection();if(!line||armed||p<0)return;auto& lane=line->lanes[selected_lane];lane.editor.Choose(lane.analysis,lane.match.paths[size_t(p)].assignments);notice="Alternative selected; captured timestamps unchanged";Update();Notify();});
		assignments->Bind(wxEVT_KEY_DOWN,[this](wxKeyEvent& e){if(e.GetKeyCode()==WXK_LEFT)Move(-1);else if(e.GetKeyCode()==WXK_RIGHT)Move(1);else if(e.GetKeyCode()==WXK_RETURN)Commit();else e.Skip();});
		panel->Bind(wxEVT_CLOSE_WINDOW,[this](wxCloseEvent& e){if(e.CanVeto()){e.Veto();panel->Hide();c->audioBox->FocusAudio();}else e.Skip();});
		panel->Show();
	}
public:
	explicit AudioTimingController39(agi::Context* context):c(context) {
		MakePanel();SelectLine();
		if(auto line=Current())reading->ChangeValue(to_wx(line->lanes[0].analysis.surface));
		connections.push_back(c->selectionController->AddActiveLineListener([this]{SelectLine();}));
		connections.push_back(c->ass->AddCommitListener(&AudioTimingController39::FileChanged,this));
		connections.push_back(c->audioController->AddPlaybackPositionListener([this](int ms){last_position=ms;if(armed && Current() && ms>=Current()->end)PlaybackStopped(Current()->end);}));
	}
	~AudioTimingController39() override {
		connections.clear();
		// wx Destroy is deferred. Delete the owned modeless view synchronously
		// so none of its callbacks can outlive this controller.
		delete panel;
	}
	bool Is39Mode() const override{return true;}
	wxString GetWarningMessage() const override{return Get39Status();}
	wxString Get39Status() const override {
		auto line=Current();if(!line)return _("39 Mode — select a line");
		if(armed)return _("39 Mode • F/J primary • D/K secondary • Space stops");
		return to_wx("39 Mode • Space capture / audition • R retake lane • Tab review • "+notice);
	}
	TimeRange GetActiveLineRange() const override{auto line=Current();return line?TimeRange(line->start,line->end):TimeRange(0,0);}
	TimeRange GetIdealVisibleTimeRange() const override{return GetActiveLineRange();}
	TimeRange GetPrimaryPlaybackRange() const override{return GetActiveLineRange();}
	void GetMarkers(TimeRange const&,AudioMarkerVector&) const override{}
	void GetLabels(TimeRange const&,std::vector<AudioLabel>&) const override{}
	void GetRenderingStyles(AudioRenderingStyleRanges& ranges) const override {auto line=Current();if(line)ranges.AddRange(line->start,line->end,AudioStyle_Selected);}
	bool IsNearbyMarker(int,int,bool) const override{return false;}
	std::vector<AudioMarker*> OnLeftClick(int,bool,bool,int,int) override{return {};}
	std::vector<AudioMarker*> OnRightClick(int,bool,int,int) override{panel->Show();panel->Raise();return {};}
	void OnMarkerDrag(std::vector<AudioMarker*> const&,int,int) override{}
	void AddLeadIn() override{}
	void AddLeadOut() override{}
	void ModifyLength(int,bool) override{}
	void ModifyStart(int) override{}
	void Next(NextMode) override{c->selectionController->NextLine();}
	void Prev() override{c->selectionController->PrevLine();}
	void Revert() override {c->audioController->Stop();lines.erase(active);SelectLine();}
	void PlaybackStarting(int ms) override {
		auto line=Current();if(!line)return;
		if(armed)PlaybackStopped(last_position); // seek/restart sanitizes owner
		armed=pending_arm;pending_arm=0;last_position=ms;
		for(int i=0;i<2;++i)if(armed&(1<<i))line->capture.lanes[i].Begin(line->start,line->end);
		Update();Notify();
	}
	void PlaybackStopped(int ms) override {
		auto line=Current();if(!line||!armed)return;
		for(int i=0;i<2;++i)if(armed&(1<<i))line->capture.lanes[i].Finish(ms);
		int captured=armed;armed=0;last_position=ms;Resolve(captured);
	}
	void TimingFocusLost(int ms) override {PlaybackStopped(ms);}
	bool TimingKey(int key,bool down,int ms,bool control,bool shift) override {
		auto line=Current();if(!line)return false;
		if(key=='F'||key=='J'||key=='D'||key=='K') {
			if((down&&(control||shift))||!armed||!c->audioController->IsPlaying())return false;
			int lane=key=='F'||key=='J'?0:1;if(!(armed&(1<<lane)))return true;
			bool changed=down?line->capture.lanes[lane].KeyDown(key,ms):line->capture.lanes[lane].KeyUp(key,ms);
			if(changed)Notify();return true;
		}
		if(!down)return false;
		if(key==WXK_SPACE) {if(c->audioController->IsPlaying())c->audioController->Stop();else {c->audioController->PlayRange(TimeRange(std::max(0,line->start-500),line->end));}return true;}
		if(key==WXK_ESCAPE){c->audioController->Stop();notice="Capture stopped and preserved; Discard clears this checkpoint";Update();return true;}
		if(key==WXK_TAB){panel->Show();panel->Raise();assignments->SetFocus();return true;}
		if(key=='R'&&!control){Retake(1<<selected_lane);return true;}
		if(key==WXK_RETURN){Commit();return true;}
		if(control&&key=='Z'){History(shift);return true;}
		if(control&&key=='Y'){History(true);return true;}
		return false;
	}
	void Get39Overlay(std::vector<Timing39Overlay>& out,int ms) const override {
		auto line=Current();if(!line)return;
		for(size_t lane=0;lane<2;++lane) {
			auto blocks=line->capture.lanes[lane].Preview(ms);auto const& review=line->lanes[lane];
			for(size_t i=0;i<blocks.size();++i) {
				auto const& b=blocks[i];std::string text=b.gap?"gap":"•";
				for(auto const& x:review.editor.Get())if(x.timing_block==i)text=Group(review.analysis,x);
				out.push_back({b.start,b.end,int(lane),to_wx(text),b.gap,review.match.confidence==t39::Confidence::Yellow});
			}
			if(blocks.empty()) {
				std::string text;for(auto const& mora:review.analysis.morae){if(!text.empty())text+=" | ";text+=mora.text;}
				out.push_back({line->start,line->end,int(lane),to_wx(text),false,false});
			}
		}
	}
	void Commit() override {
		c->audioController->Stop();auto line=Current();if(!line)return;
		std::vector<std::pair<AssDialogue*,std::string>> writes;
		for(size_t i=0;i<2;++i) {
			auto const& blocks=line->capture.lanes[i].Blocks();if(Boundaries(blocks).empty())continue;
			auto& lane=line->lanes[i];std::string output,error;
			if(!lane.target || lane.analysis.source!=lane.target->Text.get()) {notice="Choose a target event or reload its changed source before committing";Update();return;}
			if(!t39::Serialize(*lane.target,lane.analysis,blocks,lane.editor.Get(),output,error)) {notice=error;Update();return;}
			writes.emplace_back(lane.target,std::move(output));
		}
		if(writes.empty()){notice="No capture to commit";Update();return;}
		committing=true;
		for(auto const& write:writes)write.first->Text=write.second;
		// Structured diagnostics are retained with the line for regression data;
		// no global weights are changed in response to a correction.
		for(size_t i=0;i<2;++i) {auto& lane=line->lanes[i];if(lane.target&&!lane.editor.corrections.empty())c->ass->SetExtradataValue(*lane.target,"39-mode-correction",t39::Inspect(lane.analysis,line->capture.lanes[i].Blocks(),lane.match,&lane.editor));}
		c->ass->Commit(_("39 Mode karaoke timing"),AssFile::COMMIT_DIAG_TEXT);
		committing=false;
		for(auto const& write:writes)for(auto& lane:line->lanes)if(lane.target==write.first)lane.analysis.source=write.second;
		notice="Committed. Subtitle undo restores the previous events.";Update();Notify();
	}
};
}

std::unique_ptr<AudioTimingController> Create39TimingController(agi::Context* c) {return agi::make_unique<AudioTimingController39>(c);}
