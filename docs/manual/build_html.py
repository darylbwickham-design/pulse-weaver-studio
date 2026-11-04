"""Build the offline, printable Pulse Weaver manual from its editable DOCX.

Run with a Python installation that has python-docx:
    python docs/manual/build_html.py

The resulting HTML has no network or asset dependencies, so it can be opened
from either the native installation or the Lumia plugin package.
"""

from __future__ import annotations

from collections import defaultdict
from html import escape
from pathlib import Path
import json
import re
import unicodedata

from docx import Document
from docx.oxml.ns import qn


HERE = Path(__file__).resolve().parent
SOURCE = HERE / "Pulse-Weaver-Studio-Nerds-Introduction.docx"
OUTPUT = HERE / "Pulse-Weaver-Studio-Nerds-Introduction.html"
PAGE_MAP = HERE / "print-page-map.json"
TITLE = "Pulse Weaver Studio: The Nerds Introduction"


def slug(text: str) -> str:
    ascii_text = unicodedata.normalize("NFKD", text).encode("ascii", "ignore").decode()
    return re.sub(r"[^a-z0-9]+", "-", ascii_text.lower()).strip("-") or "section"


def linkify(text: str) -> str:
    parts = []
    position = 0
    for match in re.finditer(r"https?://[^\s<>]+", text):
        value = match.group().rstrip(".,;)")
        parts.append(escape(text[position : match.start()]))
        parts.append(f'<a href="{escape(value, quote=True)}">{escape(value)}</a>')
        position = match.start() + len(value)
    parts.append(escape(text[position:]))
    return "".join(parts).replace("\n", "<br>")


def field_instructions(paragraph) -> list[str]:
    return [(node.text or "").strip() for node in paragraph._p.xpath(".//w:instrText")]


def excluded_from_toc(paragraph) -> bool:
    ppr = paragraph._p.pPr
    outline = ppr.find(qn("w:outlineLvl")) if ppr is not None else None
    return outline is not None and outline.get(qn("w:val")) == "9"


def render() -> None:
    document = Document(SOURCE)
    page_map = json.loads(PAGE_MAP.read_text(encoding="utf-8")) if PAGE_MAP.exists() else {}
    paragraphs = document.paragraphs
    ids: dict[int, str] = {}
    used: dict[str, int] = defaultdict(int)
    headings: list[tuple[int, str, str]] = []
    in_release_records = False
    for i, paragraph in enumerate(paragraphs):
        title = paragraph.text.strip()
        if title == "Appendix C  Published release notes":
            in_release_records = True
        elif title == "Appendix D  Practical tricks and operator checklists":
            in_release_records = False
        style = paragraph.style.name
        if not title or not style.startswith("Heading"):
            continue
        base = slug(title)
        used[base] += 1
        identifier = base if used[base] == 1 else f"{base}-{used[base]}"
        ids[i] = identifier
        level = int(style.split()[-1])
        if level <= 2 and title != "Contents" and not excluded_from_toc(paragraph) and (not in_release_records or title == "Appendix C  Published release notes"):
            headings.append((level, title, identifier))

    glossary: dict[str, str] = {}
    index_targets: dict[str, str] = {}
    index_terms: set[str] = set()
    in_glossary = False
    for i, paragraph in enumerate(paragraphs):
        title = paragraph.text.strip()
        if title == "Appendix A  Glossary":
            in_glossary = True
        elif title == "Appendix B  Source guide and evidence boundaries":
            in_glossary = False
        if in_glossary and title and paragraph.runs and paragraph.runs[0].bold:
            term = paragraph.runs[0].text.rstrip(". ")
            glossary[term] = f"glossary-{slug(term)}"
        for instruction in field_instructions(paragraph):
            xe = re.match(r'XE\s+"([^"]+)"', instruction)
            if xe:
                index_targets.setdefault(xe.group(1), f"index-entry-{slug(xe.group(1))}")
            if instruction.startswith("INDEX "):
                index_terms.update(line.strip() for line in title.splitlines() if line.strip())
    index_terms.update(index_targets)
    index_terms.update(glossary)
    # This historic term appears only in the release-record appendix, so the
    # DOCX has no chapter XE marker for it. Give the print index a real target.
    if "Virtual camera" in index_terms and "Virtual camera" not in index_targets:
        index_targets["Virtual camera"] = "index-entry-virtual-camera"

    content: list[str] = []
    content.append('<div class="screen-tools"><a href="#contents">Contents</a><a href="#appendix-a-glossary">Glossary</a><a href="#appendix-f-index">Index</a><button type="button" onclick="window.print()">Print book</button></div>')
    content.append('<header class="cover"><div class="cover-kicker">PULSE WEAVER STUDIO · REFERENCE MANUAL</div>')
    content.append(f"<h1>{escape(TITLE)}</h1>")
    content.append('<p class="cover-subtitle">A practical guide to the OBS foundation, Stages, Looks, scenes, live operation and release history.</p>')
    content.append('<p class="cover-edition">Reference edition · 4 November 2026</p>')
    content.append('<p class="cover-version">Bundled with Pulse Weaver Studio 2.0.2</p>')
    content.append('<aside class="edition-note"><strong>About this edition.</strong> The operating reference and captured public release history were researched through Pulse Weaver Studio 2.0.1. The 2.0.2 distribution adds this offline manual and Help/About access. Historical release entries remain identified by their original version and publication date.</aside>')
    content.append('</header>')

    in_glossary = False
    glossary_open = False
    in_index = False
    current_heading = ""
    virtual_camera_anchored = False
    for i, paragraph in enumerate(paragraphs):
        raw = paragraph.text.strip()
        if not raw:
            continue
        instructions = field_instructions(paragraph)
        if any(x.startswith("TOC ") or x.startswith("INDEX ") for x in instructions):
            continue
        style = paragraph.style.name
        if style == "Title":
            continue
        # The title-page subtitle and reference note are represented by the
        # dedicated HTML cover above.
        if i < 3 and style == "Normal":
            continue
        if raw == "Appendix A  Glossary":
            in_glossary = True
        if raw == "Appendix B  Source guide and evidence boundaries":
            in_glossary = False
            if glossary_open:
                content.append("</dl>")
                glossary_open = False
        if raw == "Appendix F  Index":
            in_index = True
        if style.startswith("Heading"):
            level = int(style.split()[-1])
            identifier = ids[i]
            if raw == "Contents":
                content.append(f'<section class="contents" id="{identifier}"><h1>Contents</h1>')
                content.append('<p class="toc-note">Page references match the bundled A4 print edition. Select a title to jump to that section.</p><ol class="toc">')
                for depth, text, target in headings:
                    page = page_map.get("headings", {}).get(text)
                    number = f'<span class="toc-number">{page}</span>' if page else ""
                    content.append(f'<li class="toc-level-{depth}"><a href="#{target}">{escape(text)}</a>{number}</li>')
                content.append('</ol></section>')
                continue
            current_heading = identifier
            kind = "release-entry" if re.match(r"^v\d", raw) and level == 2 else ""
            content.append(f'<h{level} id="{identifier}" class="{kind}">{escape(raw)}</h{level}>')
            if in_index:
                content.append('<p class="index-note">Page references match the bundled A4 print edition. Select a number to jump to its discussion or glossary entry.</p><div class="index-grid">')
                for term in sorted(index_terms, key=str.casefold):
                    if term in index_targets or term in glossary:
                        links = []
                        if term in index_targets:
                            page = page_map.get("discussion", {}).get(term)
                            if page:
                                links.append(f'<a href="#{index_targets[term]}" aria-label="{escape(term, quote=True)} discussion, page {page}">{page}</a>')
                        if term in glossary:
                            page = page_map.get("glossary", {}).get(term)
                            if page:
                                links.append(f'<a href="#{glossary[term]}" aria-label="{escape(term, quote=True)} glossary, page {page}">{page}</a>')
                        content.append(f'<div class="index-item"><b>{escape(term)}</b><span>{", ".join(links)}</span></div>')
                content.append('</div>')
                in_index = False
            continue
        if raw.startswith("The contents below includes chapters") or raw.startswith("The index is generated from the marked terms"):
            continue
        if in_glossary and paragraph.runs and paragraph.runs[0].bold:
            if not glossary_open:
                content.append('<dl class="glossary">')
                glossary_open = True
            term = paragraph.runs[0].text.rstrip(". ")
            definition = raw[len(paragraph.runs[0].text) :].strip()
            content.append(f'<div class="glossary-entry" id="{glossary[term]}"><dt>{escape(term)}</dt><dd>{linkify(definition)}</dd></div>')
            continue
        if raw == "Update this field to generate the alphabetical index and page references.":
            continue
        anchors = []
        for instruction in instructions:
            xe = re.match(r'XE\s+"([^"]+)"', instruction)
            if xe and index_targets.get(xe.group(1)):
                anchors.append(f'<span id="{index_targets[xe.group(1)]}"></span>')
        if not virtual_camera_anchored and "virtual camera" in raw.casefold():
            anchors.append('<span id="index-entry-virtual-camera"></span>')
            virtual_camera_anchored = True
        text = "".join(anchors) + linkify(raw)
        if style == "List Bullet":
            content.append(f'<p class="bullet">{text}</p>')
        elif paragraph.runs and any(run.font.name == "Consolas" for run in paragraph.runs):
            content.append(f'<pre>{"".join(anchors)}{escape(raw)}</pre>')
        elif raw.startswith("Sources: ") or raw.startswith("Published record: "):
            content.append(f'<p class="source-note">{text}</p>')
        else:
            content.append(f"<p>{text}</p>")
    if glossary_open:
        content.append("</dl>")

    css = r"""
    :root { color-scheme: light; }
    * { box-sizing: border-box; }
    html { scroll-behavior: smooth; }
    body { margin: 0; color: #222; background: #f0eee9; font-family: Georgia, 'Times New Roman', serif; line-height: 1.48; }
    a { color: #173f62; text-decoration-thickness: 1px; text-underline-offset: .12em; }
    a:hover { color: #9b3b20; }
    main { max-width: 920px; min-height: 100vh; margin: 0 auto; padding: 54px 72px 100px; background: #fffdf8; box-shadow: 0 0 36px #0002; }
    .screen-tools { display: flex; justify-content: flex-end; gap: 18px; align-items: center; font: 13px Arial, sans-serif; margin-bottom: 36px; }
    .screen-tools button { border: 1px solid #516679; border-radius: 3px; padding: 8px 12px; background: #fff; color: #173f62; cursor: pointer; }
    .cover { min-height: 510px; display: flex; flex-direction: column; justify-content: center; border-top: 5px double #232323; border-bottom: 5px double #232323; padding: 45px 0; margin-bottom: 52px; }
    .cover-kicker, .cover-edition, .cover-version { font: 700 12px Arial, sans-serif; letter-spacing: .12em; text-transform: uppercase; }
    .cover h1 { font-size: 51px; line-height: 1.08; margin: 34px 0 22px; border: 0; padding: 0; }
    .cover-subtitle { font-size: 20px; max-width: 28em; color: #4b4b4b; }
    .cover-edition { margin-top: 48px; }
    .cover-version { color: #8a321f; }
    .edition-note { border-left: 3px solid #8a321f; padding: 12px 17px; margin: 24px 0 36px; background: #f7f3ec; font-size: 14px; }
    h1, h2, h3 { line-height: 1.22; color: #161616; }
    h1 { font-size: 28px; border-bottom: 1px solid #5c5c5c; padding-bottom: 10px; margin: 55px 0 22px; }
    h2 { font-size: 20px; margin: 34px 0 14px; }
    h3 { font-size: 16px; margin: 24px 0 10px; }
    p { margin: 0 0 13px; }
    .contents { margin-bottom: 40px; }
    .toc-note, .index-note, .source-note { font-size: 13px; color: #555; }
    .toc { list-style: none; padding: 0; margin: 22px 0; }
    .toc li { display: flex; justify-content: space-between; gap: 1em; padding: 6px 0; border-bottom: 1px dotted #ccc; }
    .toc li.toc-level-1 { font-weight: bold; margin-top: 12px; }
    .toc li.toc-level-2 { padding-left: 25px; font-size: 14px; }
    .toc a { text-decoration: none; }
    .toc-number { font-variant-numeric: tabular-nums; margin-left: auto; }
    .bullet { position: relative; padding-left: 23px; }
    .bullet::before { content: '•'; position: absolute; left: 6px; }
    pre { font: 13px/1.45 Consolas, 'Courier New', monospace; background: #f4f2ee; padding: 12px 14px; border-left: 2px solid #555; white-space: pre-wrap; overflow-wrap: anywhere; margin: 18px 0; }
    .source-note { font-style: italic; overflow-wrap: anywhere; }
    .glossary { margin: 0; }
    .glossary-entry { display: flex; gap: 12px; padding: 5px 0; border-bottom: 1px dotted #ddd; }
    .glossary dt { font-weight: bold; min-width: 145px; }
    .glossary dd { margin: 0; }
    .index-grid { columns: 2; column-gap: 34px; }
    .index-item { break-inside: avoid; display: flex; justify-content: space-between; gap: 7px; border-bottom: 1px dotted #ddd; padding: 5px 0; font-size: 14px; }
    .index-item span { font-size: 12px; white-space: nowrap; }
    .release-entry { border-top: 1px solid #bbb; padding-top: 20px; }
    @page { size: A4; margin: 19mm 18mm 18mm; @top-center { content: 'PULSE WEAVER STUDIO  |  NERDS INTRODUCTION'; font: 8pt Georgia, serif; } @bottom-center { content: 'Page ' counter(page); font: 8pt Georgia, serif; } }
    @media print {
      body { background: white; font-size: 10.5pt; line-height: 1.28; }
      main { max-width: none; min-height: auto; margin: 0; padding: 0; box-shadow: none; background: white; }
      .screen-tools { display: none; }
      .cover { display: block; height: 230mm; min-height: 0; margin: 0; padding: 38mm 0 0; break-after: page; break-inside: avoid; }
      .cover h1 { font-size: 34pt; break-before: auto; }
      .edition-note { break-inside: avoid; margin-top: 0; }
      .contents { break-before: page; break-after: page; }
      h1 { font-size: 19pt; break-before: page; margin-top: 0; }
      h2 { font-size: 14pt; break-after: avoid; }
      h3 { font-size: 11.5pt; break-after: avoid; }
      h1, h2, h3 { break-inside: avoid; }
      p { widows: 2; orphans: 2; }
      a { color: inherit; text-decoration: none; }
      .toc li { padding: 3px 0; }
      .index-item { font-size: 9pt; }
      .glossary-entry { break-inside: avoid; }
      .source-note { font-size: 8pt; }
      .release-entry { break-before: page; }
    }
    """
    html = "<!doctype html>\n<html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>" + escape(TITLE) + "</title><style>" + css + "</style></head><body><main>" + "\n".join(content) + "</main></body></html>\n"
    OUTPUT.write_text(html, encoding="utf-8")
    print(f"Created {OUTPUT} ({len(paragraphs)} paragraphs, {len(headings)} contents entries, {len(index_terms)} index terms)")


if __name__ == "__main__":
    render()
