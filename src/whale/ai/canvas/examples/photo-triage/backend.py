# SPDX-FileCopyrightText: 2026 Orcinus contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Backend of the example tool "Photo Triage": reads image sizes and generation settings
# (PNG text chunks as written by ComfyUI, or a .txt next to the image) and writes XMP sidecars
# with the ratings. One JSON request per line on stdin, one answer per line on stdout.

import json
import os
import struct
import sys


def png_meta(path):
    try:
        with open(path, "rb") as handle:
            data = handle.read(1000000)
    except OSError as error:
        return None, {}, str(error)
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        return None, {}, None
    dims = struct.unpack(">II", data[16:24])
    texts = {}
    offset = 8
    while offset + 8 <= len(data):
        length = struct.unpack(">I", data[offset:offset + 4])[0]
        kind = data[offset + 4:offset + 8]
        payload = data[offset + 8:offset + 8 + length]
        if kind == b"tEXt" and b"\x00" in payload:
            key, value = payload.split(b"\x00", 1)
            texts[key.decode(errors="ignore")] = value.decode(errors="ignore")
        if kind == b"IDAT":
            break
        offset += 12 + length
    return dims, texts, None


def jpeg_meta(path):
    try:
        with open(path, "rb") as handle:
            data = handle.read(200000)
    except OSError as error:
        return None, {}, str(error)
    if not data.startswith(b"\xff\xd8"):
        return None, {}, None
    offset = 2
    while offset + 9 < len(data):
        if data[offset] != 0xFF:
            break
        marker = data[offset + 1]
        length = struct.unpack(">H", data[offset + 2:offset + 4])[0]
        if 0xC0 <= marker <= 0xCF and marker not in (0xC4, 0xC8, 0xCC):
            height, width = struct.unpack(">HH", data[offset + 5:offset + 9])
            return (width, height), {}, None
        offset += 2 + length
    return None, {}, None


def settings_from(texts):
    info = {}
    try:
        nodes = json.loads(texts.get("prompt", ""))
    except (ValueError, TypeError):
        return info
    for node in nodes.values():
        inputs = node.get("inputs", {}) if isinstance(node, dict) else {}
        if "seed" in inputs:
            info["seed"] = inputs.get("seed")
            info["steps"] = inputs.get("steps")
            info["cfg"] = inputs.get("cfg")
            info["sampler"] = inputs.get("sampler_name")
            break
    return info


def prompt_text(path, texts):
    sidecar = os.path.splitext(path)[0] + ".txt"
    if os.path.isfile(sidecar):
        try:
            with open(sidecar, encoding="utf-8", errors="ignore") as handle:
                return handle.read(1500).strip()
        except OSError:
            pass
    raw = texts.get("prompt", "")
    return raw if not raw.startswith("{") else ""


TOOLKIT = "Orcinus Photo Triage"


def own_sidecar(path):
    """Whether the XMP sidecar at path is missing or one this tool wrote (others are kept)."""
    if not os.path.exists(path):
        return True
    try:
        with open(path, encoding="utf-8", errors="ignore") as handle:
            return TOOLKIT in handle.read(4096)
    except OSError:
        return False


def xmp(rating):
    return (
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\" x:xmptk=\"" + TOOLKIT + "\">\n"
        " <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
        "  <rdf:Description rdf:about=\"\" xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\">\n"
        "   <xmp:Rating>%d</xmp:Rating>\n"
        "  </rdf:Description>\n"
        " </rdf:RDF>\n"
        "</x:xmpmeta>\n" % int(rating)
    )


for line in sys.stdin:
    line = line.strip()
    if not line:
        continue
    request = json.loads(line)
    command = request.get("cmd")
    if command == "meta":
        path = request.get("path", "")
        dims, texts, error = png_meta(path)
        if dims is None:
            dims, texts, error = jpeg_meta(path)
        print(json.dumps({
            "cmd": "meta",
            "id": request.get("id"),
            "dims": list(dims) if dims else None,
            "prompt": prompt_text(path, texts),
            "info": settings_from(texts),
            "error": error,
        }), flush=True)
    elif command == "export":
        written = 0
        skipped = 0
        for path, rating in (request.get("ratings") or {}).items():
            if not rating:
                continue
            sidecar = os.path.splitext(path)[0] + ".xmp"
            # Another program's sidecar (darktable, digiKam, ...) may hold edits: never overwrite it.
            if not own_sidecar(sidecar):
                skipped += 1
                print("kept", sidecar, "(written by another program)", file=sys.stderr)
                continue
            try:
                with open(sidecar, "w", encoding="utf-8") as handle:
                    handle.write(xmp(rating))
                written += 1
            except OSError as error:
                print("skip", path, error, file=sys.stderr)
        print(json.dumps({"cmd": "export", "id": request.get("id"), "written": written, "skipped": skipped}), flush=True)
