# SPDX-FileCopyrightText: 2026 Orcinus contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""Backend of the Sound Check tool (an example of the AI canvas, CV4).

Reads one JSON request per line on stdin, answers with one JSON value per line on stdout:
  {"cmd": "scan", "folder": "/path"}  -> {"type": "file", ...} per WAV file, then {"type": "done"}
  {"cmd": "trim", "path": "/path/x.wav"} -> {"type": "trimmed", ...} or {"type": "error", ...}
Python's standard library only (wave, array): 8, 16 and 32 bit PCM.
"""

import array
import json
import math
import os
import sys
import wave

# Below this level (dBFS) a sample counts as silence; a file starts or ends with "long" silence
# when there is more of it than SILENCE_SECONDS.
SILENCE_DB = -50.0
SILENCE_SECONDS = 0.25
WAVEFORM_POINTS = 200


def send(value):
    print(json.dumps(value), flush=True)


def read_samples(path):
    """@return (samples as floats in -1..1, mixed to mono, sample rate, channels, bits)."""
    with wave.open(path, "rb") as wav:
        channels = wav.getnchannels()
        width = wav.getsampwidth()
        rate = wav.getframerate()
        frames = wav.readframes(wav.getnframes())
    if width == 1:
        data = array.array("B", frames)
        values = [(v - 128) / 128.0 for v in data]
    elif width in (2, 4):
        data = array.array("h" if width == 2 else "i")
        data.frombytes(frames)
        if sys.byteorder == "big":
            data.byteswap()
        scale = float(1 << (8 * width - 1))
        values = [v / scale for v in data]
    else:
        raise ValueError(f"{8 * width} bit samples are not supported")
    if channels > 1:
        values = [max(values[i:i + channels], key=abs) for i in range(0, len(values), channels)]
    return values, rate, channels, 8 * width


def db(level):
    return -120.0 if level <= 0 else max(-120.0, 20.0 * math.log10(level))


def analyse(path):
    samples, rate, channels, bits = read_samples(path)
    count = len(samples)
    threshold = 10 ** (SILENCE_DB / 20.0)
    peak = max((abs(s) for s in samples), default=0.0)
    rms = math.sqrt(sum(s * s for s in samples) / count) if count else 0.0
    # Clipped: samples at (or within a hair of) full scale.
    clipped = sum(1 for s in samples if abs(s) >= 0.999)
    lead = next((i for i, s in enumerate(samples) if abs(s) > threshold), count)
    tail = next((i for i, s in enumerate(reversed(samples)) if abs(s) > threshold), count)
    bucket = max(1, math.ceil(count / WAVEFORM_POINTS))
    waveform = [round(max((abs(s) for s in samples[i:i + bucket]), default=0.0), 3) for i in range(0, count, bucket)]
    return {
        "duration": round(count / rate, 3) if rate else 0,
        "rate": rate,
        "channels": channels,
        "bits": bits,
        "peakDb": round(db(peak), 1),
        "loudnessDb": round(db(rms), 1),
        "clipped": clipped,
        "silenceStart": round(lead / rate, 3) if rate else 0,
        "silenceEnd": round(tail / rate, 3) if rate else 0,
        "longSilence": rate > 0 and (lead / rate > SILENCE_SECONDS or tail / rate > SILENCE_SECONDS) and lead < count,
        "waveform": waveform,
    }


def scan(folder):
    names = sorted((n for n in os.listdir(folder) if n.lower().endswith(".wav") and not n.startswith(".")), key=str.lower)
    for name in names:
        path = os.path.join(folder, name)
        entry = {"type": "file", "name": name, "path": path}
        try:
            entry.update(analyse(path))
        except Exception as error:  # a broken or unsupported file is reported, the others go on
            entry["error"] = str(error)
        send(entry)
    send({"type": "done", "folder": folder, "count": len(names)})


def trim(path):
    """Writes a copy without the silence at start and end into the subfolder trimmed."""
    threshold = 10 ** (SILENCE_DB / 20.0)
    with wave.open(path, "rb") as wav:
        params = wav.getparams()
        frames = wav.readframes(wav.getnframes())
    samples, rate, channels, bits = read_samples(path)
    count = len(samples)
    lead = next((i for i, s in enumerate(samples) if abs(s) > threshold), count)
    tail = next((i for i, s in enumerate(reversed(samples)) if abs(s) > threshold), count)
    if lead >= count:
        send({"type": "error", "path": path, "error": "the file is silent"})
        return
    folder = os.path.join(os.path.dirname(path), "trimmed")
    os.makedirs(folder, exist_ok=True)
    target = os.path.join(folder, os.path.basename(path))
    if os.path.exists(target):
        send({"type": "error", "path": path, "error": f"{target} exists already"})
        return
    frame = params.sampwidth * params.nchannels
    with wave.open(target, "wb") as out:
        out.setparams(params)
        out.writeframes(frames[lead * frame:(count - tail) * frame])
    send({"type": "trimmed", "path": path, "target": target, "removed": round((lead + tail) / rate, 3)})


def main():
    for line in sys.stdin:
        try:
            request = json.loads(line)
            if request.get("cmd") == "scan":
                scan(request["folder"])
            elif request.get("cmd") == "trim":
                trim(request["path"])
            else:
                send({"type": "error", "error": f"unknown request {request!r}"})
        except Exception as error:
            print(f"request failed: {error}", file=sys.stderr, flush=True)
            send({"type": "error", "error": str(error)})


if __name__ == "__main__":
    main()
