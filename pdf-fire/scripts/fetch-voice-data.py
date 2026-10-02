#!/usr/bin/env python3
"""PDF Fire - download the natural-voice data and ONNX Runtime into <work>/deps/kokoro.

Every download is pinned to its SHA-256, so the build gets exactly the files the
released packages were made with. Runs on Linux and Windows (Python 3.8+, no
extra modules).

Result (what Pdf4QtLibGui/CMakeLists.txt expects):
    <work>/deps/kokoro/voice/          kokoro.onnx (fp16), vocab.json, us_gold.json,
                                       us_silver.json, voices/*.bin, LICENSE-*.txt
    <work>/deps/kokoro/onnxruntime-<linux-x64|win-x64>-1.23.2/

<work> is PDFFIRE_WORK_DIR, or the parent folder of this repository.
"""
import hashlib
import json
import os
import shutil
import struct
import sys
import tarfile
import urllib.request
import zipfile
import ast

ORT_VERSION = "1.23.2"
KOKORO = "https://github.com/thewh1teagle/kokoro-onnx/releases/download/model-files-v1.0/"
DOWNLOADS = {
    # file name: (url, sha256)
    "kokoro-v1.0.fp16.onnx": (KOKORO + "kokoro-v1.0.fp16.onnx",
                              "c1610a859f3bdea01107e73e50100685af38fff88f5cd8e5c56df109ec880204"),
    "voices-v1.0.bin": (KOKORO + "voices-v1.0.bin",
                        "bca610b8308e8d99f32e6fe4197e7ec01679264efed0cac9140fe9c29f1fbf7d"),
    "us_gold.json": ("https://raw.githubusercontent.com/hexgrad/misaki/main/misaki/data/us_gold.json",
                     "dc414872a49a28ae6c141463d502fd945f3b2fde040484fdc47d00cc4612686f"),
    "us_silver.json": ("https://raw.githubusercontent.com/hexgrad/misaki/main/misaki/data/us_silver.json",
                       "de8f67be911bb6c659187b4a65fd966b6a30e56350e0f790d763210b053ac475"),
    "config.json": ("https://huggingface.co/hexgrad/Kokoro-82M/resolve/main/config.json",
                    "5abb01e2403b072bf03d04fde160443e209d7a0dad49a423be15196b9b43c17f"),
}
ORT = {
    "linux": ("onnxruntime-linux-x64-%s.tgz" % ORT_VERSION,
              "1fa4dcaef22f6f7d5cd81b28c2800414350c10116f5fdd46a2160082551c5f9b"),
    "win32": ("onnxruntime-win-x64-%s.zip" % ORT_VERSION,
              "0b38df9af21834e41e73d602d90db5cb06dbd1ca618948b8f1d66d607ac9f3cd"),
}
VOICES = ["af_heart", "af_bella", "af_nicole", "am_michael", "am_fenrir", "am_puck", "bf_emma"]
LICENSES = {"LICENSE-kokoro.txt": "Kokoro-82M_LICENSE.txt",
            "LICENSE-misaki.txt": "misaki_LICENSE.txt",
            "LICENSE-kokoro-onnx.txt": "kokoro-onnx_LICENSE.txt"}


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def fetch(url, path, expected):
    if os.path.exists(path) and sha256(path) == expected:
        print("ok      ", os.path.basename(path))
        return
    print("download", url)
    tmp = path + ".part"
    with urllib.request.urlopen(url, timeout=120) as response, open(tmp, "wb") as out:
        shutil.copyfileobj(response, out, 1 << 20)
    actual = sha256(tmp)
    if actual != expected:
        os.remove(tmp)
        sys.exit("checksum mismatch for %s:\n  expected %s\n  got      %s" % (url, expected, actual))
    os.replace(tmp, path)


def npy_data(blob):
    """The raw array data of a .npy file (the voices are float32 510x1x256)."""
    if blob[:6] != b"\x93NUMPY":
        sys.exit("voices-v1.0.bin: not a .npy entry")
    if blob[6] == 1:
        start, length = 10, struct.unpack("<H", blob[8:10])[0]
    else:
        start, length = 12, struct.unpack("<I", blob[8:12])[0]
    header = ast.literal_eval(blob[start:start + length].decode("latin1"))
    if header["descr"] != "<f4" or header["fortran_order"]:
        sys.exit("voices-v1.0.bin: unexpected array format %r" % (header,))
    return blob[start + length:]


def main():
    here = os.path.dirname(os.path.realpath(__file__))
    src = os.path.realpath(os.path.join(here, "..", ".."))
    work = os.environ.get("PDFFIRE_WORK_DIR") or os.path.dirname(src)
    kokoro = os.path.join(work, "deps", "kokoro")
    voice = os.path.join(kokoro, "voice")
    os.makedirs(os.path.join(voice, "voices"), exist_ok=True)

    for name, (url, digest) in DOWNLOADS.items():
        fetch(url, os.path.join(kokoro, name), digest)

    # The model and the dictionaries, as the program loads them
    shutil.copyfile(os.path.join(kokoro, "kokoro-v1.0.fp16.onnx"), os.path.join(voice, "kokoro.onnx"))
    for name in ("us_gold.json", "us_silver.json"):
        shutil.copyfile(os.path.join(kokoro, name), os.path.join(voice, name))
    with open(os.path.join(kokoro, "config.json"), encoding="utf-8") as f:
        vocab = json.load(f)["vocab"]
    with open(os.path.join(voice, "vocab.json"), "w", encoding="utf-8", newline="\n") as f:
        f.write(json.dumps(vocab, ensure_ascii=False))

    # The voices: one raw float32 file each, from the voices archive
    with zipfile.ZipFile(os.path.join(kokoro, "voices-v1.0.bin")) as archive:
        for name in VOICES:
            with open(os.path.join(voice, "voices", name + ".bin"), "wb") as out:
                out.write(npy_data(archive.read(name + ".npy")))

    for target, source in LICENSES.items():
        shutil.copyfile(os.path.join(src, "3rdparty_licenses", source), os.path.join(voice, target))

    # ONNX Runtime for this system
    platform = "win32" if sys.platform == "win32" else "linux"
    archive_name, digest = ORT[platform]
    archive_path = os.path.join(kokoro, archive_name)
    fetch("https://github.com/microsoft/onnxruntime/releases/download/v%s/%s" % (ORT_VERSION, archive_name),
          archive_path, digest)
    folder = archive_name.replace(".tgz", "").replace(".zip", "")
    if not os.path.isdir(os.path.join(kokoro, folder)):
        if archive_name.endswith(".zip"):
            with zipfile.ZipFile(archive_path) as archive:
                archive.extractall(kokoro)
        else:
            with tarfile.open(archive_path) as archive:
                archive.extractall(kokoro)
    print("voice data ready:", voice)
    print("ONNX Runtime:     ", os.path.join(kokoro, folder))


if __name__ == "__main__":
    main()
