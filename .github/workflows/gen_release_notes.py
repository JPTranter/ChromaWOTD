#!/usr/bin/env python3
# flake8: noqa: E501
# The markdown template below deliberately contains long lines: GitHub renders the
# asset table and the flash commands as-is, and wrapping them would change the
# published output. Code (not content) is kept within the line limit instead.
"""Generate ChromaWOTD release notes — including flash instructions.

Called by the Release GitHub Actions workflow on a `v*` tag. Summarises the
feat/fix commits since the previous `v*` tag (docs/chore/refactor are excluded so
the summary reads as "what changed for the user"), and embeds the flash procedure
for the ESP32-S3 so a release is self-contained: someone arriving at the Releases
page can flash the attached image without hunting through the README.

Also appends a `title=...` line to $GITHUB_OUTPUT when set, so the workflow can
give the release a relevance-bearing name.

Usage:
    python .github/workflows/gen_release_notes.py --tag v0.1.0 --out notes.md
"""

import argparse
import os
import re
import subprocess

REPO = "JPTranter/ChromaWOTD"

_TEMPLATE = """# ChromaWOTD firmware {tag}

**Changes{since}:**
{changes}

---

## What this is

Firmware for a 2.9" quadruple-colour (black/white/red/yellow) ePaper display on a
**Seeed XIAO ESP32-S3** with the **ePaper Display Board EE05**. It shows a daily
scripture verse (mornings) or a word of the day (afternoons) plus local weather,
refreshing 2-4 times a day and deep-sleeping in between.

## Attached files

| File | Use |
| :--- | :--- |
| `ChromaWOTD-{tag}.bin` | **Complete flash image** — bootloader + partitions + application in one file. Flash at offset `0x0`. |
| `ChromaWOTD-{tag}-app.bin` | Application only (`0x10000`), for updating an already-flashed board. |
| `ChromaWOTD-{tag}.elf` | Unstripped binary, for debugging / `addr2line`. |

## Flashing

> [!NOTE]
> **A released image contains no credentials, and cannot.** Credentials are entered on
> the device over its own setup portal (SoftAP + QR code) and stored there; nothing is
> ever compiled into the firmware. So after flashing, the device comes up in its setup
> wizard — join the `ChromaWOTD-XXXXXX` network shown on the screen (scan the QR code)
> and enter your Wi-Fi details. They never leave the device.

### Flash the release image

1. Install `esptool` (once): `pip install esptool`
2. Connect the board over USB-C. If the serial port is missing, the device is
   deep-asleep — double-tap **RESET** (or unplug/replug) to wake it. The board uses the
   ESP32-S3's native USB Serial/JTAG, so no BOOT button is needed.
3. Flash the merged image at offset 0:

   ```bash
   esptool.py --chip esp32s3 --port <PORT> write_flash 0x0 ChromaWOTD-{tag}.bin
   ```

   `<PORT>` is `COM13`-style on Windows, `/dev/ttyACM0` on Linux, `/dev/cu.usbmodem*` on macOS.
4. **Set the device up**: the panel shows a Wi-Fi QR code. Scan it to join the setup
   network, and the configuration page opens automatically (or browse to
   `http://192.168.4.1`). Enter your Wi-Fi details, timezone and coordinates, then save.
   The device restarts and connects.

To change the settings later, or to move the device to another network: hold any button
for 10 seconds to reset it, and the setup portal returns.

### Building it yourself

```bash
git clone https://github.com/{repo}.git && cd ChromaWOTD
cd firmware && pio run -e s3 -t upload
```

No credentials file is needed — the same portal configures a locally-built image.

While the device deep-sleeps its USB port disappears, so an upload can find no port.
`python tools/flash_when_awake.py --seconds 420` polls for it and flashes on sight.

### Recovering an unresponsive board

If the port never enumerates, put the chip in ROM download mode: **hold BOOT, tap
RESET, release BOOT**. Both buttons are tiny and sit on the XIAO module beside the
USB-C connector. This is the recovery path, not the routine one.

## Verifying the image

The image is verified by `tools/merge_firmware.py` before it is attached: the
bootloader, partition table, `boot_app0` and application are each checked for
their expected signature at the expected offset, and a credential scan refuses to
publish an image containing Wi-Fi credentials (there is no compile-time path, so this
is a tripwire against one being reintroduced).

**Full changelog**: {compare}
"""


def git(*args):
    # Surface a failed git call in the workflow log. Swallowing stderr (the original
    # behaviour) meant an invalid ref produced an EMPTY changelog that read as "no changes",
    # which is exactly what a first release must not publish (LESSONS §42: a never-run gate
    # is a test of the gate).
    result = subprocess.run(["git", *args], capture_output=True, text=True)
    if result.returncode != 0:
        print(f"::warning::git {' '.join(args)} failed: {result.stderr.strip()}")
    return result.stdout


def prev_tag(current):
    """Previous v* tag before `current` in semver order, or '' when none."""
    tags = [
        t.strip() for t in git("tag", "--list", "v*", "--sort=-v:refname").splitlines() if t.strip()
    ]
    if current not in tags:
        return ""
    i = tags.index(current)
    return tags[i + 1] if i + 1 < len(tags) else ""


def changes_between(prev, tag):
    """(type, subject) for feat/fix commits in prev..tag.

    With no previous tag — the FIRST release — the range is the whole history up to
    `tag`. Returning [] there (the original behaviour) made the very first release
    announce "No feat/fix changes in this range", which is both wrong and the least
    useful possible thing to publish.
    """
    rng = f"{prev}..{tag}" if prev else tag
    out = []
    for line in git("log", "--pretty=%s", rng).splitlines():
        m = re.match(r"^(feat|fix)(\([^)]*\))?: (.*)$", line.strip(), re.I)
        if m:
            out.append((m.group(1).lower(), m.group(3).strip()))
    return out


def make_title(tag, changes):
    """Relevance-bearing release title, e.g. 'v0.1.0 - Fix date assembly'."""
    if not changes:
        return tag
    word = changes[0][0].capitalize()
    subj = re.sub(r"\s*\([^)]*\)$", "", changes[0][1]).strip()
    if len(word + subj) > 55:
        subj = subj[: 55 - len(word)].rstrip() + "..."
    return f"{tag} - {word} {subj}"


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--tag", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    prev = prev_tag(args.tag)
    changes = changes_between(prev, args.tag)
    since = f" since {prev}" if prev else " (initial release)"
    if changes:
        bullets = "\n".join(f"- **{t.capitalize()}**: {s}" for t, s in changes)
    else:
        bullets = "- No feat/fix changes in this range (docs/tooling only)."

    # compare/initial...v0.1.0 is a 404; a first release gets the commit list instead.
    compare = (
        f"https://github.com/{REPO}/compare/{prev}...{args.tag}"
        if prev
        else f"https://github.com/{REPO}/commits/{args.tag}"
    )

    body = _TEMPLATE.format(
        tag=args.tag,
        since=since,
        changes=bullets,
        repo=REPO,
        compare=compare,
    )
    with open(args.out, "w", encoding="utf-8") as f:
        f.write(body)

    if os.environ.get("GITHUB_OUTPUT"):
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as f:
            f.write(f"title={make_title(args.tag, changes)}\n")


if __name__ == "__main__":
    main()
