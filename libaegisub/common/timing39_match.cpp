// Copyright (c) 2026, JibunSenyou contributors. ISC license.
#include <libaegisub/timing39_session.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <numeric>
#include <sstream>

namespace agi { namespace timing39 {
namespace {
bool Legal(Analysis const& a, size_t first, size_t count) {
	if(first>=a.graph.size()) return false;
	for(auto const& edge:a.graph[first]) if(edge.count==count) return true;
	return false;
}
bool Partition(Analysis const& a, std::vector<TimingAssignment> const& assignments) {
	size_t position=0;
	for(auto const& x:assignments) {
		if(x.first_mora!=position || !Legal(a,x.first_mora,x.mora_count)) return false;
		position+=x.mora_count;
	}
	return position==a.morae.size() && position>0;
}
}

void TimingLane::Begin(int start,int end) {
	Clear(); checkpoint_start=start; checkpoint_end=std::max(start,end); gap_start=start; enabled=true;
}
bool TimingLane::Owns(int key) const {return key==keys[0] || key==keys[1];}
bool TimingLane::KeyDown(int key,int ms) {
	if(!enabled || !Owns(key) || ms<checkpoint_start || ms>=checkpoint_end) return false;
	int index=key==keys[0]?0:1;
	if(held[index]) return false;
	// Reject seeks backwards instead of making overlapping raw capture.
	if(!blocks.empty() && ms<blocks.back().end) {Finish(blocks.back().end);return false;}
	if(owner>=0) blocks.back().end=ms;
	else if(ms>gap_start) blocks.push_back({gap_start,ms,true});
	held[index]=true; owner=index; blocks.push_back({ms,ms,false}); return true;
}
bool TimingLane::KeyUp(int key,int ms) {
	if(!Owns(key)) return false;
	int index=key==keys[0]?0:1;
	bool was_held=held[index]; held[index]=false;
	if(owner!=index) return was_held;
	blocks.back().end=std::max(blocks.back().start,std::min(ms,checkpoint_end));
	gap_start=blocks.back().end; owner=-1; return true;
}
void TimingLane::Finish(int ms) {
	ms=std::max(checkpoint_start,std::min(ms,checkpoint_end));
	if(owner>=0) {blocks.back().end=std::max(blocks.back().start,ms);gap_start=blocks.back().end;}
	else if(enabled && !blocks.empty() && ms>gap_start) {blocks.push_back({gap_start,ms,true});gap_start=ms;}
	owner=-1; held={{false,false}}; enabled=false;
}
void TimingLane::Clear() {blocks.clear();held={{false,false}};owner=-1;enabled=false;}
std::vector<TimingBlock> TimingLane::Preview(int ms) const {
	return Preview(ms,std::numeric_limits<int>::min(),std::numeric_limits<int>::max());

}
std::vector<TimingBlock> TimingLane::Preview(int ms,int visible_start,int visible_end) const {
	std::vector<TimingBlock> result;
	VisitPreview(ms,visible_start,visible_end,[&](TimingBlock const& block){result.push_back(block);});
	return result;
}

MatchResult Match(Analysis const& a,std::vector<TimingBlock> const& blocks,ScoringModel const& weights) {
	MatchResult result; result.language_certain=a.language_certain;
	if(!a.error.empty()) {result.reason=a.error;return result;}
	std::vector<size_t> sung;
	double total_duration=0;
	int end=0;
	for(size_t i=0;i<blocks.size();++i) {
		auto const& b=blocks[i];
		if(b.start<0 || b.end<b.start || (i&&b.start<end) || (!b.gap && b.end==b.start)) {result.reason="Invalid or unfinished raw timing block";return result;}
		end=b.end;
		if(!b.gap) {sung.push_back(i);total_duration+=b.end-b.start;}
	}
	size_t m=a.morae.size(),n=sung.size();
	if(!n || n>m) {result.reason="Captured block count cannot cover the base morae";return result;}
	// Bound worst-case memory for pathological input. Ordinary lyric lines are
	// tens of morae; this limit prevents an untrusted subtitle freezing the UI.
	if(m>512) {result.reason="Line exceeds the 512-mora analysis limit; split at dialogue checkpoints";return result;}
	double unit=total_duration/m;
	struct Node {double cost=0; size_t previous=unknown, edge=0, position=0; Score score;};
	std::vector<Node> nodes(1);
	using Cell=std::vector<size_t>;
	std::vector<Cell> current(m+1),next(m+1); current[0].push_back(0);
	size_t keep=std::max<size_t>(2,std::min<size_t>(weights.alternatives,16));
	for(size_t used=0;used<n;++used) {
		for(auto& cell:next) cell.clear();
		for(size_t pos=0;pos<m;++pos) for(auto previous:current[pos]) {
			for(size_t e=0;e<a.graph[pos].size();++e) {
				auto const& edge=a.graph[pos][e];size_t to=pos+edge.count;
				if(to>m || m-to<n-used-1) continue;
				Score score;
				score.prior=weights.join_prior[static_cast<size_t>(edge.features.type)];
				if(edge.count>1) score.language=(edge.features.uncertain_language?weights.unknown_language:0) +
					(!edge.features.same_reading_span&&!edge.features.same_lexeme?weights.different_span:0) +
					(edge.features.source_supported?weights.source_support:0) +
					(edge.features.strength==CandidateStrength::Soft?weights.soft_candidate:0);
				double duration=blocks[sung[used]].end-blocks[sung[used]].start;
				// Log-ratio is scale independent and soft: singing is not metronomic.
				score.duration=weights.duration_fit*std::pow(std::log(duration/(unit*edge.count)),2);
				double cost=nodes[previous].cost+score.Total();
				auto& cell=next[to];
				if(cell.size()==keep && cost>=nodes[cell.back()].cost) continue;
				nodes.push_back({cost,previous,e,pos,score});size_t index=nodes.size()-1;
				auto at=std::upper_bound(cell.begin(),cell.end(),cost,[&](double x,size_t y){return x<nodes[y].cost;});cell.insert(at,index);
				if(cell.size()>keep) cell.pop_back();
			}
		}
		current.swap(next);
	}
	for(auto terminal:current[m]) {
		MatchPath path;path.cost=nodes[terminal].cost;
		for(size_t used=n;used>0;--used) {
			auto const& node=nodes[terminal];auto const& edge=a.graph[node.position][node.edge];
			path.assignments.push_back({sung[used-1],edge.first,edge.count});path.scores.push_back(node.score);terminal=node.previous;
		}
		std::reverse(path.assignments.begin(),path.assignments.end());std::reverse(path.scores.begin(),path.scores.end());result.paths.push_back(std::move(path));
	}
	if(result.paths.empty()) {result.reason="No supported candidate path for this captured count; raw timing is preserved";return result;}
	result.margin=result.paths.size()>1?result.paths[1].cost-result.paths[0].cost:std::numeric_limits<double>::infinity();
	result.confidence=result.margin<weights.ambiguity_margin?Confidence::Yellow:Confidence::Green;
	result.reason=result.confidence==Confidence::Yellow?"Several legal paths remain close after language and duration scoring":"Clear preferred legal assignment";
	if(result.confidence==Confidence::Yellow) {
		auto has_boundary=[](MatchPath const& path,size_t boundary) {
			return std::any_of(path.assignments.begin(),path.assignments.end(),[&](TimingAssignment const& x){return x.first_mora+x.mora_count==boundary;});
		};
		for(size_t boundary=1;boundary<m;++boundary) {
			bool first=has_boundary(result.paths[0],boundary),disputed=false;
			for(size_t i=1;i<result.paths.size() && result.paths[i].cost-result.paths[0].cost<weights.ambiguity_margin;++i)
				disputed|=has_boundary(result.paths[i],boundary)!=first;
			if(disputed)result.ambiguous_mora_boundaries.push_back(boundary);
		}
		for(size_t i=1;i<result.paths.size() && result.paths[i].cost-result.paths[0].cost<weights.ambiguity_margin;++i)
			for(size_t j=1;j<n;++j) if(result.paths[i].assignments[j].first_mora!=result.paths[0].assignments[j].first_mora) result.uncertain_boundaries.push_back(j);
		std::sort(result.uncertain_boundaries.begin(),result.uncertain_boundaries.end());
		result.uncertain_boundaries.erase(std::unique(result.uncertain_boundaries.begin(),result.uncertain_boundaries.end()),result.uncertain_boundaries.end());
	}
	return result;
}

bool ValidAssignments(Analysis const& a,std::vector<TimingBlock> const& blocks,std::vector<TimingAssignment> const& assignments) {
	if(!Partition(a,assignments)) return false;
	size_t n=0;
	for(size_t i=0;i<blocks.size();++i) if(!blocks[i].gap) {
		if(n>=assignments.size() || assignments[n++].timing_block!=i) return false;
	}
	return n==assignments.size();
}

void AssignmentEditor::Reset(std::vector<TimingAssignment> assignments) {history={std::move(assignments)};cursor=0;corrections.clear();}
std::vector<TimingAssignment> const& AssignmentEditor::Get() const {
	static std::vector<TimingAssignment> const empty;
	return history.empty()?empty:history[cursor];
}
bool AssignmentEditor::Choose(Analysis const& a,std::vector<TimingAssignment> assignments) {
	if(!Partition(a,assignments) || assignments.size()!=Get().size()) return false;
	for(size_t i=0;i<assignments.size();++i) if(assignments[i].timing_block!=Get()[i].timing_block) return false;
	if(assignments==Get()) return false;
	corrections.push_back({Get(),assignments,unknown,"selected legal alternative"});
	history.resize(cursor+1);history.push_back(std::move(assignments));++cursor;return true;
}
bool AssignmentEditor::Move(Analysis const& a,size_t divider,int delta) {
	auto edited=Get();
	if(!divider || divider>=edited.size() || (delta!=1&&delta!=-1)) return false;
	auto& left=edited[divider-1];auto& right=edited[divider];
	if((delta<0&&left.mora_count<=1)||(delta>0&&right.mora_count<=1)) return false;
	left.mora_count+=delta;right.first_mora+=delta;right.mora_count-=delta;
	if(!Choose(a,std::move(edited))) return false;
	corrections.back().boundary=divider;corrections.back().reason="assignment boundary moved; media timestamps unchanged";return true;
}
bool AssignmentEditor::Undo() {if(!cursor) return false;--cursor;return true;}
bool AssignmentEditor::Redo() {if(cursor+1>=history.size()) return false;++cursor;return true;}

bool MoraPlacementDraft::Push(State next) {
	if(!ValidManualAssignments(next.analysis,blocks,next.assignments))return false;
	history.resize(cursor+1);history.push_back(std::move(next));++cursor;return true;
}
void MoraPlacementDraft::Begin(Analysis const& analysis,std::vector<TimingBlock> const& capture,
	std::vector<TimingAssignment> const& initial) {
	blocks=capture;cursor=0;history.clear();
	State state;state.analysis=analysis;state.latin_cuts=analysis.manual_latin_cuts;
	if(ValidManualAssignments(analysis,blocks,initial))state.assignments=initial;
	else {
		std::vector<size_t> sung;for(size_t i=0;i<blocks.size();++i)if(!blocks[i].gap)sung.push_back(i);
		auto morae=analysis.morae.size();
		for(size_t i=0;i<sung.size()&&morae;++i) {
			auto first=std::min(i,morae-1);
			auto count=i+1==sung.size()&&sung.size()<=morae?morae-first:1;
			state.assignments.push_back({sung[i],first,count});
		}
	}
	history.push_back(std::move(state));
}
bool MoraPlacementDraft::Move(size_t divider,int delta) {
	if(history.empty()||!divider||divider>=Assignments().size()||(delta!=1&&delta!=-1))return false;
	auto next=history[cursor];
	auto& left=next.assignments[divider-1];auto& right=next.assignments[divider];
	if(left.first_mora==right.first_mora || (delta<0&&left.mora_count<=1) ||
		(delta>0&&right.mora_count<=1))return false;
	left.mora_count+=delta;right.first_mora+=delta;right.mora_count-=delta;
	return Push(std::move(next));
}
bool MoraPlacementDraft::Merge(size_t divider) {
	if(history.empty()||!divider||divider>=Assignments().size())return false;
	auto next=history[cursor];
	auto& left=next.assignments[divider-1];auto& right=next.assignments[divider];
	if(left.first_mora==right.first_mora || left.first_mora+left.mora_count!=right.first_mora ||
		(divider>1&&next.assignments[divider-2].first_mora==left.first_mora) ||
		(divider+1<next.assignments.size()&&next.assignments[divider+1].first_mora==right.first_mora))return false;
	auto combined=left.mora_count+right.mora_count;
	left.mora_count=combined;right.first_mora=left.first_mora;right.mora_count=combined;
	return Push(std::move(next));
}
bool MoraPlacementDraft::CutLatin(size_t mora,size_t byte_offset) {
	if(history.empty()||mora>=CurrentAnalysis().morae.size())return false;
	auto next=history[cursor];auto& a=next.analysis;
	auto const& original=a.morae[mora];
	if(byte_offset==0||byte_offset>=original.text.size()||
		!std::all_of(original.text.begin(),original.text.end(),[](unsigned char ch){
			return ch<128&&(std::isalpha(ch)||ch=='\''||ch=='-');}))return false;
	auto old_graph=a.graph;
	BaseMora left=original,right=original;
	left.text=original.text.substr(0,byte_offset);right.text=original.text.substr(byte_offset);
	left.logical_end=original.logical_begin+byte_offset;right.logical_begin=left.logical_end;
	left.reading_end=original.reading_begin+byte_offset;right.reading_begin=left.reading_end;
	a.morae[mora]=left;a.morae.insert(a.morae.begin()+mora+1,right);
	a.boundaries.insert(a.boundaries.begin()+mora+1,{false,"manual Latin segmentation",BoundaryKind::Soft});
	a.graph.assign(a.morae.size(),{});
	for(size_t i=0;i<a.morae.size();++i)
		a.graph[i].push_back({i,1,{Join::Single,false,false,false,false,CandidateStrength::Base},"base mora"});
	for(auto const& edges:old_graph)for(auto edge:edges)if(edge.count>1) {
		bool crosses=edge.first<=mora&&edge.first+edge.count>mora;
		edge.first+=edge.first>mora;
		edge.count+=crosses;
		a.graph[edge.first].push_back(std::move(edge));
	}
	for(auto& mapping:next.assignments) {
		if(mapping.first_mora>mora)++mapping.first_mora;
		else if(mapping.first_mora+mapping.mora_count>mora)++mapping.mora_count;
	}
	next.latin_cuts.push_back(left.logical_end);
	std::sort(next.latin_cuts.begin(),next.latin_cuts.end());
	a.manual_latin_cuts=next.latin_cuts;
	return Push(std::move(next));
}
bool MoraPlacementDraft::RemoveLatinCut(size_t boundary) {
	if(history.empty()||!boundary||boundary>=CurrentAnalysis().morae.size())return false;
	auto next=history[cursor];auto& a=next.analysis;
	auto cut=a.morae[boundary].logical_begin;
	auto found=std::find(next.latin_cuts.begin(),next.latin_cuts.end(),cut);
	if(found==next.latin_cuts.end())return false;
	for(auto const& mapping:next.assignments) {
		bool left=mapping.first_mora<=boundary-1&&mapping.first_mora+mapping.mora_count>boundary-1;
		bool right=mapping.first_mora<=boundary&&mapping.first_mora+mapping.mora_count>boundary;
		if(left!=right)return false;
	}
	auto old_graph=a.graph;
	a.morae[boundary-1].text+=a.morae[boundary].text;
	a.morae[boundary-1].logical_end=a.morae[boundary].logical_end;
	a.morae[boundary-1].reading_end=a.morae[boundary].reading_end;
	a.morae.erase(a.morae.begin()+boundary);
	a.boundaries.erase(a.boundaries.begin()+boundary);
	a.graph.assign(a.morae.size(),{});
	for(size_t i=0;i<a.morae.size();++i)
		a.graph[i].push_back({i,1,{Join::Single,false,false,false,false,CandidateStrength::Base},"base mora"});
	for(auto const& edges:old_graph)for(auto edge:edges)if(edge.count>1) {
		bool crosses=edge.first<boundary&&edge.first+edge.count>boundary;
		edge.first-=edge.first>=boundary;
		edge.count-=crosses;
		if(edge.count>1)a.graph[edge.first].push_back(std::move(edge));
	}
	for(auto& mapping:next.assignments) {
		if(mapping.first_mora>boundary)--mapping.first_mora;
		else if(mapping.first_mora+mapping.mora_count>boundary)--mapping.mora_count;
	}
	next.latin_cuts.erase(found);
	a.manual_latin_cuts=next.latin_cuts;
	return Push(std::move(next));
}
bool MoraPlacementDraft::Undo() {if(!cursor)return false;--cursor;return true;}
bool MoraPlacementDraft::Redo() {if(cursor+1>=history.size())return false;++cursor;return true;}
bool MoraPlacementDraft::Reset() {if(history.empty()||!cursor)return false;cursor=0;return true;}

namespace {
double BoundaryDistance(std::vector<int> const& a,std::vector<int> const& b) {
	if(a.empty()||b.empty()) return std::numeric_limits<double>::infinity();
	auto direction=[](std::vector<int> const& from,std::vector<int> const& to) {
		double sum=0;for(int x:from) {double best=std::numeric_limits<double>::infinity();for(int y:to) best=std::min(best,std::abs(double(x)-y));sum+=best;}return sum/from.size();
	};
	return (direction(a,b)+direction(b,a))/2;
}
}
StyleMatch InferStyles(std::vector<StyleEvidence> const& evidence,double minimum_margin_ms) {
	struct Costs {double p=0,s=0;size_t count=0;};std::map<std::string,Costs> by_style;
	for(auto const& e:evidence) {
		if(e.existing.empty()||e.primary.empty()||e.secondary.empty()) continue;
		auto& cost=by_style[e.style];cost.p+=BoundaryDistance(e.existing,e.primary);cost.s+=BoundaryDistance(e.existing,e.secondary);++cost.count;
	}
	StyleMatch result;double best=std::numeric_limits<double>::infinity(),second=best;
	for(auto const& p:by_style) for(auto const& s:by_style) {
		if(p.first==s.first) continue;
		double cost=p.second.p/p.second.count+s.second.s/s.second.count;
		if(cost<best) {second=best;best=cost;result.primary=p.first;result.secondary=s.first;}
		else second=std::min(second,cost);
	}
	result.cost=best;result.margin=second-best;
	// Absolute fit matters too: unrelated lines must not win merely by being
	// the least bad pair. Both styles need corroborating events.
	result.ambiguous=!std::isfinite(best)||best>160||result.margin<minimum_margin_ms ||
		by_style[result.primary].count<2||by_style[result.secondary].count<2;
	return result;
}

std::string Inspect(Analysis const& a,std::vector<TimingBlock> const& blocks,MatchResult const& result,AssignmentEditor const* editor) {
	std::ostringstream s;
	s<<"SOURCE: "<<a.source<<"\nSPAN SOURCE (karaoke-free): "<<a.span_source<<"\nSURFACE: "<<a.surface<<"\nNORMALIZED: "<<a.reading.normalized<<"\n";
	for(size_t i=0;i<a.spans.size();++i) {auto const& x=a.spans[i];s<<"SPAN "<<i<<" source["<<x.begin<<","<<x.end<<") "<<x.display<<" | "<<x.original_reading<<" reading["<<x.reading_begin<<","<<x.reading_end<<") explicit="<<x.explicit_reading<<"\n";}
	for(size_t i=0;i<a.words.size();++i) {auto const& w=a.words[i];s<<"WORD "<<i<<" ["<<w.reading<<"] "<<w.lemma<<" kind="<<static_cast<int>(w.kind)<<" certain="<<w.certain<<"\n";}
	for(auto const& note:a.language_notes)s<<"LANGUAGE "<<note<<"\n";
	for(size_t i=0;i<a.morae.size();++i) {auto const& m=a.morae[i];s<<"MORA "<<i<<" "<<m.text<<" reading["<<m.reading_begin<<","<<m.reading_end<<") logical["<<m.logical_begin<<","<<m.logical_end<<") span="<<m.span<<" lexeme="<<m.lexeme<<"\n";}
	for(size_t i=1;i<a.morae.size();++i) s<<(a.boundaries[i].fixed?"FIXED ":"CANDIDATE ")<<i<<": "<<a.boundaries[i].reason<<"\n";
	for(auto const& edges:a.graph) for(auto const& e:edges) s<<"EDGE "<<e.first<<"+"<<e.count<<" "<<e.reason<<" strength="<<(e.features.strength==CandidateStrength::Soft?"SOFT":e.features.strength==CandidateStrength::Strong?"STRONG":"BASE")<<" same-lexeme="<<e.features.same_lexeme<<" same-span="<<e.features.same_reading_span<<"\n";
	size_t taps=0;for(size_t i=0;i<blocks.size();++i) {auto const& b=blocks[i];if(!b.gap) ++taps;s<<"BLOCK "<<i<<" "<<b.start<<".."<<b.end<<" duration="<<b.end-b.start<<" gap="<<b.gap<<"\n";}
	s<<"TAPS "<<taps<<" LANGUAGE "<<(result.language_certain?"known":"UNKNOWN/partial")<<" TIMING "<<(result.confidence==Confidence::Green?"GREEN":result.confidence==Confidence::Yellow?"YELLOW":"RED")<<" margin="<<result.margin<<" "<<result.reason<<"\n";
	for(size_t p=0;p<result.paths.size();++p) {
		auto const& path=result.paths[p];s<<"PATH "<<p<<" cost="<<path.cost<<" delta="<<path.cost-result.paths.front().cost<<"\n";
		for(size_t i=0;i<path.assignments.size();++i) {auto const& x=path.assignments[i];auto const& score=path.scores[i];s<<"  block "<<x.timing_block<<" -> mora "<<x.first_mora<<"+"<<x.mora_count<<" [";for(size_t j=0;j<x.mora_count;++j)s<<a.morae[x.first_mora+j].text;s<<"] prior="<<score.prior<<" language="<<score.language<<" duration="<<score.duration<<"\n";}
	}
	if(editor) {
		s<<"CURRENT ASSIGNMENT\n";for(auto const& x:editor->Get())s<<x.timing_block<<" -> "<<x.first_mora<<"+"<<x.mora_count<<"\n";
		for(auto const& correction:editor->corrections) {
			s<<"CORRECTION divider="<<correction.boundary<<" "<<correction.reason<<"\n";
			for(auto const& set:{correction.predicted,correction.corrected}) {for(auto const& x:set)s<<x.timing_block<<":"<<x.first_mora<<"+"<<x.mora_count<<" ";s<<"\n";}
		}
	}
	return s.str();
}
} }
