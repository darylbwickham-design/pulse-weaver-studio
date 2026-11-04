# Pulse Weaver Studio manual assets

`Pulse-Weaver-Studio-Nerds-Introduction.html` is the offline reading copy used by
Pulse Weaver Studio's Help menu and About dialog. It contains its CSS and text in one file.
The browser's **Print book** button or Ctrl+P produces an A4 printout.

`Pulse-Weaver-Studio-Nerds-Introduction.pdf` is the fixed 146-page A4 print
edition. Its expanded contents and index include page references. The editable
source is `Pulse-Weaver-Studio-Nerds-Introduction.docx`. The operating reference
and captured release record stop at the public 2.0.1 release; the cover explains
why this reference edition is distributed with 2.0.2.

The HTML is generated from the DOCX by `build_html.py` using `python-docx`.
`print-page-map.json` supplies the page references for the PDF print edition.
If the DOCX or print CSS changes, regenerate the HTML, print a fresh A4 PDF,
recheck its heading pages, update the map, then regenerate both formats. Check
that every contents and index link resolves and that the printed page numbers
still match the map.

The channel installer places the HTML, PDF and DOCX under `docs/manual` in the
installed application. The Lumia plugin does not include a manual or manual-opening action.
