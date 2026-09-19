# Word-of-the-Day API Research & A.Word.A.Day Integration

Research into a free daily vocabulary word (Word of the Day / WOTD) source for
ChromaWOTD's afternoon presentation. The decisive constraint is not the payload
format — it is that **the panel font is printable ASCII only** (see
`LESSONS_LEARNT.md` §33), so a source that publishes a *respelling* pronunciation
is worth far more than one that publishes IPA.

## Decision: A.Word.A.Day (Wordsmith.org) as the source

### Endpoint Details
* **Method**: `GET`
* **URL**: `https://wordsmith.org/words/today.html`
* **Authentication**: None required (no API key or credentials).
* **Payload**: ~10.5 KB of HTML (section markup, no JavaScript required).
* **Transport**: HTTPS; `wordsmith.org` chains to the Let's Encrypt **YE1**
  (ECDSA) intermediate, which is pinned in `firmware/src/net/net_impl_esp32.cpp`.

### Published Fields
The page wraps each section identically — `<div style="…">LABEL:</div>` followed
by a value div — with these labels:

| Label | Example |
| :--- | :--- |
| *(headword)* | `<h3> breviloquent </h3>` |
| `PRONUNCIATION` | `(bre-VIL-uh-kwuhnt)` |
| `MEANING` | `adjective: Using few words.` |
| `ETYMOLOGY` | `From Latin brevis (short) + loqui (to speak).` |
| `USAGE` | `"[The driver] raised his hands from the wheel…"` + attribution |
| `NOTES` | Running commentary |
| `A THOUGHT FOR TODAY` | Daily quotation |

### Key Advantages for ChromaWOTD
1. **The pronunciation is a respelling, not IPA.** `(bre-VIL-uh-kwuhnt)` is pure
   ASCII and renders on the panel as-is. This is the reason this source was
   chosen and is the single most important property of the feed.
2. **Definition + real usage example** — both render directly, so the afternoon
   panel shows a definition followed by a quoted sentence.
3. **No key, no quota, no bot wall** (see the rejected alternatives below).
4. Matches the sibling docs' sourcing style: `SCRIPTURE_APIS.md` (BibleGateway),
   `WEATHER_APIS.md` (Open-Meteo).

## Rejected Alternatives (and why)

| Source | Outcome |
| :--- | :--- |
| **Merriam-Webster WOTD RSS** (`merriam-webster.com/wotd/feed/rss`) | **HTTP 403** — Cloudflare bot challenge ("Just a moment…"). An ESP32 can never satisfy it. Dead end. |
| **Merriam-Webster WOTD API** | Requires a registered API key; key would have to live in `secrets.h`. Not needed once a keyless source worked. |
| **Wordnik WOTD** (`api.wordnik.com/v4/words.json/wordOfTheDay`) | Requires an API key on every request. |
| **Wordsmith RSS** (`wordsmith.org/words/rss.xml`, `/awad/rss.xml`, `today.rss`) | **404**. The working feed is `wordsmith.org/awad/rss1.xml`, but it carries only the word + a one-line definition — **no pronunciation**, so it cannot drive the caption line. |
| **tinymind.eu/api/word.php** | Works, keyless, small (491 B JSON) — but its pronunciation is **IPA** (`/suːˈsʊrəs/`), which the ASCII panel font renders as `?`s. Rejected on exactly the criterion A.Word.A.Day satisfies. |

## ESP32 Implementation Notes
1. **Buffer placement**: the page is ~10 KB, which is far too large for the 16 KB
   sync-task stack. `cc_fetchWord()` uses a `static` (BSS) buffer — never a stack
   array.
2. **Parsing** (`cc_parseAwad`, shared host + device): values are found by their
   label, then **the label's own `</div>` must be skipped** before taking the
   value div; stopping at the first `</div>` after the label yields an empty
   value. The `USAGE` value additionally carries `<br>` + attribution after the
   quote, so the example is cut at the closing curly quote (`&#8221;`).
3. **Whitespace normalisation**: the source wraps values across lines. Those
   newlines are ASCII (< 0x80), so they pass a naive "is it ASCII?" check yet
   reach the glyph rasteriser as control characters and render as garbage. The
   extractor collapses all whitespace runs to single spaces, and the tests assert
   **printable** ASCII (0x20–0x7E), not merely `< 0x80`.
4. **Licensing / attribution**: A.Word.A.Day content is © Wordsmith.org. It is
   displayed on a personal device and is **not** redistributed in this repository;
   only the parser and a handful of bundled fallback words live here.
5. **Fallback hierarchy**:
   * Primary: A.Word.A.Day `today.html`
   * Offline fallback: a bundled word + respelling + definition in `main.cpp`
     (`kFallbackWord` / `kFallbackPron` / `kFallbackDef`), surfaced with the red
     `OFFLINE:` banner so the substitution is never silent.
