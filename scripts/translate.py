#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 ProjectHax LLC
"""
scripts/translate.py: keep nutty's Qt translation catalogs (i18n/*.ts) complete.

1. Runs `lupdate` over src/ (C++ tr() and QML qsTr()) into i18n/nutty_<lang>.ts,
   plus a plural-only i18n/nutty_en.ts for English "%n item(s)" forms.
2. Sends every unfinished message to `claude -p` (Sonnet, low effort) in
   batches, languages in parallel, and writes back only the translations that
   keep every %1/%n placeholder, & mnemonic, HTML tag and the exact number of
   plural forms Qt expects for that language.

Usage:
  python3 scripts/translate.py                # lupdate + translate everything new
  python3 scripts/translate.py --update-only  # just lupdate
  python3 scripts/translate.py --check        # release gate: exit 1 if a catalog is
                                              # stale or has unfinished messages
  python3 scripts/translate.py --lang de,fr   # only these languages
  python3 scripts/translate.py --force        # re-translate finished messages too
  python3 scripts/translate.py --jobs 1       # one language at a time (default 6)

lupdate is found via $LUPDATE, then PATH (lupdate-qt6, lupdate), then the usual
Qt 6 install locations. Qt 6.4 or newer. The release workflow refuses to build a
tag whose catalogs are incomplete (--check). stdlib-only; no pip dependencies.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
I18N_DIR = REPO_ROOT / "i18n"
SOURCE_DIR = REPO_ROOT / "src"

MODEL = "claude-sonnet-5-5"
EFFORT = "low"

# File code -> (lupdate target language, name handed to the model).
LANGS = {
    "es": ("es", "Spanish"),
    "fr": ("fr", "French"),
    "de": ("de", "German"),
    "it": ("it", "Italian"),
    "pt": ("pt_PT", "European Portuguese"),
    "pt_BR": ("pt_BR", "Brazilian Portuguese"),
    "nl": ("nl", "Dutch"),
    "sv": ("sv", "Swedish"),
    "pl": ("pl", "Polish"),
    "cs": ("cs", "Czech"),
    "uk": ("uk", "Ukrainian"),
    "ru": ("ru", "Russian"),
    "zh_CN": ("zh_CN", "Simplified Chinese"),
    "zh_TW": ("zh_TW", "Traditional Chinese"),
    "ja": ("ja", "Japanese"),
    "ko": ("ko", "Korean"),
    "id": ("id", "Indonesian"),
    "hi": ("hi", "Hindi"),
    "ar": ("ar", "Arabic"),
    "tr": ("tr", "Turkish"),
    "vi": ("vi", "Vietnamese"),
    "th": ("th", "Thai"),
    "fa": ("fa", "Persian"),
    "el": ("el", "Greek"),
}
RTL = {"ar", "fa"}

# What each of Qt's plural forms means, in Qt's order (qttools numerus rules).
# A language whose form count doesn't match gets generic "form k of N" labels.
_ONE_OTHER = ["singular (n = 1)", "plural (any other n, including 0)"]
_FRENCH = ["singular (n = 0 or 1)", "plural (n >= 2)"]
_SLAVIC = [
    "singular (n % 10 = 1 and n % 100 != 11: 1, 21, 31, ...)",
    "paucal (n % 10 = 2..4 and n % 100 not 12..14: 2-4, 22-24, ...)",
    "plural (everything else: 0, 5-20, 25-30, ...)",
]
_UNIVERSAL = ["the only form (used for every n)"]
PLURAL_FORMS = {
    "en": _ONE_OTHER,
    "fr": _FRENCH,
    "pt": _FRENCH,
    "pt_BR": _FRENCH,
    "pl": [
        "singular (n = 1)",
        "paucal (n % 10 = 2..4 and n % 100 not 12..14: 2-4, 22-24, ...)",
        "plural (everything else: 0, 5-21, 25-31, ...)",
    ],
    "cs": ["singular (n = 1)", "paucal (n = 2..4)", "plural (everything else, including 0)"],
    "ru": _SLAVIC,
    "uk": _SLAVIC,
    "ar": [
        "zero (n = 0)",
        "singular (n = 1)",
        "dual (n = 2)",
        "few (n % 100 = 3..10)",
        "many (n % 100 = 11..99)",
        "other (n % 100 = 0..2 with n >= 100: 100, 101, 102, ...)",
    ],
}
for _code in ("zh_CN", "zh_TW", "ja", "ko", "id", "tr", "vi", "th", "fa"):
    PLURAL_FORMS[_code] = _UNIVERSAL
# Forms that stand for exactly one n, where the language spells the number out
# ("two devices" is a single word in Arabic), so they may drop %n.
SPELLED_OUT_FORMS = {"ar": {0, 1, 2}}

DO_NOT_TRANSLATE = (
    "nutty, NUT, upsd, upsmon, apcupsd, EcoFlow, Home Assistant, Pushover, ntfy, "
    "Discord, Slack, Teams, SMTP, STARTTLS, TLS, HTTP, HTTPS, JSON, CSV, URL, API, "
    "UPS, FSD, UPower, IOKit, logind, polkit, D-Bus, Flatpak, AppImage, KDE, GNOME, "
    "Wayland, X11, Qt, GPL, ProjectHax LLC, units (V, W, Wh, %, °C, ms, s, min, h), "
    "and anything that looks like a file path, host name, command or code"
)

_PLACEHOLDER_RE = re.compile(r"%L?(?:\d{1,2}|n)")
_TAG_RE = re.compile(r"</?([A-Za-z][A-Za-z0-9]*)\b[^>]*>")
_MNEMONIC_RE = re.compile(r"(?<!&)&(?=[^&\s;])(?![A-Za-z]+;|#\d+;)")
_ANSI_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")


# --- lupdate ------------------------------------------------------------------


def find_lupdate() -> str:
    candidates = []
    if os.environ.get("LUPDATE"):
        candidates.append(os.environ["LUPDATE"])
    candidates += [shutil.which("lupdate-qt6") or "", shutil.which("lupdate") or ""]
    if os.environ.get("QT_ROOT_DIR"):
        candidates.append(str(Path(os.environ["QT_ROOT_DIR"]) / "bin" / "lupdate"))
    candidates += [
        "/usr/lib/qt6/bin/lupdate",
        "/usr/lib64/qt6/bin/lupdate",
        "/usr/libexec/qt6/lupdate",
        "/opt/homebrew/opt/qt/bin/lupdate",
        "/usr/local/opt/qt/bin/lupdate",
    ]
    for c in candidates:
        if not c or not Path(c).is_file():
            continue
        out = subprocess.run([c, "-version"], capture_output=True, text=True)
        version = re.search(r"version (\d+)\.(\d+)", out.stdout + out.stderr)
        if version and (int(version[1]), int(version[2])) >= (6, 4):
            return c
    sys.exit("error: no Qt 6.4+ lupdate found; set LUPDATE=/path/to/lupdate")


def run_lupdate(lupdate: str, ts: Path, target_language: str, plural_only: bool = False) -> None:
    """Refresh one catalog from src/. `-locations none` keeps catalogs free of
    line numbers, so they only change when a string does. `-I src` lets lupdate
    follow the "core/..." includes, so a class's tr() strings land in its
    namespaced context (nutty::NutSource), the one Qt looks up at runtime."""
    cmd = [
        lupdate,
        str(SOURCE_DIR),
        "-I", str(SOURCE_DIR),
        "-extensions", "cpp,h,mm,qml,js",
        "-locations", "none",
        "-no-obsolete",
        "-silent",
        "-source-language", "en",
        "-target-language", target_language,
    ]
    if plural_only:
        cmd.append("-pluralonly")
    cmd += ["-ts", str(ts)]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        sys.exit(f"error: lupdate failed for {ts.name}:\n{proc.stdout}{proc.stderr}")


def catalogs() -> dict[str, tuple[Path, str]]:
    """Language code -> (.ts path, lupdate target language), English first."""
    out = {"en": (I18N_DIR / "nutty_en.ts", "en")}
    for code, (target, _) in LANGS.items():
        out[code] = (I18N_DIR / f"nutty_{code}.ts", target)
    return out


def update_all(lupdate: str, codes: list[str]) -> None:
    I18N_DIR.mkdir(exist_ok=True)
    for code in codes:
        ts, target = catalogs()[code]
        run_lupdate(lupdate, ts, target, plural_only=(code == "en"))


# --- the .ts files --------------------------------------------------------------


class Message:
    """One <message>, addressed by its position so duplicates stay distinct."""

    def __init__(self, context: str, el: ET.Element):
        self.context = context
        self.el = el
        self.source = el.findtext("source") or ""
        self.comment = el.findtext("comment") or ""
        self.note = el.findtext("extracomment") or ""
        self.numerus = el.get("numerus") == "yes"
        self.translation = el.find("translation")
        if self.translation is None:
            self.translation = ET.SubElement(el, "translation", type="unfinished")

    @property
    def key(self) -> tuple:
        return (self.context, self.source, self.comment, self.numerus)

    @property
    def finished(self) -> bool:
        return self.translation.get("type") not in ("unfinished", "vanished", "obsolete")

    @property
    def form_count(self) -> int:
        return len(self.translation.findall("numerusform"))

    def current(self):
        if self.numerus:
            return [f.text or "" for f in self.translation.findall("numerusform")]
        return self.translation.text or ""

    def set(self, value) -> None:
        if self.numerus:
            for form, text in zip(self.translation.findall("numerusform"), value):
                form.text = text
        else:
            self.translation.text = value
        self.translation.attrib.pop("type", None)


def load_ts(path: Path) -> tuple[ET.ElementTree, list[Message]]:
    tree = ET.parse(path)
    messages = []
    for ctx in tree.getroot().iter("context"):
        name = ctx.findtext("name") or ""
        for el in ctx.iter("message"):
            messages.append(Message(name, el))
    return tree, messages


def save_ts(tree: ET.ElementTree, path: Path, lupdate: str, target: str, plural_only: bool) -> None:
    ET.indent(tree, space="    ")
    body = ET.tostring(tree.getroot(), encoding="unicode")
    path.write_text('<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE TS>\n' + body + "\n", encoding="utf-8")
    # Let lupdate rewrite it in its own canonical layout, so a translate run and a
    # plain lupdate run produce byte-identical files.
    run_lupdate(lupdate, path, target, plural_only)


# --- validation -------------------------------------------------------------------


def _mnemonics(s: str) -> int:
    return len(_MNEMONIC_RE.findall(s))


def problems(msg: Message, value, forms: int, code: str = "") -> list[str]:
    """Why a proposed translation can't be used (empty list if it can)."""
    texts = value if msg.numerus else [value]
    if msg.numerus:
        if not isinstance(value, list) or len(value) != forms:
            return [f"needs exactly {forms} plural form(s)"]
    if not all(isinstance(t, str) and t.strip() for t in texts):
        return ["empty translation"]
    issues = []
    want_ph = sorted(set(_PLACEHOLDER_RE.findall(msg.source)))
    want_tags = sorted(t.lower() for t in _TAG_RE.findall(msg.source))
    spelled_out = SPELLED_OUT_FORMS.get(code, set()) if msg.numerus else set()
    for i, t in enumerate(texts):
        got_ph = sorted(set(_PLACEHOLDER_RE.findall(t)))
        if i in spelled_out and "%n" not in got_ph:
            got_ph = sorted(set(got_ph) | {"%n"})
        if got_ph != want_ph:
            issues.append(f"placeholders {got_ph} != {want_ph}")
        got_tags = sorted(x.lower() for x in _TAG_RE.findall(t))
        if got_tags != want_tags:
            issues.append(f"HTML tags {got_tags} != {want_tags}")
        if _mnemonics(msg.source) != _mnemonics(t):
            issues.append("the & mnemonic count changed")
        if msg.source.endswith("\n") != t.endswith("\n"):
            issues.append("trailing newline changed")
    return issues


# --- the model ----------------------------------------------------------------------


def form_labels(code: str, count: int) -> list[str]:
    labels = PLURAL_FORMS.get(code, _ONE_OTHER)
    if len(labels) != count:
        return [f"plural form {i + 1} of {count} (Qt's order for this language)" for i in range(count)]
    spelled_out = SPELLED_OUT_FORMS.get(code, set())
    return [f"{label}; may spell the number out instead of %n" if i in spelled_out else label
            for i, label in enumerate(labels)]


def build_prompt(batch: list[tuple[int, Message]], code: str, lang_name: str) -> str:
    items = []
    for idx, m in batch:
        item = {"id": idx, "context": m.context, "source": m.source}
        if m.comment:
            item["disambiguation"] = m.comment
        if m.note:
            item["note"] = m.note
        if m.numerus:
            item["plural_forms"] = form_labels(code, m.form_count)
        items.append(item)
    english = code == "en"
    task = (
        "These are English UI strings with a %n count. Write the proper English singular and plural "
        "forms for each."
        if english
        else f"Translate these English UI strings into {lang_name}."
    )
    return f"""\
You are a professional software localizer working on nutty, a desktop tray app that monitors \
UPS units, power stations and batteries, sends notifications and can sleep, hibernate or shut \
down the computer on low battery. {task}

"context" is the C++ class or QML file the string comes from; "disambiguation" and "note" are \
hints from the developer. Use the natural, concise wording a native {lang_name} desktop app \
uses for buttons, menus, labels and messages, and the platform's usual terms (sleep, hibernate, \
shut down, tray, notification, ...).

Rules (follow every one):
1. Reply with ONLY a JSON object mapping each "id" (as a string) to its translation. No prose, \
no markdown, no code fences.
2. Every id appears exactly once.
3. Keep every placeholder exactly as written: %1, %2, ... (also %L1) and %n. Each must appear in \
your text exactly as many times as needed, never translated or dropped. You may reorder them to \
fit the grammar.
4. An item with "plural_forms" gets a JSON array with exactly one string per listed form, in that \
order. Every form keeps %n (and the other placeholders), except a form marked "may spell the \
number out".
5. A single "&" marks a keyboard mnemonic: keep exactly one "&" before a suitable letter of your \
text. For Chinese, Japanese and Korean, append it as "(&X)" with the original letter instead. \
"&&" is a literal ampersand.
6. Keep HTML tags, entities, leading/trailing whitespace, newlines, "..." / "…" and the end \
punctuation as in the source.
7. Do not translate these names and terms; keep them verbatim: {DO_NOT_TRANSLATE}.

Input:
{json.dumps(items, ensure_ascii=False, indent=1)}
"""


def call_model(prompt: str) -> str:
    # A throwaway working directory keeps the project's own settings and
    # CLAUDE.md out of the request; --tools "" makes it a plain completion.
    with tempfile.TemporaryDirectory(prefix="nutty-translate-") as cwd:
        proc = subprocess.run(
            ["claude", "-p", "--model", MODEL, "--effort", EFFORT, "--tools", "",
             "--no-session-persistence", "--output-format", "text"],
            input=prompt, capture_output=True, text=True, encoding="utf-8", timeout=600, cwd=cwd,
        )
    if proc.returncode != 0:
        raise RuntimeError(f"claude exited {proc.returncode}: {(proc.stderr or proc.stdout).strip()[:500]}")
    return _ANSI_RE.sub("", proc.stdout)


def parse_response(raw: str) -> dict:
    text = raw.strip()
    candidates = [text]
    fenced = re.search(r"```(?:json)?\s*(\{.*\})\s*```", text, re.DOTALL)
    if fenced:
        candidates.append(fenced.group(1))
    start, end = text.find("{"), text.rfind("}")
    if start != -1 and end > start:
        candidates.append(text[start : end + 1])
    for c in candidates:
        try:
            obj = json.loads(c)
        except json.JSONDecodeError:
            continue
        if isinstance(obj, dict):
            return obj
    return {}


# --- per language -----------------------------------------------------------------------


def special_translation(code: str, msg: Message):
    """Messages answered without the model."""
    if msg.context == "QGuiApplication" and msg.source == "QT_LAYOUT_DIRECTION":
        return "RTL" if code in RTL else "LTR"
    return None


def translate_lang(code: str, lupdate: str, batch_size: int, force: bool, retries: int = 2) -> bool:
    path, target = catalogs()[code]
    lang_name = "English" if code == "en" else LANGS[code][1]
    tree, messages = load_ts(path)
    todo: list[tuple[int, Message]] = []
    changed = False
    for idx, m in enumerate(messages):
        special = special_translation(code, m)
        if special is not None:
            if m.current() != special or not m.finished:
                m.set(special)
                changed = True
            continue
        if force or not m.finished or problems(m, m.current(), m.form_count, code):
            todo.append((idx, m))
    if not todo:
        if changed:
            save_ts(tree, path, lupdate, target, code == "en")
        print(f"  [{code}] up to date ({len(messages)} messages)")
        return True

    batches = [todo[i : i + batch_size] for i in range(0, len(todo), batch_size)]
    print(f"  [{code}] {lang_name}: {len(todo)} message(s) in {len(batches)} batch(es)")
    complete = True
    for n, batch in enumerate(batches, 1):
        pending = dict(batch)
        for attempt in range(1, retries + 2):
            try:
                reply = parse_response(call_model(build_prompt(list(pending.items()), code, lang_name)))
            except Exception as e:  # noqa: BLE001 - report, then retry or skip
                print(f"    batch {n}/{len(batches)} attempt {attempt}: {e}")
                continue
            for idx, m in list(pending.items()):
                value = reply.get(str(idx))
                if m.numerus and m.form_count == 1 and isinstance(value, str):
                    value = [value]  # one plural form: a bare string is unambiguous
                issues = problems(m, value, m.form_count, code) if value is not None else ["missing from the reply"]
                if issues:
                    if attempt > retries:
                        print(f"    [{code}] keeping {m.source[:60]!r} unfinished: {'; '.join(issues)}")
                    continue
                m.set(value)
                del pending[idx]
            if not pending:
                break
        complete = complete and not pending
        save_ts(tree, path, lupdate, target, code == "en")  # incremental: survives an interrupt
        tree, messages = load_ts(path)
        # Re-resolve the remaining batches against the reloaded tree.
        for later in batches[n:]:
            for i, (idx, _) in enumerate(later):
                later[i] = (idx, messages[idx])
    print(f"  [{code}] saved {path.relative_to(REPO_ROOT)}")
    return complete


# --- main -------------------------------------------------------------------------------


def check(lupdate: str) -> int:
    """Exit status for the release gate: catalogs in sync with src/ and complete."""
    failed = False
    with tempfile.TemporaryDirectory(prefix="nutty-i18n-check-") as tmp:
        for code, (path, target) in catalogs().items():
            if not path.exists():
                print(f"{path.name} is missing")
                failed = True
                continue
            fresh = Path(tmp) / path.name
            shutil.copy(path, fresh)
            run_lupdate(lupdate, fresh, target, plural_only=(code == "en"))
            # Compare the messages, not the bytes: lupdate's layout varies a little
            # between Qt versions.
            _, messages = load_ts(path)
            _, current = load_ts(fresh)
            if [m.key for m in messages] != [m.key for m in current]:
                print(f"{path.name} is out of sync with the source strings")
                failed = True
            bad = [m for m in messages if not m.finished or problems(m, m.current(), m.form_count, code)]
            if bad:
                print(f"{path.name} has {len(bad)} unfinished message(s), e.g. {bad[0].source[:60]!r}")
                failed = True
    if failed:
        print("Run: python3 scripts/translate.py, then commit i18n/.")
        return 1
    print(f"all {len(catalogs())} catalogs are up to date and complete.")
    return 0


def main() -> None:
    ap = argparse.ArgumentParser(description="Update and translate nutty's Qt catalogs via claude -p.")
    ap.add_argument("--lang", default="all", help='comma-separated codes (e.g. "de,pt_BR"), or "all"')
    ap.add_argument("--batch-size", type=int, default=40, dest="batch_size")
    ap.add_argument("--jobs", type=int, default=6, help="languages translated in parallel")
    ap.add_argument("--force", action="store_true", help="re-translate finished messages too")
    ap.add_argument("--update-only", action="store_true", dest="update_only", help="only run lupdate")
    ap.add_argument("--check", action="store_true", help="exit 1 if any catalog is stale or unfinished")
    args = ap.parse_args()

    lupdate = find_lupdate()
    if args.check:
        sys.exit(check(lupdate))

    known = list(catalogs())
    if args.lang == "all":
        codes = known
    else:
        codes = [c.strip() for c in args.lang.split(",") if c.strip()]
        unknown = [c for c in codes if c not in known]
        if unknown:
            sys.exit(f"error: unknown language(s) {', '.join(unknown)}; known: {', '.join(known)}")

    update_all(lupdate, codes)
    print(f"updated {len(codes)} catalog(s) in {I18N_DIR.relative_to(REPO_ROOT)}/")
    if args.update_only:
        return
    if not shutil.which("claude"):
        sys.exit("error: the claude CLI isn't on PATH")

    sys.stdout.reconfigure(line_buffering=True)
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        runs = [pool.submit(translate_lang, code, lupdate, args.batch_size, args.force) for code in codes]
        results = [r.result() for r in runs]
    if not all(results):
        print("some messages are still unfinished; run the script again to retry them.")
        sys.exit(1)


if __name__ == "__main__":
    main()
