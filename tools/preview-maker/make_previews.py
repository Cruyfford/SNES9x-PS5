#!/usr/bin/env python3
"""Convert game preview videos (MP4/MKV/WEBM/AVI) into the .ogv clips that
SNES9x PS5 Edition plays on its home screen.

The output file keeps the input name, so name your videos exactly like the
ROMs (e.g. "Super Metroid (Japan, USA) (En,Ja).mp4") and copy the results to
    /dev_hdd0/game/SNES95000/USRDIR/art/video/

Needs ffmpeg with libtheora on PATH.

  GUI:  python make_previews.py
  CLI:  python make_previews.py <input folder or file> [output folder] [--seconds 30]
"""
import argparse
import shutil
import subprocess
import sys
from pathlib import Path

VIDEO_EXTS = {".mp4", ".mkv", ".webm", ".avi", ".mov", ".m4v", ".flv"}


def ffmpeg_cmd(src: Path, dst: Path, seconds: int, quality: int) -> list:
    # 320 px wide is plenty for SNES footage and decodes quickly on the PS3.
    return [
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
        "-i", str(src),
        "-t", str(seconds),
        "-vf", "scale='min(320,iw)':-2:flags=neighbor,fps=30",
        "-c:v", "libtheora", "-q:v", str(quality),
        "-an",
        str(dst),
    ]


def find_inputs(path: Path):
    if path.is_file():
        return [path]
    return sorted(p for p in path.iterdir() if p.suffix.lower() in VIDEO_EXTS)


def convert_all(inputs, out_dir: Path, seconds: int, quality: int, log=print):
    out_dir.mkdir(parents=True, exist_ok=True)
    ok = 0
    for i, src in enumerate(inputs, 1):
        dst = out_dir / (src.stem + ".ogv")
        log(f"[{i}/{len(inputs)}] {src.name}")
        r = subprocess.run(ffmpeg_cmd(src, dst, seconds, quality), capture_output=True, text=True)
        if r.returncode == 0 and dst.exists():
            ok += 1
            log(f"    -> {dst.name}  ({dst.stat().st_size // 1024} KB)")
        else:
            log(f"    failed: {r.stderr.strip().splitlines()[-1] if r.stderr.strip() else 'unknown error'}")
    log(f"Done: {ok} of {len(inputs)} converted into {out_dir}")
    return ok


def check_ffmpeg():
    if not shutil.which("ffmpeg"):
        return "ffmpeg was not found. Install it and make sure it is on PATH."
    r = subprocess.run(["ffmpeg", "-hide_banner", "-encoders"], capture_output=True, text=True)
    if "libtheora" not in r.stdout:
        return "This ffmpeg build has no libtheora encoder. Use a full build (e.g. gyan.dev 'full' on Windows)."
    return None


def run_gui():
    from PySide6.QtCore import QThread, Signal
    from PySide6.QtWidgets import (QApplication, QFileDialog, QHBoxLayout, QLabel, QLineEdit,
                                   QPlainTextEdit, QPushButton, QSpinBox, QVBoxLayout, QWidget)

    class Worker(QThread):
        line = Signal(str)

        def __init__(self, inputs, out_dir, seconds, quality):
            super().__init__()
            self.args = (inputs, out_dir, seconds, quality)

        def run(self):
            convert_all(*self.args, log=self.line.emit)

    app = QApplication(sys.argv)
    w = QWidget()
    w.setWindowTitle("SNES9x PS5 — Preview Maker")
    w.resize(720, 480)
    w.setStyleSheet("""
        QWidget { background: #0b0e17; color: #e8eaf2; font-size: 14px; }
        QLineEdit, QSpinBox, QPlainTextEdit { background: #161a28; border: 1px solid #2a3046; border-radius: 8px; padding: 6px; }
        QPushButton { background: #2f7cf6; border: 0; border-radius: 18px; padding: 8px 18px; font-weight: 600; }
        QPushButton:disabled { background: #2a3046; color: #7a8099; }
        QLabel#hint { color: #8a90a8; }
    """)
    lay = QVBoxLayout(w)
    title = QLabel("Preview videos for the home screen")
    title.setStyleSheet("font-size: 20px; font-weight: 700;")
    lay.addWidget(title)
    hint = QLabel("Name each video like its ROM. The .ogv files go to USRDIR/art/video/ on the PS3.")
    hint.setObjectName("hint")
    hint.setWordWrap(True)
    lay.addWidget(hint)

    def folder_row(label):
        row = QHBoxLayout()
        row.addWidget(QLabel(label))
        edit = QLineEdit()
        row.addWidget(edit, 1)
        btn = QPushButton("Browse")
        btn.clicked.connect(lambda: edit.setText(QFileDialog.getExistingDirectory(w, label) or edit.text()))
        row.addWidget(btn)
        lay.addLayout(row)
        return edit

    src_edit = folder_row("Videos folder")
    out_edit = folder_row("Output folder")
    opts = QHBoxLayout()
    opts.addWidget(QLabel("Clip length (s)"))
    secs = QSpinBox(); secs.setRange(5, 120); secs.setValue(30); opts.addWidget(secs)
    opts.addWidget(QLabel("Quality (0-10)"))
    qual = QSpinBox(); qual.setRange(0, 10); qual.setValue(6); opts.addWidget(qual)
    opts.addStretch(1)
    go = QPushButton("Convert")
    opts.addWidget(go)
    lay.addLayout(opts)
    log = QPlainTextEdit(); log.setReadOnly(True)
    lay.addWidget(log, 1)

    err = check_ffmpeg()
    if err:
        log.appendPlainText(err)
        go.setEnabled(False)

    state = {}

    def start():
        src = Path(src_edit.text().strip())
        if not src.exists():
            log.appendPlainText("Choose the folder with your videos first.")
            return
        out = Path(out_edit.text().strip() or (src / "ogv"))
        inputs = find_inputs(src)
        if not inputs:
            log.appendPlainText("No video files found in that folder.")
            return
        go.setEnabled(False)
        t = Worker(inputs, out, secs.value(), qual.value())
        t.line.connect(log.appendPlainText)
        t.finished.connect(lambda: go.setEnabled(True))
        state["t"] = t
        t.start()

    go.clicked.connect(start)
    w.show()
    sys.exit(app.exec())


def main():
    if len(sys.argv) == 1:
        try:
            run_gui()
            return
        except ImportError:
            print("PySide6 is not installed: pip install PySide6  (or use the command line)\n")
            print(__doc__)
            return
    ap = argparse.ArgumentParser(description="Make .ogv preview clips for SNES9x PS5 Edition")
    ap.add_argument("input", type=Path, help="video file or folder of videos")
    ap.add_argument("output", type=Path, nargs="?", help="output folder (default: <input>/ogv)")
    ap.add_argument("--seconds", type=int, default=30, help="clip length (default 30)")
    ap.add_argument("--quality", type=int, default=6, help="Theora quality 0-10 (default 6)")
    a = ap.parse_args()
    err = check_ffmpeg()
    if err:
        sys.exit(err)
    inputs = find_inputs(a.input)
    if not inputs:
        sys.exit("No video files found.")
    out = a.output or ((a.input if a.input.is_dir() else a.input.parent) / "ogv")
    convert_all(inputs, out, a.seconds, a.quality)


if __name__ == "__main__":
    main()
