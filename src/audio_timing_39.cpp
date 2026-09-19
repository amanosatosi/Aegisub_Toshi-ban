// Copyright (c) 2026, JibunSenyou contributors. ISC license.
#include "audio_timing.h"
#include "timing39_karaoke.h"
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
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/eventfilter.h>
#include <wx/listbox.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/timer.h>
#include <wx/filedlg.h>
#include <wx/ffile.h>

namespace {
namespace t39 = agi::timing39;
class AudioTimingController39 final : public AudioTimingController, public wxEventFilter {
 agi::Context* c;
 t39::Timing39Session session;
 std::map<uint64_t,AssDialogue*> events;
 std::vector<agi::signal::Connection> connections;
 wxTimer countdown;
 wxDialog *panel=nullptr,*inspector=nullptr;
 wxListBox *rows=nullptr,*assignments=nullptr;
 wxChoice *lane_choice=nullptr,*paths=nullptr;
 wxStaticText *status=nullptr;
 wxTextCtrl *reading=nullptr,*details=nullptr;
 size_t selected=0;
 int selected_lane=0,last_position=0;
 unsigned rhythm_serial=0;
 std::array<bool,4> physical_held{{false,false,false,false}};
 bool committing=false,refreshing=false;
 std::string notice;
 int Preroll() const {return std::max(0,int(OPT_GET("Audio/Lead/IN")->GetInt()));}
 void Notify(){AnnounceMarkerMoved();AnnounceLabelChanged();}
 t39::SessionResult* Current(){return selected<session.Results().size()?&session.Results()[selected]:nullptr;}
 static std::string Color(t39::Confidence s){return s==t39::Confidence::Green?"GREEN":s==t39::Confidence::Yellow?"YELLOW":"RED";}
 void Prepare() {
  auto active=c->selectionController->GetActiveLine();
  auto provider=c->project->AudioProvider();
  if(!active||!provider){notice="Select a lyric line and open audio";return;}
  auto const& chosen=c->selectionController->GetSelectedSet();
  bool explicit_scope=chosen.size()>1;
  int start=active->Start,end=int(provider->GetNumSamples()*1000/provider->GetSampleRate());
  if(explicit_scope){start=end;end=0;for(auto d:chosen){start=std::min(start,int(d->Start));end=std::max(end,int(d->End));}}
  std::vector<t39::SessionTarget> targets;
  uint64_t id=0;
  for(auto& d:c->ass->Events) {
   if(d.Comment)continue;
   if(explicit_scope&&!chosen.count(&d))continue;
   if(!explicit_scope&&int(d.End)<=start)continue;
   t39::SessionTarget t;t.id=++id;t.start=d.Start;t.end=d.End;t.style=d.Style;
   t.analysis=t39::AnalyzeDialogue(d);t.existing_boundaries=t39::KaraokeBoundaries(d);
   t.selected=chosen.count(&d)!=0;
   t.lyric_evidence=!t.analysis.morae.empty() && std::any_of(t.analysis.reading.characters.begin(),t.analysis.reading.characters.end(),[](t39::ReadingCharacter const& ch){return ch.kana>=U'ぁ'&&ch.kana<=U'ー';});
   events[t.id]=&d;targets.push_back(std::move(t));
  }
  session.Prepare(std::move(targets),explicit_scope,active->Style,std::max(0,start-Preroll()),end);
  last_position=session.StartTime();countdown.Start(1000);
 }
 void Tick(wxTimerEvent&) {
  if(session.State()!=t39::SessionState::Countdown){countdown.Stop();return;}
  if(session.TickCountdown()) {
   countdown.Stop();c->audioController->PlayRange(TimeRange(session.StartTime(),session.EndTime()));
   if(!c->audioController->IsPlaying())PlaybackStopped(session.StartTime());
  }
  Notify();
 }
 void ShowResults(){if(!panel)MakePanel();Update();panel->Show();panel->Raise();}
 void FileChanged(int type,AssDialogue const*) {
  if(committing||!(type&(AssFile::COMMIT_DIAG_FULL|AssFile::COMMIT_DIAG_ADDREM)))return;
  c->audioController->Stop();countdown.Stop();session.Discard();events.clear();
  notice="Subtitle edit or undo invalidated the cached targets. Toggle 39 Mode to start a new session.";
  if(inspector)inspector->Hide();Update();Notify();
 }
 void Update() {
  if(!panel)return;refreshing=true;rows->Clear();
  for(auto const& r:session.Results())rows->Append(to_wx(Color(r.GetConfidence())+(r.committed?" [committed] ":r.reviewed?" [reviewed] ":" ")+std::to_string(r.target.start)+"–"+std::to_string(r.target.end)+"  "+r.target.style+"  "+r.target.analysis.surface));
  if(auto r=Current()) {
   rows->SetSelection(int(selected));lane_choice->SetSelection(r->lane+1);
   status->SetLabel(to_wx(notice.empty()?r->target.discovery_reason+"; "+r->lanes[selected_lane].match.reason:notice));
  }else status->SetLabel(to_wx(notice.empty()?"No captured lyric targets. Toggle 39 Mode to start again.":notice));
  if(inspector)UpdateInspector();
  panel->Layout();refreshing=false;
 }
 std::string Group(t39::Analysis const& a,t39::TimingAssignment const& x) const {
  std::string out;for(size_t i=0;i<x.mora_count;++i)out+=a.morae[x.first_mora+i].text;return out;
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
  std::string extra="\nSESSION RAW F/J\n";
  for(int lane=0;lane<2;++lane){if(lane)extra+="SESSION RAW D/K\n";for(auto const& b:session.Raw(lane))extra+=std::to_string(b.start)+" "+std::to_string(b.end)+(b.gap?" gap\n":" sung\n");}
  for(auto const& take:session.retakes){extra+="RETAKE target="+std::to_string(take.target)+" lane="+std::to_string(take.lane)+"\n";for(auto const& b:take.raw)extra+=std::to_string(b.start)+" "+std::to_string(b.end)+(b.gap?" gap\n":" sung\n");}
  extra+="LOCAL RAW INDICES ";for(auto i:l.capture.raw_indices)extra+=std::to_string(i)+" ";
  extra+=l.capture.clipped?"\nCLIPPED at dialogue checkpoint; review required\n":"\n";
  details->ChangeValue(to_wx(t39::Inspect(r->target.analysis,l.capture.blocks,l.match,&l.editor)+extra));
 }
 void Button(wxWindow* parent,wxSizer* s,wxString const& label,std::function<void()> fn) {
  auto b=new wxButton(parent,wxID_ANY,label);s->Add(b,0,wxALL,3);b->Bind(wxEVT_BUTTON,[fn](wxCommandEvent&){fn();});
 }
 void MakePanel() {
  panel=new wxDialog(c->parent,wxID_ANY,_("39 Mode — session results"),wxDefaultPosition,wxSize(820,460),wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER);
  auto root=new wxBoxSizer(wxVERTICAL);status=new wxStaticText(panel,wxID_ANY,"");root->Add(status,0,wxEXPAND|wxALL,8);
  rows=new wxListBox(panel,wxID_ANY);root->Add(rows,1,wxEXPAND|wxALL,8);
  auto select=new wxBoxSizer(wxHORIZONTAL);lane_choice=new wxChoice(panel,wxID_ANY);lane_choice->Append(_("Unresolved lane"));lane_choice->Append(_("F/J primary"));lane_choice->Append(_("D/K secondary"));select->Add(lane_choice,0,wxALL,3);
  Button(panel,select,_("Mark reviewed"),[this]{if(auto r=Current()){r->reviewed=r->lane>=0;notice=r->reviewed?"Selected line reviewed":"Choose its lane first";Update();}});
  Button(panel,select,_("Inspector"),[this]{ShowInspector();});
  Button(panel,select,_("Retake selected lane"),[this]{Retake();});root->Add(select,0,wxALL,5);
  auto commit=new wxBoxSizer(wxHORIZONTAL);Button(panel,commit,_("Commit all GREEN"),[this]{CommitResults(false);});Button(panel,commit,_("Commit reviewed GREEN / YELLOW"),[this]{CommitResults(true);});root->Add(commit,0,wxALL,5);
  panel->SetSizer(root);
  rows->Bind(wxEVT_LISTBOX,[this](wxCommandEvent&){selected=size_t(rows->GetSelection());if(auto r=Current())selected_lane=std::max(0,r->lane);notice.clear();Update();});
  lane_choice->Bind(wxEVT_CHOICE,[this](wxCommandEvent&){if(refreshing)return;if(auto r=Current()){r->lane=lane_choice->GetSelection()-1;r->association_ambiguous=true;selected_lane=std::max(0,r->lane);r->reviewed=false;notice="Lane selected. Inspect and mark reviewed before committing ambiguity.";Update();}});
  panel->Bind(wxEVT_CLOSE_WINDOW,[this](wxCloseEvent& e){if(e.CanVeto()){e.Veto();panel->Hide();}else e.Skip();});
 }
 void ShowInspector() {
  if(!Current())return;
  if(!inspector) {
   inspector=new wxDialog(panel,wxID_ANY,_("39 Mode — selected line Inspector"),wxDefaultPosition,wxSize(800,650),wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER);
   auto root=new wxBoxSizer(wxVERTICAL);reading=new wxTextCtrl(inspector,wxID_ANY);root->Add(reading,0,wxEXPAND|wxALL,6);
   Button(inspector,root,_("Apply explicit reading"),[this]{auto r=Current();if(!r)return;auto a=t39::Analyze(from_wx(reading->GetValue()));if(!a.error.empty()){notice=a.error;Update();return;}a.source=r->target.analysis.source;r->target.analysis=std::move(a);session.Rematch(selected,0);session.Rematch(selected,1);Update();});
   paths=new wxChoice(inspector,wxID_ANY);root->Add(paths,0,wxEXPAND|wxALL,6);
   assignments=new wxListBox(inspector,wxID_ANY);root->Add(assignments,1,wxEXPAND|wxALL,6);
   auto edit=new wxBoxSizer(wxHORIZONTAL);Button(inspector,edit,_("← Mora"),[this]{Move(-1);});Button(inspector,edit,_("Mora →"),[this]{Move(1);});Button(inspector,edit,_("Undo"),[this]{History(false);});Button(inspector,edit,_("Redo"),[this]{History(true);});root->Add(edit,0);
   details=new wxTextCtrl(inspector,wxID_ANY,"",wxDefaultPosition,wxSize(-1,240),wxTE_MULTILINE|wxTE_READONLY|wxTE_DONTWRAP);root->Add(details,1,wxEXPAND|wxALL,6);
   Button(inspector,root,_("Save inspection"),[this]{wxFileDialog save(inspector,_("Save inspection"),"","39-mode.txt",_("Text files (*.txt)|*.txt"),wxFD_SAVE|wxFD_OVERWRITE_PROMPT);if(save.ShowModal()==wxID_OK){wxFFile f(save.GetPath(),"w");if(f.IsOpened())f.Write(details->GetValue(),wxConvUTF8);}});
   paths->Bind(wxEVT_CHOICE,[this](wxCommandEvent&){auto r=Current();int p=paths->GetSelection();if(!r||p<0)return;auto& l=r->lanes[selected_lane];l.editor.Choose(r->target.analysis,l.match.paths[size_t(p)].assignments);r->reviewed=false;Update();});
   assignments->Bind(wxEVT_KEY_DOWN,[this](wxKeyEvent& e){if(e.GetKeyCode()==WXK_LEFT)Move(-1);else if(e.GetKeyCode()==WXK_RIGHT)Move(1);else e.Skip();});
   inspector->SetSizer(root);inspector->Bind(wxEVT_CLOSE_WINDOW,[this](wxCloseEvent& e){if(e.CanVeto()){e.Veto();inspector->Hide();}else e.Skip();});
  }
  UpdateInspector();inspector->Show();inspector->Raise();
 }
 void Move(int delta){if(auto r=Current()){if(!r->lanes[selected_lane].editor.Move(r->target.analysis,size_t(std::max(0,assignments->GetSelection())),delta))notice="Protected divider; choose a complete alternative path.";else notice="Assignment moved; raw timestamps unchanged";r->reviewed=false;Update();}}
 void History(bool redo){if(auto r=Current()){auto& e=r->lanes[selected_lane].editor;if(redo)e.Redo();else e.Undo();r->reviewed=false;Update();}}
 void Retake(){if(session.Retake(selected,selected_lane,Preroll())){panel->Hide();if(inspector)inspector->Hide();last_position=session.StartTime();countdown.Start(1000);c->audioBox->FocusAudio();AnnounceUpdatedPrimaryRange();Notify();}}
 void CommitResults(bool reviewed) {
  if(Is39SessionActive())return;
  struct Write{t39::SessionResult* result;AssDialogue* event;std::string text;};std::vector<Write> writes;
  for(auto& r:session.Results()) {
   if(r.committed||r.lane<0)continue;
   auto confidence=r.GetConfidence();
   if(reviewed?(!r.reviewed||confidence==t39::Confidence::Red):confidence!=t39::Confidence::Green)continue;
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
  connections.push_back(c->audioController->AddPlaybackPositionListener([this](int ms){last_position=ms;}));
 }
 ~AudioTimingController39() override {countdown.Stop();wxEvtHandler::RemoveFilter(this);connections.clear();delete inspector;delete panel;}
 int FilterEvent(wxEvent& event) override {
  if(Is39SessionActive()&&event.GetEventType()==wxEVT_ACTIVATE_APP&&!static_cast<wxActivateEvent&>(event).GetActive()){physical_held.fill(false);c->audioController->Stop();return Event_Skip;}
  if(event.GetEventType()!=wxEVT_CHAR_HOOK&&event.GetEventType()!=wxEVT_KEY_DOWN&&event.GetEventType()!=wxEVT_KEY_UP)return Event_Skip;
  auto window=dynamic_cast<wxWindow*>(event.GetEventObject());
  while(window&&window!=c->parent)window=window->GetParent();if(!window)return Event_Skip;
  auto& key=static_cast<wxKeyEvent&>(event);bool down=event.GetEventType()!=wxEVT_KEY_UP;
  int code=key.GetKeyCode();
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
 wxString GetWarningMessage() const override{return Get39GetConfidence();}
 wxString Get39GetConfidence() const override {
  if(session.State()==t39::SessionState::Countdown)return to_wx("39 Mode    "+std::to_string(session.Countdown())+"    F/J primary • D/K secondary");
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
 void Revert() override{c->audioController->Stop();session.Discard();Update();Notify();}
 void PlaybackStarting(int ms) override {
  if(session.State()==t39::SessionState::Capturing)PlaybackStopped(c->audioController->GetPlaybackPosition());
  else if(session.State()==t39::SessionState::Countdown)PlaybackStopped(session.StartTime());
  session.Start(ms);last_position=ms;Notify();
 }
 void PlaybackStopped(int ms) override {countdown.Stop();if(session.Stop(ms)){last_position=ms;ShowResults();Notify();}}
 void TimingFocusLost(int) override{} // widget focus changes do not end the session; app deactivation does
 bool TimingKey(int key,bool down,int ms,bool control,bool shift) override {
  if(!Is39SessionActive())return false;
  if(key!='F'&&key!='J'&&key!='D'&&key!='K')return false;
  if(down&&(control||shift))return false;
  if(session.Key(key,down,ms)){if(down)++rhythm_serial;Notify();}return true;
 }
 void Get39Overlay(std::vector<Timing39Overlay>& out,int ms) const override {
  for(int lane=0;lane<2;++lane)for(auto const& b:session.Preview(lane,ms))out.push_back({b.start,b.end,lane,{},b.gap,false});
 }
 void Commit() override{CommitResults(false);}
};
}
std::unique_ptr<AudioTimingController> Create39TimingController(agi::Context* c){return agi::make_unique<AudioTimingController39>(c);}
