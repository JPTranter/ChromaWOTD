# Scripture API Research & BibleGateway Integration

This document details the research into free daily Bible verse (Verse of the Day / VOTD) endpoints for CHROMAWOTD.

## Decision: BibleGateway as Primary Source

BibleGateway provides an unauthenticated JSON endpoint underlying their custom website widget.

### Endpoint Details
* **Method**: `GET`
* **URL**: `https://www.biblegateway.com/votd/get/?format=json&version=NIV`
* **Authentication**: None required (no API key or credentials).
* **Supported Translations**: Any major version on BibleGateway (e.g., `NIV`, `ESV`, `KJV`, `NLT`, `NASB`).

### Example Response
```json
{
  "votd": {
    "text": "&ldquo;A song of ascents. I lift up my eyes to the mountains&#8212; where does my help come from?  My help comes from the LORD, the Maker of heaven and earth.&rdquo;",
    "content": "<h3>Psalm 121</h3><h4>A song of ascents.</h4> I lift up my eyes to the mountains&#8212; where does my help come from? My help comes from the <span class=\"small-caps\">Lord</span>, the Maker of heaven and earth.",
    "display_ref": "Psalm 121:1-2",
    "reference": "Psalm 121:1-2",
    "permalink": "https://www.biblegateway.com/passage/?search=Psalm%20121%3A1-2&version=NIV",
    "day": "11",
    "month": "09",
    "year": "2026",
    "version": "New International Version",
    "version_id": "NIV"
  }
}
```

### Key Advantages for CHROMAWOTD
1. **Curated & Authoritative**: Provides the widely followed official BibleGateway daily verse.
2. **Translation Choice**: Switching versions is a simple query parameter update (`&version=ESV`, `&version=NIV`, etc.).
3. **Integrated Date Fields**: Includes `day`, `month`, and `year` to corroborate system time.
4. **Lightweight Parsing**: ~1.1 KB payload easily filtered and parsed via `ArduinoJson` on the ESP32-S3.

### ESP32 Implementation Notes
1. **HTML Entity Decoding**: Strip or translate HTML entities (`&ldquo;` / `&rdquo;` -> `"`, `&#8212;` -> `—`, `&#8211;` -> `–`, `&amp;` -> `&`). The safest order is: decode entities, then let the layout engine normalise — `cc_utf8ToAscii()` in `firmware/src/text/glyphs.cpp` already maps `—`/`–` to `-`, curly quotes to straight quotes, `…` to `.` and `⚠` to `!` (documented in `LESSONS_LEARNT.md` §10), so decoded text may safely reach the renderer as UTF-8. Do **not** rely on the panel font to carry any non-ASCII glyph: it cannot.
2. **Memory Safety**: Use `ArduinoJson` deserialization filtering to only parse `text` and `reference`.
3. **Highlight Extraction**: pick the red phrase and check it with `verseHighlightFound()` before drawing; the source's capitalisation may differ from the bundled fixture (matching is exact-then-case-insensitive, first occurrence only).
4. **Fallback Hierarchy**:
   * Primary: BibleGateway VOTD API
   * Secondary fallback: OurManna API (`https://beta.ourmanna.com/api/v1/get?format=json`) or NET Bible (`https://labs.bible.org/api/?passage=votd&type=json`)
   * Offline emergency fallback: Static array of 14 core verses embedded in flash storage.

