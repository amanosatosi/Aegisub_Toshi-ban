"""Same-runner application benchmark; run under Xvfb in GitHub Actions.

Each sample gets a fresh profile and the same on-disk fixture collection. Warm
filesystem caches are intentional; no persistent application cache is used.
Times are monotonic. CPU/RSS samples exclude shutdown. Correctness and graceful
shutdown are covered separately by startup_smoke.py and the native Lua tests.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import time


def records(directory):
    result = []
    decoder = json.JSONDecoder()
    for path in directory.glob("*.json"):
        data = path.read_text(encoding="utf-8", errors="replace")
        offset = 0
        while offset < len(data):
            offset += len(data[offset:]) - len(data[offset:].lstrip())
            try:
                entry, offset = decoder.raw_decode(data, offset)
            except json.JSONDecodeError:  # The last record may still be writing.
                break
            result.append(entry)
    return result


SCENARIOS = ("bundled", "mixed", "dependency-heavy")


def fixtures(root):
    include = root / "include"
    (include / "l0").mkdir(parents=True, exist_ok=True)
    (include / "l0/DependencyControl.lua").write_text(
        "local n=0; for i=1,8000000 do n=n+i%97 end; assert(n>0); return {}\n")
    (include / "indirect.lua").write_text(
        "local parts = {'l0', 'DependencyControl'}; return require(table.concat(parts, '.'))\n")
    for scenario in SCENARIOS:
        folder = root / scenario
        folder.mkdir(parents=True, exist_ok=True)
        if scenario == "bundled":
            continue
        (folder / "000-first.lua").write_text("aegisub.register_macro('First external', '', function() end)\n")
        (folder / "z-last.moon").write_text("aegisub.register_macro 'Last independent', '', -> nil\n")
        count = 4 if scenario == "mixed" else 36
        for index in range(count):
            module = "l0.DependencyControl" if index % 2 == 0 else "indirect"
            (folder / f"dependency-{index:02}.lua").write_text(
                f"require('{module}'); aegisub.register_macro('Dependency {index}', '', function() end)\n")
    return include


class DisplayInitializationFailure(RuntimeError):
    """The process could not connect to the test runner's virtual display."""


def _sample_once(executable, collection, bundled, includes, folder, workers, profiling=False):
    home = folder / "home"
    profile = home / ".aegisub"
    profile.mkdir(parents=True, exist_ok=True)
    config = {
        "App": {"First Start": False, "Language": "en_US", "Auto": {"Check For Updates": False},
                "Mangetsu Renderer Offer Seen": True},
        "Subtitle": {"Provider": "libass"},
        "Path": {"Automation": {"Autoload": "|".join(map(str, (bundled, collection))), "Include": "|".join(map(str, includes))}},
    }
    (profile / "config.json").write_text(json.dumps(config))
    env = dict(os.environ, HOME=str(home), CI="true", AEGISUB_AUTOMATION_WORKERS=str(workers),
               AEGISUB_AUTOMATION_PROFILE="1" if profiling else "0", AEGISUB_AUTOMATION_MEASURE_UI="1")
    started = time.monotonic()
    with (folder / "console.txt").open("w") as console:
        process = subprocess.Popen([str(executable)], env=env, stdout=console, stderr=subprocess.STDOUT)
        try:
            peak_kib = 0
            cpu_ms = 0
            visible_ms = None
            captured = False
            while time.monotonic() - started < 120:
                exit_code = process.poll()
                if exit_code is not None:
                    # This is infrastructure, not an Automation worker failure.
                    # Keep genuine app failures fatal and never retry after
                    # application logging/initialization has begun.
                    output = (folder / "console.txt").read_text(encoding="utf-8", errors="replace")
                    if ("Unable to initialize GTK+, is DISPLAY set properly?" in output
                            and not records(profile / "log")):
                        raise DisplayInitializationFailure(
                            f"GTK could not initialize Xvfb (exit {exit_code}): {folder}")
                    raise AssertionError(
                        f"Application exited with code {exit_code}: {folder}; "
                        f"console tail: {output[-1500:]!r}")
                stat = Path(f"/proc/{process.pid}/stat").read_text().rsplit(")", 1)[1].split()
                cpu_ms = (int(stat[11]) + int(stat[12])) * 1000 / os.sysconf("SC_CLK_TCK")
                status = Path(f"/proc/{process.pid}/status").read_text()
                peak_kib = max(peak_kib, int(re.search(r"VmHWM:\s+(\d+)", status)[1]))
                log = records(profile / "log")
                completed = next((r for r in log if r["section"] == "startup/automation"), None)
                if visible_ms is None:
                    visible = subprocess.run(["xdotool", "search", "--onlyvisible", "--pid", str(process.pid),
                                              "--name", "Untitled.*Aegisub"], capture_output=True)
                    if visible.returncode == 0:
                        visible_ms = (time.monotonic() - started) * 1000
                if profiling and not captured and not completed and any(r["section"] == "startup/automation/first_macro" for r in log):
                    time.sleep(0.15)  # Let GTK paint the just-published status controls.
                    subprocess.run(["scrot", "-o", str(folder / "loading.png")], check=True)
                    captured = True
                if completed:
                    break
                time.sleep(0.025)
            else:
                raise AssertionError(f"Autoload timed out: {folder}")
            idle = next(r for r in log if r["section"] == "startup/timing" and "First responsive idle event" in r["message"])
            stamp = lambda r: (r["sec"] + r["usec"] / 1e9 - started) * 1000
            first = next((r for r in log if r["section"] == "startup/automation/first_macro"), completed)
            totals = re.search(r"(\d+) / (\d+), failures: (\d+), ([\d.eE+-]+) ms", completed["message"])
            assert totals, completed
            assert totals[1] == totals[2], completed
            expected = sum(p.suffix in (".lua", ".moon") for path in (bundled, collection) for p in path.iterdir())
            assert int(totals[2]) == expected, (expected, completed)
            assert int(totals[3]) == 0, completed
            scripts = []
            for record in log:
                if record["section"] == "startup/automation/script":
                    match = re.match(r"(.*): ([\d.eE+-]+) ms$", record["message"])
                    if match:
                        scripts.append({"file": Path(match[1]).name, "ms": float(match[2])})
            if profiling:
                # Screenshots belong to separate diagnostic runs; their delay
                # and screen capture overhead never enter the timing samples.
                time.sleep(2.3)
                subprocess.run(["scrot", "-o", str(folder / "completed.png")], check=True)
            sections = lambda name: [r for r in log if r["section"] == "startup/automation/" + name]
            phases = {}
            for entry in sections("phase"):
                name = entry["message"].split(";", 1)[0]
                phases[name] = round(stamp(entry), 3)
            publication = sections("publication")
            if not phases:
                # The old loader logs individual files before each publication.
                # Attribute their visibility to the ensuing batch notification.
                bundled_names = {p.name for p in bundled.iterdir()}
                pending = []
                for entry in log:
                    message = entry["message"]
                    if entry["section"] == "startup/automation/script" and "; publication_wait_ms=" in message:
                        name = Path(message.split("; publication_wait_ms=", 1)[0]).name
                        pending.append("bundled" if name in bundled_names else
                                       "dependency_control" if name.startswith("dependency-") else "external")
                    elif entry["section"] == "startup/automation/publication":
                        for name in pending:
                            phases[name] = round(stamp(entry), 3)
                        pending.clear()
            blocking = sum(float(value) for r in publication
                           for value in re.findall(r"(?:registration|notifications)_ms=([\d.eE+-]+)", r["message"]))
            blocking += sum(float(r["message"]) for r in sections("ui_request"))
            return {"phases_ms": phases, "publication_batches": len(publication),
                    "menu_rebuilds": len(sections("menu")), "blocking_ms": round(blocking, 3),
                    "longest_ui_gap_ms": max(float(r["message"]) for r in sections("ui_gap")),
                    "responsive_ms": round(max(visible_ms or 0, stamp(idle)), 3),
                    "first_macro_ms": round(stamp(first), 3) if expected else None,
                    "all_ms": round(stamp(completed), 3), "autoload_ms": float(totals[4]),
                    "cpu_ms": cpu_ms, "peak_rss_mib": round(peak_kib / 1024, 3),
                    "slowest": sorted(scripts, key=lambda s: -s["ms"])[:5],
                    "diagnostics": [r for r in log if r["section"].startswith("startup/automation")],
                    "menu": [r for r in log if r["section"] == "startup/automation/menu"]}
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


def sample(executable, collection, bundled, includes, folder, workers, profiling=False):
    """Retry only a pre-initialization GTK/Xvfb failure, never an app crash.

    Each attempt uses a new process and measurement clock. Failed display
    starts remain in the artifact, outside the successful timing sample.
    """
    for attempt in range(3):
        try:
            result = _sample_once(executable, collection, bundled, includes, folder, workers, profiling)
            result["display_retries"] = attempt
            return result
        except DisplayInitializationFailure as exc:
            failed_console = folder / f"console-display-failure-{attempt + 1}.txt"
            shutil.copy2(folder / "console.txt", failed_console)
            print("AUTOMATION_DISPLAY_RETRY " + json.dumps({
                "sample": folder.name, "attempt": attempt + 1,
                "reason": str(exc), "console": str(failed_console)
            }), flush=True)
            if attempt == 2:
                raise AssertionError(
                    f"GTK/Xvfb failed to initialize on all 3 attempts: {folder}; "
                    f"see retained console-display-failure files") from exc
            time.sleep(0.25 * (attempt + 1))
    raise AssertionError(f"Unexpected display retry state: {folder}")


def main(args):
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    source = args.source.resolve()
    includes = (fixtures(output / "fixtures"), source / "automation/include")
    bundled = args.bundled_dir.resolve()
    observations = []
    variants = [("baseline", args.baseline.resolve(), 2)] if args.baseline else []
    variants.append(("candidate", args.candidate.resolve(), 2))
    # Alternate variants within each sample to reduce warm-cache/run-order bias.
    for scenario in SCENARIOS:
        for repeat in range(args.repeats):
            ordered = variants if repeat % 2 == 0 else list(reversed(variants))
            for version, executable, workers in ordered:
                name = f"{scenario}-{version}-{workers}-{repeat}"
                result = sample(executable, output / "fixtures" / scenario, bundled, includes,
                                output / name, workers)
                if args.baseline and version == "candidate":
                    phase_order = ["bundled"] if scenario == "bundled" else ["bundled", "external", "dependency_control"]
                    assert list(result["phases_ms"]) == phase_order, result
                    assert result["publication_batches"] == len(phase_order), result
                    assert [result["phases_ms"][p] for p in phase_order] == sorted(result["phases_ms"].values()), result
                observations.append(dict(scenario=scenario, version=version, workers=workers, repeat=repeat, **result))
                print("AUTOMATION_SAMPLE " + json.dumps({k: v for k, v in observations[-1].items() if k not in ("diagnostics", "menu")}), flush=True)
                (output / "samples.json").write_text(json.dumps(observations, indent=2))
    lines = ["|Collection|Version|Responsive ms|First macro ms|Bundled available ms|Independent available ms|Dependency available ms|All ms|Main-thread work ms|Longest UI gap ms|Registration batches|Menu rebuilds|",
             "|---|---|" + "|".join(["---:"] * 10) + "|"]
    for scenario in SCENARIOS:
        for version, _, workers in variants:
            group = [r for r in observations if (r["scenario"], r["version"], r["workers"]) == (scenario, version, workers)]
            def median(values):
                values = [value for value in values if value is not None]
                return f"{statistics.median(values):.1f}" if values else "—"
            values = [median([r[key] for r in group]) for key in ("responsive_ms", "first_macro_ms")]
            values += [median([r["phases_ms"].get(phase) for r in group])
                       for phase in ("bundled", "external", "dependency_control")]
            values += [median([r[key] for r in group]) for key in
                       ("all_ms", "blocking_ms", "longest_ui_gap_ms", "publication_batches", "menu_rebuilds")]
            lines.append(f"|{scenario}|{version}|" + "|".join(values) + "|")
    report = "\n".join(lines) + "\n\n"
    report += ("Phase availability is measured from process launch at the last publication for that group. "
               "Main-thread work sums registration, observer notifications and worker-requested UI calls. "
               "The longest gap is measured by a 10 ms timer while loading and includes scheduler delay. "
               "DependencyControl fixtures exercise a controlled, CPU-heavy module with direct and indirect imports.\n")
    print(report, flush=True)
    (output / "summary.md").write_text(report)
    if os.getenv("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as summary:
            summary.write(report)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--bundled-dir", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--source", type=Path, default=Path.cwd())
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repeats", type=int, default=3)
    main(parser.parse_args())
