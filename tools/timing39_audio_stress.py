"""Generate repeatable audio/ASS input for manual 39 Mode paint profiling.

Usage: python tools/timing39_audio_stress.py OUTPUT_DIRECTORY
Run the generated scenario on both instrumented builds with the same window
size, zoom, audio mode, and playback duration. This does not measure by itself.
"""

import math
from pathlib import Path
import struct
import sys
import wave


def ass_time(milliseconds):
    centiseconds = milliseconds // 10
    hours, centiseconds = divmod(centiseconds, 360000)
    minutes, centiseconds = divmod(centiseconds, 6000)
    seconds, centiseconds = divmod(centiseconds, 100)
    return f"{hours}:{minutes:02}:{seconds:02}.{centiseconds:02}"


def main(destination):
    destination.mkdir(parents=True, exist_ok=True)
    audio_path = destination / "timing39_audio_stress.wav"
    script_path = destination / "timing39_audio_stress.ass"
    rate = 8000
    duration_seconds = 180
    with wave.open(str(audio_path), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(rate)
        for second in range(duration_seconds):
            data = bytearray()
            for sample in range(rate):
                time = second + sample / rate
                level = int(7500 * math.sin(2 * math.pi * (220 + second % 30) * time))
                data.extend(struct.pack("<h", level))
            output.writeframes(data)
    header = """[Script Info]
Title: 39 Mode audio paint stress
ScriptType: v4.00+
PlayResX: 1920
PlayResY: 1080

[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Default,Arial,40,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,0,2,10,10,10,1
Style: Lyric,Arial,40,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,0,2,10,10,10,1

[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
"""
    with script_path.open("w", encoding="utf-8", newline="\n") as output:
        output.write(header)
        for event in range(1800):
            start = event * 100
            end = min(start + 750, duration_seconds * 1000)
            style = "Lyric" if event % 15 == 0 else "Default"
            text = "<世|せ><界|かい>まで" if style == "Lyric" else "reference"
            output.write(
                f"Dialogue: 0,{ass_time(start)},{ass_time(end)},{style},,0,0,0,,{text}\n"
            )
    print(audio_path)
    print(script_path)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: timing39_audio_stress.py OUTPUT_DIRECTORY")
    main(Path(sys.argv[1]))
