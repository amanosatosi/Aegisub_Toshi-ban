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


def fixtures(root, source):
    for scenario in ("empty", "small", "large", "cpu", "broken"):
        folder = root / scenario
        folder.mkdir(parents=True, exist_ok=True)
        if scenario == "empty":
            continue
        (folder / "000-fast.lua").write_text("aegisub.register_macro('First macro', '', function() end)\n")
        if scenario in ("small", "large"):
            for path in sorted((source / "automation/autoload").iterdir()):
                if path.suffix in (".lua", ".moon"):
                    shutil.copy2(path, folder / path.name)
        if scenario == "large":
            for index in range(40):
                # Real bundled includes exercise module IO/compilation and state
                # setup; alternating MoonScript tests the existing compiler.
                if index % 4 == 0:
                    script = f"util = require 'aegisub.util'\naegisub.register_macro 'Large {index}', '', ->\n  return nil\n"
                    suffix = "moon"
                else:
                    script = f"require('aegisub.util'); require('aegisub.re'); aegisub.register_macro('Large {index}', '', function() end)\n"
                    suffix = "lua"
                (folder / f"large-{index:02}.{suffix}").write_text(script)
        if scenario == "cpu":
            for index in range(12):
                script = "local n=0; for i=1,16000000 do n=n+i%97 end; assert(n>0); "
                script += f"aegisub.register_macro('CPU {index}', '', function() end)\n"
                (folder / f"cpu-{index:02}.lua").write_text(script)
        if scenario == "broken":
            (folder / "010-invalid.lua").write_text("aegisub.register_macro('Must not publish', '', function() end); error('expected benchmark failure')")
            (folder / "020-invalid.moon").write_text("this is ) invalid moonscript\n")
            (folder / "030-good.moon").write_text("aegisub.register_macro 'Survivor', '', ->\n  return nil\n")
        (folder / "ignored.txt").write_text("error('unsupported file loaded')")


class DisplayInitializationFailure(RuntimeError):
    """The process could not connect to the test runner's virtual display."""


def _sample_once(executable, collection, include, folder, workers, profiling=False):
    home = folder / "home"
    profile = home / ".aegisub"
    profile.mkdir(parents=True, exist_ok=True)
    config = {
        "App": {"First Start": False, "Language": "en_US", "Auto": {"Check For Updates": False},
                "Mangetsu Renderer Offer Seen": True},
        "Subtitle": {"Provider": "libass"},
        "Path": {"Automation": {"Autoload": str(collection), "Include": str(include)}},
    }
    (profile / "config.json").write_text(json.dumps(config))
    env = dict(os.environ, HOME=str(home), CI="true", AEGISUB_AUTOMATION_WORKERS=str(workers),
               AEGISUB_AUTOMATION_PROFILE="1" if profiling else "0")
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
            expected = len([p for p in collection.iterdir() if p.suffix in (".lua", ".moon")])
            assert int(totals[2]) == expected, (expected, completed)
            assert int(totals[3]) == (2 if collection.name == "broken" else 0), completed
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
            return {"responsive_ms": round(max(visible_ms or 0, stamp(idle)), 3),
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


def sample(executable, collection, include, folder, workers, profiling=False):
    """Retry only a pre-initialization GTK/Xvfb failure, never an app crash.

    Each attempt uses a new process and measurement clock. Failed display
    starts remain in the artifact, outside the successful timing sample.
    """
    for attempt in range(3):
        try:
            result = _sample_once(executable, collection, include, folder, workers, profiling)
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
    fixtures(output / "fixtures", source)
    observations = []
    variants = [("baseline", args.baseline.resolve(), 1)] + [("candidate", args.candidate.resolve(), n) for n in (1, 2, 3, 4)]
    # Alternate variants within each sample to reduce warm-cache/run-order bias.
    for scenario in ("empty", "small", "large", "cpu", "broken"):
        for repeat in range(args.repeats):
            ordered = variants if repeat % 2 == 0 else list(reversed(variants))
            for version, executable, workers in ordered:
                name = f"{scenario}-{version}-{workers}-{repeat}"
                result = sample(executable, output / "fixtures" / scenario, source / "automation/include",
                                output / name, workers)
                observations.append(dict(scenario=scenario, version=version, workers=workers, repeat=repeat, **result))
                print("AUTOMATION_SAMPLE " + json.dumps({k: v for k, v in observations[-1].items() if k not in ("diagnostics", "menu")}), flush=True)
                (output / "samples.json").write_text(json.dumps(observations, indent=2))
    for scenario in ("small", "large", "cpu", "broken"):
        result = sample(args.candidate.resolve(), output / "fixtures" / scenario, source / "automation/include",
                        output / f"profile-{scenario}", 2, profiling=True)
        (output / f"phases-{scenario}.json").write_text(json.dumps(result, indent=2))
    lines = ["|Collection|Version|Workers|Responsive ms|First macro ms|All ms|Autoload ms|CPU ms|Peak RSS MiB|Slowest ms|",
             "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|"]
    for scenario in ("empty", "small", "large", "cpu", "broken"):
        for version, _, workers in variants:
            group = [r for r in observations if (r["scenario"], r["version"], r["workers"]) == (scenario, version, workers)]
            values = []
            for key in ("responsive_ms", "first_macro_ms", "all_ms", "autoload_ms", "cpu_ms", "peak_rss_mib"):
                values.append(f"{statistics.median(r[key] for r in group):.1f}" if group[0][key] is not None else "—")
            values.append(f"{statistics.median(r['slowest'][0]['ms'] for r in group):.1f}" if group[0]["slowest"] else "—")
            lines.append(f"|{scenario}|{version}|{workers}|" + "|".join(values) + "|")
    report = "\n".join(lines) + "\n"
    print(report, flush=True)
    (output / "summary.md").write_text(report)
    if os.getenv("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as summary:
            summary.write(report)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--source", type=Path, default=Path.cwd())
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repeats", type=int, default=3)
    main(parser.parse_args())
