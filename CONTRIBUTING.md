# Contributing to CHROMAWOTD

Thank you for your interest in contributing to CHROMAWOTD! This guide will help you get started.

## Table of Contents

- [Code of Conduct](#code-of-conduct)
- [Getting Started](#getting-started)
- [Development Workflow](#development-workflow)
- [Building & Testing](#building--testing)
- [Code Style](#code-style)
- [Submitting Changes](#submitting-changes)
- [Documentation](#documentation)

## Code of Conduct

This project follows the [Contributor Covenant](https://www.contributor-covenant.org/version/2/0/code_of_conduct.html) code of conduct. Be respectful, constructive, and inclusive in all interactions.

## Getting Started

### Prerequisites

- **PlatformIO** (for firmware builds)
- **CMake** 3.16+ with Ninja generator (for host tests)
- **Python 3.8+** (for tooling)
- **MinGW GCC** on Windows (for host tests)
- **Pillow** (Python, for render verification)
- **pre-commit** (for the secret-scanning + hygiene hooks)

### Install the git hooks (once per clone)

```bash
pip install pre-commit
pre-commit install
```

Committed secrets are blocked in two places that mirror the sibling eClock project: gitleaks
scans **staged content** on every commit (`.pre-commit-config.yaml`), and a CI job rescans **all
history** on every push (`.github/workflows/ci.yml`). Skipping this step leaves only the CI layer.

The first commit after installing can fail while `end-of-file-fixer` rewrites newly added files —
`git add -A && git commit` again and it passes.

### Clone the Repository

```bash
git clone https://github.com/JPTranter/ChromaWOTD.git
cd ChromaWOTD
```

### Verify Your Setup

Run the full verification suite to ensure everything is configured correctly:

```bash
python tools/verify_all.py
```

This command:
1. Builds the firmware for XIAO ESP32-S3 (`pio run -e s3`)
2. Runs the host test suite (CMake + GoogleTest) on the **5x7 fallback** font path
3. Runs the same suite on the **device** font path (`-DCHROMAWOTD_DEVICE_FONTS=ON`, the
   proportional Roboto set the firmware actually ships)
4. Verifies the render ledger (PNGs in `docs/images/` match ctest output)
5. Measures the layout alignment invariants

**Exit code is non-zero on any failure.** If this passes, your setup is ready.

## Development Workflow

### Branching Model

- **`master`**: Stable, release-ready code. Only merged via PRs.
- **Feature branches**: `feat/<description>`, `fix/<description>`, `docs/<description>`, `tools/<description>`, `chore/<description>`.
- **Naming**: Use conventional commits (see below).

### Conventional Commits

All commits must follow [Conventional Commits](https://www.conventionalcommits.org/):

```
<type>(<scope>): <description>

[optional body]

[optional footer(s)]
```

**Types:**
- `feat`: New feature
- `fix`: Bug fix
- `docs`: Documentation only
- `style`: Code style (formatting, no logic change)
- `refactor`: Code refactor (no feature or fix)
- `test`: Adding or correcting tests
- `chore`: Maintenance tasks (build, CI, deps)

**Examples:**
```
feat(layout): add weather icon reflow for no-alert case
fix(font): correct degree symbol positioning in FreeSans path
docs(review): add check-off table to REVIEW.md
chore(build): remove dead board_pins.h and driver.h
```

### Making Changes

1. **Create a feature branch:**
   ```bash
   git checkout -b feat/my-feature
   ```

2. **Make your changes.** Keep commits small and atomic. Each commit should:
   - Do one thing
   - Pass all tests
   - Have a clear, descriptive message

3. **Test your changes:**
   ```bash
   # Firmware build
   cd firmware && pio run -e s3 && cd ..

   # Host tests
   cmake --build firmware/test/build
   ctest --test-dir firmware/test/build --output-on-failure

   # Full verification
   python tools/verify_all.py
   ```

4. **Commit your changes:**
   ```bash
   git add -p  # stage selectively
   git commit -m "feat(layout): add weather icon reflow for no-alert case"
   ```

5. **Push and open a PR:**
   ```bash
   git push origin feat/my-feature
   ```

## Building & Testing

### Firmware (PlatformIO)

```bash
cd firmware

# Build
pio run -e s3

# Flash
pio run -e s3 -t upload

# Serial monitor
pio device monitor -b 115200
```

**Notes:**
- Build takes ~13 seconds on a clean tree.
- Flash uses USB CDC serial (not DFU).
- See `docs/lessons/LESSONS_LEARNT.md` for hardware-specific gotchas. (Note the
  directory: `docs/LESSONS_LEARNT.md` no longer exists.)

#### If `pio` is not on PATH

On this Windows host PlatformIO is installed as a Python module rather than a
`pio.exe` on `PATH`, so invoke it explicitly:

```bash
/c/Python314/python.exe -m platformio run -e s3
```

`tools/verify_all.py` detects this automatically and falls back to
`python -m platformio`.

#### Flashing a device that is deep-asleep

While the XIAO ESP32-S3 deep-sleeps, its native USB Serial/JTAG port does **not exist**
(`pio device list` is empty), so a one-shot `-t upload` fails with no port to attach to. The
port returns on every wake — use the retry-loop uploader:

```bash
python tools/flash_when_awake.py --seconds 420
```

**Most of the time this needs no button press at all.** This board's native USB Serial/JTAG
peripheral can put the ESP32-S3 into download mode by itself, so double-tapping RESET (or
unplug/replug USB) to re-enumerate the port is enough and the upload then succeeds normally.

The ROM-download-mode sequence — **hold BOOT, tap RESET, release BOOT** — is the *recovery*
path for when the port will not enumerate at all or the chip is wedged (e.g. left in download
mode by a bad DTR/RTS reset). Note both buttons are on the XIAO module beside the USB-C
connector, not the three user buttons on the EE05 carrier.

### Host Tests (CMake + GoogleTest)

```bash
# Configure (first run only)
cmake -S firmware/test -B firmware/test/build -G Ninja

# Build
cmake --build firmware/test/build

# Run tests
ctest --test-dir firmware/test/build --output-on-failure
```

**Offline builds:** If you don't have internet access, provide a local GoogleTest checkout:
```bash
cmake -S firmware/test -B firmware/test/build -G Ninja \
      -DCHROMAWOTD_GTEST_DIR=/path/to/googletest
```

### Render Ledger

The render ledger ensures that PNGs in `docs/images/` are byte-identical to ctest output.

**Regenerate after layout changes:**
```bash
python tools/verify_all.py --fix
```

**Verify ledger integrity:**
```bash
python tools/verify_all.py
```

**Preview a fixture without flashing:**
```bash
python tools/render_preview.py --fixture --open
python tools/render_preview.py --verse "Trust in the Lord..." --highlight "Lord" --open
```

## Code Style

### C++ Style

- **Formatter:** `.clang-format` (see below)
- **Indentation:** 4 spaces (no tabs)
- **Line length:** 120 characters (soft limit)
- **Naming:**
  - Classes/structs: `CamelCase` (e.g., `VerseData`, `WeatherData`)
  - Functions: `camelCase` (e.g., `drawLayout`, `cc_utf8ToAscii`)
  - Constants: `UPPER_SNAKE_CASE` (e.g., `CC_WHITE`, `CC_DEGREE`)
  - Private members: `g_` prefix (e.g., `g_bodyFont`, `g_canvas`)

### Python Style

- **Formatter:** `black` (100 character line length)
- **Linter:** `flake8` (config in `pyproject.toml` or `.flake8`)
- **Type hints:** Use `mypy` for static type checking (optional but recommended)

### Markdown Style

- **Line length:** 80 characters (soft limit)
- **Headers:** Use `#` for document title, `##` for sections, `###` for subsections
- **Links:** Use relative paths where possible (e.g., `docs/README.md` not `/docs/README.md`)
- **Images:** Use `![alt](path)` syntax

## Submitting Changes

### Pull Request Process

1. **Open a PR** against `master`.
2. **Fill out the PR template:**
   - Summary of changes
   - Testing done (local + CI)
   - Screenshots (if visual changes)
   - Links to related issues
3. **Request a review** from a maintainer.
4. **Address review comments** and push updates.
5. **Merge** when approved and CI passes.

### CI/CD

CI runs on every PR and merge to `master`:
- Build firmware (`pio run -e s3`)
- Run host tests (`ctest`)
- Verify render ledger (`verify_all.py --skip-firmware`)
- Lint code (`clang-format --dry-run`, `flake8`)

**CI is green** = PR can be merged. **CI is red** = fix the issues before merging.

## Documentation

### Updating Documentation

- **README.md**: Project overview, quick start, hardware specs
- **docs/ARCHITECTURE.md**: System design, security model, state machine
- **docs/REVIEW.md**: Code review findings (check-off table)
- **docs/STATUS.md**: Current phase, completed tasks, next steps
- **docs/PROJECT_PLAN.md**: Phased implementation plan
- **docs/lessons/LESSONS_LEARNT.md**: Hard-won findings, hardware quirks

**Rule:** If you change code that affects behavior, update the documentation. If you add a feature, add tests. If you fix a bug, add a regression test.

### Render Documentation

Renders live in `docs/images/` and are produced by the host test harness.

**Regenerate after layout changes:**
```bash
python tools/regenerate_screenshots.py
```

**Verify renders match code:**
```bash
python tools/verify_all.py
```

**Never edit PNGs manually.** They are artifacts of the code. If a render looks wrong, fix the code and regenerate.

## Questions & Help

- **GitHub Issues**: Report bugs, request features, ask questions
- **Discussions**: Long-form questions, design decisions, architecture
- **Chat**: Real-time help (link to be added)

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.

## Acknowledgments

- [Seeed Studio](https://www.seeedstudio.com/) for the XIAO ePaper Display Board EE05 and Seeed_GFX library
- [Adafruit](https://adafruit.com/) for the 5x7 font and TFT_eSPI library
- [Google](https://google.github.io/googletest/) for GoogleTest
- [stb](https://github.com/nothings/stb) for stb_image_write

---

**Thank you for contributing to CHROMAWOTD!** 🌿
