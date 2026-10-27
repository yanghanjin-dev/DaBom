#!/usr/bin/env python3
"""Find Cursor Agents Window UI Font Size persistence."""
import os
import re
import sqlite3
import sys

roots = [
    os.path.expandvars(r"%APPDATA%\Cursor"),
    os.path.expandvars(r"%LOCALAPPDATA%\Cursor"),
    os.path.expanduser(r"~\.cursor"),
]
install = os.path.expandvars(r"%LOCALAPPDATA%\Programs\cursor")

text_pat = re.compile(
    rb"uiFontSize|UIFontSize|ui_font_size|agentsUiFont|agentUiFont|"
    rb"glass\.[A-Za-z0-9_.]*[Ff]ont|appearance\.[A-Za-z0-9_.]*[Ff]ont|"
    rb"codeFontSize|ui\.font\.size|FontSize",
    re.I,
)
row_pat = re.compile(
    r"uiFontSize|UIFontSize|uiFont|FontSize|fontSize|appearance|glass\.|"
    r"agentsUi|codeFontSize|ui\.font|composerFont",
    re.I,
)


def scan_sqlite(path: str) -> None:
    try:
        con = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
    except Exception as e:
        print("SQLERR", path, e)
        return
    cur = con.cursor()
    try:
        tables = [
            r[0]
            for r in cur.execute(
                "SELECT name FROM sqlite_master WHERE type='table'"
            ).fetchall()
        ]
    except Exception as e:
        print("SQLERR tables", path, e)
        con.close()
        return
    for t in tables:
        try:
            for row in cur.execute(f"SELECT * FROM [{t}]"):
                blob = " | ".join("" if v is None else str(v) for v in row)
                if row_pat.search(blob):
                    print("SQLHIT", path, "table=", t)
                    print("  ROW:", blob[:800].replace("\n", " "))
        except Exception:
            pass
    con.close()


def scan_bytes(path: str) -> None:
    try:
        data = open(path, "rb").read()
    except Exception:
        return
    if not text_pat.search(data):
        return
    print("BITHIT", path, "size=", len(data))
    for m in re.finditer(
        rb".{0,50}(uiFontSize|UIFontSize|uiFont|FontSize|fontSize|"
        rb"glass\.[A-Za-z0-9_.]*[Ff]ont|appearance\.[A-Za-z0-9_.]*|"
        rb"codeFontSize)[^\x00]{0,120}",
        data,
        re.I,
    ):
        print(" ", m.group(0).decode("utf-8", "replace")[:240])


def walk_data() -> None:
    for root in roots:
        if not os.path.isdir(root):
            print("MISSING", root)
            continue
        print("WALK", root)
        for dirpath, _, files in os.walk(root):
            for name in files:
                path = os.path.join(dirpath, name)
                try:
                    sz = os.path.getsize(path)
                except OSError:
                    continue
                if sz == 0 or sz > 80_000_000:
                    continue
                low = name.lower()
                if low.endswith((".vscdb", ".sqlite", ".db")):
                    scan_sqlite(path)
                elif low.endswith(
                    (".json", ".log", ".txt", ".vscdb-wal", ".ldb", ".log")
                ) or "prefer" in low or name in ("Local State", "Preferences"):
                    scan_bytes(path)


def scan_install_strings() -> None:
    if not os.path.isdir(install):
        print("NO INSTALL", install)
        return
    print("SCAN INSTALL", install)
    # Focus on js bundles that might define the setting
    targets = []
    for dirpath, _, files in os.walk(os.path.join(install, "resources", "app")):
        for name in files:
            if name.endswith((".js", ".json", ".css")):
                path = os.path.join(dirpath, name)
                try:
                    sz = os.path.getsize(path)
                except OSError:
                    continue
                if 0 < sz < 40_000_000:
                    targets.append(path)
    print("js/json/css count", len(targets))
    key_pat = re.compile(
        rb"uiFontSize|UI Font Size|agents?\s*UI\s*Font|glass\.uiFont|"
        rb"appearance\.uiFont|composer\.uiFontSize|glass\.fontSize|"
        rb"uiFontSizeMinimum|defaultUiFontSize|UI_FONT_SIZE",
        re.I,
    )
    hits = 0
    for path in targets:
        try:
            data = open(path, "rb").read()
        except Exception:
            continue
        if not key_pat.search(data):
            continue
        hits += 1
        print("INSTHIT", path)
        for m in re.finditer(
            rb".{0,80}(uiFontSize|UI Font Size|glass\.[A-Za-z0-9_.]*[Ff]ont|"
            rb"appearance\.[A-Za-z0-9_.]*[Ff]ont|defaultUiFontSize|"
            rb"UI_FONT_SIZE|uiFontSizeMinimum)[^\x00]{0,160}",
            data,
            re.I,
        ):
            print(" ", m.group(0).decode("utf-8", "replace")[:300])
        if hits >= 30:
            print("...truncated install hits")
            break
    print("install hits", hits)


if __name__ == "__main__":
    walk_data()
    scan_install_strings()
    print("DONE")
