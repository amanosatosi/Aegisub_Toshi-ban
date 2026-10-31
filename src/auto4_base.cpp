// Copyright (c) 2006, Niels Martin Hansen
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//   * Neither the name of the Aegisub Group nor the names of its contributors
//     may be used to endorse or promote products derived from this software
//     without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Aegisub Project http://www.aegisub.org/

#include "auto4_base.h"

#include "ass_file.h"
#include "ass_style.h"
#include "compat.h"
#include "dialog_progress.h"
#include "include/aegisub/context.h"
#include "options.h"
#include "string_codec.h"
#include "subs_controller.h"

#include <libaegisub/dispatch.h>
#include <libaegisub/format.h>
#include <libaegisub/fs.h>
#include <libaegisub/path.h>
#include <libaegisub/log.h>
#include <libaegisub/make_unique.h>
#include <libaegisub/split.h>

#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/scope_exit.hpp>
#include <chrono>
#include <exception>
#include <future>
#include <mutex>
#include <set>
#include <thread>
#include <utility>

#include <wx/dcmemory.h>
#include <wx/filefn.h>
#include <wx/log.h>
#include <wx/sizer.h>
#include <wx/thread.h>

#ifdef __WINDOWS__
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <libaegisub/charset_conv_win.h>
#endif

namespace Automation4 {
	namespace {
		thread_local std::shared_ptr<ScriptLoadContext> load_context;
		std::atomic<unsigned> script_execution_count{0};
	}

	ScriptExecutionGuard::ScriptExecutionGuard() { ++script_execution_count; }
	ScriptExecutionGuard::~ScriptExecutionGuard() { --script_execution_count; }

	std::shared_ptr<ScriptLoadContext> GetScriptLoadContext() { return load_context; }

	bool ScriptLoadOnMain(std::function<void()> task) {
		if (wxIsMainThread()) { task(); return true; }
		auto context = load_context;
		if (!context) {
			agi::dispatch::Main().Sync(std::move(task));
			return true;
		}
		if (context->cancelled) return false;
		auto finished = std::make_shared<std::promise<void>>();
		auto future = finished->get_future();
		agi::dispatch::Main().Async([context, finished, task = std::move(task)] {
			try {
				if (!context->cancelled) task();
				finished->set_value();
			}
			catch (...) { finished->set_exception(std::current_exception()); }
		});
		while (future.wait_for(std::chrono::milliseconds(20)) != std::future_status::ready)
			if (context->cancelled) return false;
		future.get();
		return !context->cancelled;
	}

	bool CalculateTextExtents(AssStyle *style, std::string const& text, double &width, double &height, double &descent, double &extlead)
	{
#ifndef WIN32
		if (GetScriptLoadContext()) {
			struct Metrics { double w = 0, h = 0, d = 0, e = 0; bool ok = false; };
			auto metrics = std::make_shared<Metrics>();
			auto copy = std::make_shared<AssStyle>(*style);
			if (!ScriptLoadOnMain([copy, text, metrics] {
				metrics->ok = CalculateTextExtents(copy.get(), text, metrics->w, metrics->h, metrics->d, metrics->e);
			})) return false;
			width = metrics->w; height = metrics->h; descent = metrics->d; extlead = metrics->e;
			return metrics->ok;
		}
#endif
		width = height = descent = extlead = 0;

		double fontsize = style->fontsize * 64;
		double spacing = style->spacing * 64;

#ifdef WIN32
		// This is almost copypasta from TextSub
		auto dc = CreateCompatibleDC(nullptr);
		if (!dc) return false;

		SetMapMode(dc, MM_TEXT);

		LOGFONTW lf = {0};
		lf.lfHeight = (LONG)fontsize;
		lf.lfWeight = style->bold ? FW_BOLD : FW_NORMAL;
		lf.lfItalic = style->italic;
		lf.lfUnderline = style->underline;
		lf.lfStrikeOut = style->strikeout;
		lf.lfCharSet = style->encoding;
		lf.lfOutPrecision = OUT_TT_PRECIS;
		lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
		lf.lfQuality = ANTIALIASED_QUALITY;
		lf.lfPitchAndFamily = DEFAULT_PITCH|FF_DONTCARE;
		wcsncpy(lf.lfFaceName, agi::charset::ConvertW(style->font).c_str(), 31);

		auto font = CreateFontIndirect(&lf);
		if (!font) return false;

		auto old_font = SelectObject(dc, font);

		std::wstring wtext(agi::charset::ConvertW(text));
		if (spacing != 0 ) {
			width = 0;
			for (auto c : wtext) {
				SIZE sz;
				GetTextExtentPoint32(dc, &c, 1, &sz);
				width += sz.cx + spacing;
				height = sz.cy;
			}
		}
		else {
			SIZE sz;
			GetTextExtentPoint32(dc, &wtext[0], (int)wtext.size(), &sz);
			width = sz.cx;
			height = sz.cy;
		}

		TEXTMETRIC tm;
		GetTextMetrics(dc, &tm);
		descent = tm.tmDescent;
		extlead = tm.tmExternalLeading;

		SelectObject(dc, old_font);
		DeleteObject(font);
		DeleteObject(dc);

#else // not WIN32
		wxMemoryDC thedc;

		// fix fontsize to be 72 DPI
		//fontsize = -FT_MulDiv((int)(fontsize+0.5), 72, thedc.GetPPI().y);

		// now try to get a font!
		// use the font list to get some caching... (chance is the script will need the same font very often)
		// USING wxTheFontList SEEMS TO CAUSE BAD LEAKS!
		//wxFont *thefont = wxTheFontList->FindOrCreateFont(
		wxFont thefont(
			(int)fontsize,
			wxFONTFAMILY_DEFAULT,
			style->italic ? wxFONTSTYLE_ITALIC : wxFONTSTYLE_NORMAL,
			style->bold ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL,
			style->underline,
			to_wx(style->font),
			wxFONTENCODING_SYSTEM); // FIXME! make sure to get the right encoding here, make some translation table between windows and wx encodings
		thedc.SetFont(thefont);

		wxString wtext(to_wx(text));
		if (spacing) {
			// If there's inter-character spacing, kerning info must not be used, so calculate width per character
			// NOTE: Is kerning actually done either way?!
			for (auto const& wc : wtext) {
				int a, b, c, d;
				thedc.GetTextExtent(wc, &a, &b, &c, &d);
				double scaling = fontsize / (double)(b > 0 ? b : 1); // semi-workaround for missing OS/2 table data for scaling
				width += (a + spacing)*scaling;
				height = b > height ? b*scaling : height;
				descent = c > descent ? c*scaling : descent;
				extlead = d > extlead ? d*scaling : extlead;
			}
		} else {
			// If the inter-character spacing should be zero, kerning info can (and must) be used, so calculate everything in one go
			wxCoord lwidth, lheight, ldescent, lextlead;
			thedc.GetTextExtent(wtext, &lwidth, &lheight, &ldescent, &lextlead);
			double scaling = fontsize / (double)(lheight > 0 ? lheight : 1); // semi-workaround for missing OS/2 table data for scaling
			width = lwidth*scaling; height = lheight*scaling; descent = ldescent*scaling; extlead = lextlead*scaling;
		}
#endif

		// Compensate for scaling
		width = style->scalex / 100 * width / 64;
		height = style->scaley / 100 * height / 64;
		descent = style->scaley / 100 * descent / 64;
		extlead = style->scaley / 100 * extlead / 64;

		return true;
	}

	ExportFilter::ExportFilter(std::string const& name, std::string const& description, int priority)
	: AssExportFilter(name, description, priority)
	{
	}

	std::string ExportFilter::GetScriptSettingsIdentifier()
	{
		return inline_string_encode(GetName());
	}

	wxWindow* ExportFilter::GetConfigDialogWindow(wxWindow *parent, agi::Context *c) {
		config_dialog = GenerateConfigDialog(parent, c);

		if (config_dialog) {
			std::string const& val = c->ass->Properties.automation_settings[GetScriptSettingsIdentifier()];
			if (!val.empty())
				config_dialog->Unserialise(val);
			return config_dialog->CreateWindow(parent);
		}

		return nullptr;
	}

	void ExportFilter::LoadSettings(bool is_default, agi::Context *c) {
		if (config_dialog)
			c->ass->Properties.automation_settings[GetScriptSettingsIdentifier()] = config_dialog->Serialise();
	}

	// ProgressSink
	ProgressSink::ProgressSink(agi::ProgressSink *impl, BackgroundScriptRunner *bsr)
	: impl(impl)
	, bsr(bsr)
	, trace_level(OPT_GET("Automation/Trace Level")->GetInt())
	{
	}

	void ProgressSink::ShowDialog(ScriptDialog *config_dialog)
	{
		agi::dispatch::Main().Sync([=] {
			wxDialog w; // container dialog box
			w.SetExtraStyle(wxWS_EX_VALIDATE_RECURSIVELY);
			w.Create(bsr->GetParentWindow(), -1, to_wx(bsr->GetTitle()));
			auto s = new wxBoxSizer(wxHORIZONTAL); // sizer for putting contents in
			wxWindow *ww = config_dialog->CreateWindow(&w); // generate actual dialog contents
			s->Add(ww, 0, wxALL, 5); // add contents to dialog
			w.SetSizerAndFit(s);
			w.SetLayoutAdaptationMode(wxDIALOG_ADAPTATION_MODE_ENABLED);
			w.CenterOnParent();
			w.ShowModal();
		});
	}

	BackgroundScriptRunner::BackgroundScriptRunner(wxWindow *parent, std::string const& title)
	: impl(new DialogProgress(parent, to_wx(title)))
	{
	}

	BackgroundScriptRunner::~BackgroundScriptRunner()
	{
	}

	void BackgroundScriptRunner::Run(std::function<void (ProgressSink*)> task)
	{
		impl->Run([&](agi::ProgressSink *ps) {
			ProgressSink aps(ps, this);
			task(&aps);
		});
	}

	wxWindow *BackgroundScriptRunner::GetParentWindow() const
	{
		return impl.get();
	}

	std::string BackgroundScriptRunner::GetTitle() const
	{
		return from_wx(impl->GetTitle());
	}

	// Script
	Script::Script(agi::fs::path const& filename)
	: filename(filename)
	{
		include_path.emplace_back(filename.parent_path());
		if (auto context = GetScriptLoadContext()) {
			include_path.insert(include_path.end(), context->include_paths.begin(), context->include_paths.end());
			return;
		}

		std::string include_paths = OPT_GET("Path/Automation/Include")->GetString();
		for (auto tok : agi::Split(include_paths, '|')) {
			auto path = config::path->Decode(agi::str(tok));
			if (path.is_absolute() && agi::fs::DirectoryExists(path))
				include_path.emplace_back(std::move(path));
		}
	}

	// ScriptManager
	void ScriptManager::Add(std::unique_ptr<Script> script)
	{
		if (find(scripts.begin(), scripts.end(), script) == scripts.end())
			scripts.emplace_back(std::move(script));

		ScriptsChanged();
	}

	void ScriptManager::Remove(Script *script)
	{
		auto i = find_if(scripts.begin(), scripts.end(), [&](std::unique_ptr<Script> const& s) { return s.get() == script; });
		if (i != scripts.end())
			scripts.erase(i);

		ScriptsChanged();
	}

	void ScriptManager::RemoveAll()
	{
		scripts.clear();
		ScriptsChanged();
	}

	void ScriptManager::Reload(Script *script)
	{
		try {
			script->Reload();
		}
		catch (agi::Exception const& e) {
			wxLogError(_("Failed to reload Automation script '%s':\n%s"), script->GetFilename().wstring(), to_wx(e.GetMessage()));
		}
		catch (std::exception const& e) {
			wxLogError(_("Failed to reload Automation script '%s':\n%s"), script->GetFilename().wstring(), to_wx(e.what()));
		}
		catch (...) {
			wxLogError(_("Failed to reload Automation script '%s':\nUnknown error"), script->GetFilename().wstring());
		}
		ScriptsChanged();
	}

	const std::vector<cmd::Command*>& ScriptManager::GetMacros()
	{
		macros.clear();
		for (auto& script : scripts) {
			std::vector<cmd::Command*> sfs = script->GetMacros();
			copy(sfs.begin(), sfs.end(), back_inserter(macros));
		}
		return macros;
	}

	// Workers own unpublished scripts; only the UI thread touches live registries.
	struct AutoloadScriptManager::Batch {
		std::shared_ptr<ScriptLoadContext> context;
		std::thread worker;
		std::mutex mutex;
		AutoloadProgress progress;
		std::vector<std::unique_ptr<Script>> scripts;
		std::vector<std::string> errors;
		std::atomic<bool> done{false};
		std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();

		Batch(std::vector<agi::fs::path> directories, std::shared_ptr<ScriptLoadContext> context)
		: context(std::move(context)) {
			worker = std::thread([this, directories = std::move(directories)] {
				load_context = this->context;
				BOOST_SCOPE_EXIT_ALL(&) { load_context.reset(); done = true; };
				try {
					std::vector<agi::fs::path> files;
					std::set<agi::fs::path> seen;
					for (auto const& directory : directories) {
						if (this->context->cancelled) return;
						try {
							if (!agi::fs::DirectoryExists(directory)) continue;
							auto dir = agi::fs::Canonicalize(directory);
							if (!seen.insert(dir).second) continue;
							for (auto const& filename : agi::fs::DirectoryIterator(dir, "*.*")) {
								auto file = dir / filename;
								if (ScriptFactory::Supports(file) && agi::fs::FileExists(file)) files.push_back(file);
							}
						}
						catch (agi::Exception const& e) { errors.push_back(directory.string() + ": " + e.GetMessage()); }
						catch (std::exception const& e) { errors.push_back(directory.string() + ": " + e.what()); }
					}
					{
						std::lock_guard<std::mutex> lock(mutex);
						progress.total = files.size();
						progress.failed = errors.size();
						progress.loading = !files.empty();
					}
					// One worker bounds resource usage and preserves directory order.
					for (auto const& file : files) {
						if (this->context->cancelled) return;
						{
							std::lock_guard<std::mutex> lock(mutex);
							progress.filename = file.filename().string();
						}
						auto start = std::chrono::steady_clock::now();
						std::unique_ptr<Script> script;
						std::string error;
						try {
							script = ScriptFactory::CreateFromFile(file, false, false);
							if (script && !script->GetLoadedState()) error = script->GetDescription();
						}
						catch (agi::Exception const& e) { error = e.GetMessage(); }
						catch (std::exception const& e) { error = e.what(); }
						catch (...) { error = "Unknown error"; }
						LOG_I("startup/automation/script") << file.string() << ": "
							<< std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() << " ms";
						if (!error.empty()) errors.push_back(file.string() + ": " + error);
						if (!script) script = agi::make_unique<UnknownScript>(file, error);
						scripts.push_back(std::move(script));
						std::lock_guard<std::mutex> lock(mutex);
						++progress.completed;
						progress.failed = errors.size();
					}
				}
				catch (agi::Exception const& e) { errors.push_back(e.GetMessage()); }
				catch (std::exception const& e) { errors.push_back(e.what()); }
				catch (...) { errors.push_back("Unknown error scanning Automation scripts"); }
			});
		}

		~Batch() {
			context->cancelled = true;
			if (worker.joinable()) worker.join();
		}
	};

	AutoloadScriptManager::AutoloadScriptManager(std::string path)
	: path(std::move(path)), timer(this) {
		Bind(wxEVT_TIMER, &AutoloadScriptManager::OnTimer, this);
	}

	AutoloadScriptManager::~AutoloadScriptManager() {
		timer.Stop();
		batch.reset(); // Cancel and join before application globals are destroyed.
	}

	void AutoloadScriptManager::Reload() {
		if (batch) {
			batch->context->cancelled = true;
			reload_pending = true; // Coalesce rescans; never create unbounded workers.
			return;
		}
		StartReload();
	}

	void AutoloadScriptManager::Reload(Script *) {
		// Global Reload uses the same staged path as Rescan Autoload Dir.
		Reload();
	}

	void AutoloadScriptManager::StartReload() {
		auto context = std::make_shared<ScriptLoadContext>();
		context->paths = std::make_shared<agi::Path>(*config::path);
		for (auto tok : agi::Split(OPT_GET("Path/Automation/Include")->GetString(), '|')) {
			auto include = context->paths->Decode(agi::str(tok));
			if (include.is_absolute()) context->include_paths.push_back(std::move(include));
		}
		std::vector<agi::fs::path> directories;
		for (auto tok : agi::Split(path, '|')) directories.push_back(context->paths->Decode(agi::str(tok)));
		progress = {};
		ProgressChanged(progress);
		batch = agi::make_unique<Batch>(std::move(directories), std::move(context));
		timer.Start(100); // At most ten real progress updates per second.
	}

	void AutoloadScriptManager::OnTimer(wxTimerEvent&) {
		if (!batch) return;
		if (batch->done) {
			if (!reload_pending && script_execution_count) return;
			timer.Stop();
			if (reload_pending) {
				batch.reset(); // Discard every result of the cancelled generation.
				reload_pending = false;
				StartReload();
				return;
			}
			{
				std::lock_guard<std::mutex> lock(batch->mutex);
				progress = batch->progress;
			}
			progress.loading = false;
			progress.failed = batch->errors.size();
			// Delete old features before publishing replacements with the same names.
			scripts.clear();
			scripts = std::move(batch->scripts);
			for (auto& script : scripts) {
				bool loaded = script->GetLoadedState();
				script->Publish();
				if (loaded && !script->GetLoadedState()) batch->errors.push_back(script->GetFilename().string() + ": " + script->GetDescription());
			}
			errors = batch->errors;
			progress.failed = errors.size();
			for (auto const& error : errors) LOG_E("automation/autoload") << error;
			LOG_I("startup/automation") << progress.completed << " / " << progress.total
				<< ", failures: " << progress.failed << ", "
				<< std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - batch->started).count() << " ms";
			batch.reset();
			ScriptsChanged();
			ProgressChanged(progress);
			return;
		}
		AutoloadProgress next;
		{
			std::lock_guard<std::mutex> lock(batch->mutex);
			next = batch->progress;
		}
		if (next.completed != progress.completed || next.total != progress.total || next.filename != progress.filename) {
			progress = std::move(next);
			ProgressChanged(progress);
		}
	}

	LocalScriptManager::LocalScriptManager(agi::Context *c)
	: context(c)
	, file_open_connection(c->subsController->AddFileOpenListener(&LocalScriptManager::Reload, this))
	{
		AddScriptChangeListener(&LocalScriptManager::SaveLoadedList, this);
	}

	void LocalScriptManager::Reload()
	{
		bool was_empty = scripts.empty();
		scripts.clear();

		auto const& local_scripts = context->ass->Properties.automation_scripts;
		if (local_scripts.empty()) {
			if (!was_empty)
				ScriptsChanged();
			return;
		}

		auto autobasefn(OPT_GET("Path/Automation/Base")->GetString());

		for (auto tok : agi::Split(local_scripts, '|')) {
			tok = boost::trim_copy(tok);
			if (boost::size(tok) == 0) continue;
			char first_char = tok[0];
			std::string trimmed(begin(tok) + 1, end(tok));

			agi::fs::path basepath;
			if (first_char == '~') {
				basepath = context->subsController->Filename().parent_path();
			} else if (first_char == '$') {
				basepath = autobasefn;
			} else if (first_char == '/') {
			} else {
				wxLogWarning("Automation Script referenced with unknown location specifier character.\nLocation specifier found: %c\nFilename specified: %s",
					first_char, to_wx(trimmed));
				continue;
			}
			auto sfname = basepath/trimmed;
			if (agi::fs::FileExists(sfname)) {
				try {
					if (auto script = Automation4::ScriptFactory::CreateFromFile(sfname, true))
						scripts.emplace_back(std::move(script));
				}
				catch (agi::Exception const& e) {
					wxLogError(_("Failed to load Automation script '%s':\n%s"), sfname.wstring(), to_wx(e.GetMessage()));
				}
				catch (std::exception const& e) {
					wxLogError(_("Failed to load Automation script '%s':\n%s"), sfname.wstring(), to_wx(e.what()));
				}
				catch (...) {
					wxLogError(_("Failed to load Automation script '%s':\nUnknown error"), sfname.wstring());
				}
			}
			else {
				wxLogWarning("Automation Script referenced could not be found.\nFilename specified: %c%s\nSearched relative to: %s\nResolved filename: %s",
					first_char, to_wx(trimmed), basepath.wstring(), sfname.wstring());
			}
		}

		ScriptsChanged();
	}

	void LocalScriptManager::SaveLoadedList()
	{
		// Store Automation script data
		// Algorithm:
		// 1. If script filename has Automation Base Path as a prefix, the path is relative to that (ie. "$")
		// 2. Otherwise try making it relative to the ass filename
		// 3. If step 2 failed, or absolute path is shorter than path relative to ass, use absolute path ("/")
		// 4. Otherwise, use path relative to ass ("~")
		std::string scripts_string;
		agi::fs::path autobasefn(OPT_GET("Path/Automation/Base")->GetString());

		for (auto& script : GetScripts()) {
			if (!scripts_string.empty())
				scripts_string += "|";

			auto scriptfn(script->GetFilename().string());
			auto autobase_rel = context->path->MakeRelative(scriptfn, autobasefn);
			auto assfile_rel = context->path->MakeRelative(scriptfn, "?script");

			if (autobase_rel.string().size() <= scriptfn.size() && autobase_rel.string().size() <= assfile_rel.string().size()) {
				scriptfn = "$" + autobase_rel.generic_string();
			} else if (assfile_rel.string().size() <= scriptfn.size() && assfile_rel.string().size() <= autobase_rel.string().size()) {
				scriptfn = "~" + assfile_rel.generic_string();
			} else {
				scriptfn = "/" + script->GetFilename().generic_string();
			}

			scripts_string += scriptfn;
		}
		context->ass->Properties.automation_scripts = std::move(scripts_string);
	}

	// ScriptFactory
	ScriptFactory::ScriptFactory(std::string engine_name, std::string filename_pattern)
	: engine_name(std::move(engine_name))
	, filename_pattern(std::move(filename_pattern))
	{
	}

	void ScriptFactory::Register(std::unique_ptr<ScriptFactory> factory)
	{
		if (find(Factories().begin(), Factories().end(), factory) != Factories().end())
			throw agi::InternalError("Automation 4: Attempt to register the same script factory multiple times. This should never happen.");

		Factories().emplace_back(std::move(factory));
	}

	std::unique_ptr<Script> ScriptFactory::CreateFromFile(agi::fs::path const& filename, bool complain_about_unrecognised, bool create_unknown)
	{
		for (auto& factory : Factories()) {
			auto s = factory->Produce(filename);
			if (s) {
				if (!GetScriptLoadContext()) s->Publish();
				if (!s->GetLoadedState() && complain_about_unrecognised) {
					wxLogError(_("Failed to load Automation script '%s':\n%s"), filename.wstring(), s->GetDescription());
				}
				return s;
			}
		}

		if (complain_about_unrecognised) {
			wxLogError(_("The file was not recognised as an Automation script: %s"), filename.wstring());
		}

		return create_unknown ? agi::make_unique<UnknownScript>(filename) : nullptr;
	}

	std::vector<std::unique_ptr<ScriptFactory>>& ScriptFactory::Factories()
	{
		static std::vector<std::unique_ptr<ScriptFactory>> factories;
		return factories;
	}

	const std::vector<std::unique_ptr<ScriptFactory>>& ScriptFactory::GetFactories()
	{
		return Factories();
	}

	bool ScriptFactory::Supports(agi::fs::path const& filename) {
		for (auto const& factory : Factories())
			for (auto pattern : agi::Split(factory->GetFilenamePattern(), ','))
				if (wxMatchWild(to_wx(agi::str(pattern)).Lower(), to_wx(filename.filename().string()).Lower())) return true;
		return false;
	}

	std::string ScriptFactory::GetWildcardStr()
	{
		std::string fnfilter, catchall;
		for (auto& fact : Factories()) {
			if (fact->GetEngineName().empty() || fact->GetFilenamePattern().empty())
				continue;

			std::string filter(fact->GetFilenamePattern());
			boost::replace_all(filter, ",", ";");
			fnfilter += agi::format("%s scripts (%s)|%s|", fact->GetEngineName(), fact->GetFilenamePattern(), filter);
			catchall += filter + ";";
		}
		fnfilter += from_wx(_("All Files")) + " (*.*)|*.*";

		if (!catchall.empty())
			catchall.pop_back();

		if (Factories().size() > 1)
			fnfilter = from_wx(_("All Supported Formats")) + "|" + catchall + "|" + fnfilter;

		return fnfilter;
	}

	std::string UnknownScript::GetDescription() const {
		return error.empty() ? from_wx(_("File was not recognized as a script")) : error;
	}
}
