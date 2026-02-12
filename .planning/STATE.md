# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-02-09)

**Core value:** When LibreOffice WASM can't find a requested font, it calls out to JavaScript so the host can provide font data from the system -- eliminating the need to bundle most fonts.
**Current focus:** Phase 3: Font Variant Support

## Current Position

Phase: 3 (Font Variant Support)
Plan: 1 of 1 in current phase
Status: Complete
Last activity: 2026-02-12 -- Multi-variant ArrayBuffer[] resolution, family-name-only cache, FreeType-extracted names

Progress: [██████████] 100%

## Performance Metrics

**Velocity:**
- Total plans completed: 2
- Average duration: 4min
- Total execution time: 8min

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| 01-core-font-resolution | 1 | 3min | 3min |
| 03-font-variant-support | 1 | 5min | 5min |

**Recent Trend:**
- Last 5 plans: 01-01 (3min), 03-01 (5min)
- Trend: stable

*Updated after each plan completion*

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
Recent decisions affecting current work:

- [Roadmap]: Style hints (REQ-F10) and TTC support (REQ-F11) included in v1 scope despite being P2 features
- [Roadmap]: 3-phase structure derived from requirement clustering (core resolution, robustness, variant support)
- [01-01]: Export FS unconditionally in EXPORTED_RUNTIME_METHODS -- FORCE_FILESYSTEM=1 already set, no size cost
- [01-01]: Quality boost 5800 matches AddTempDevFont exactly for consistent font priority
- [01-01]: s_aTriedFonts as both recursion guard and negative cache -- one JS call per font name per session
- [01.1]: Root cause: bHaveSubstitute=1 always -- fontconfig always finds a substitute, blocking the WASM hook
- [01.1]: Fix: gate on FindFontFamily() (actual collection presence) instead of !bHaveSubstitute
- [01.1]: Move WASM block before GetFcSubstitute() -- resolve from host BEFORE fontconfig runs
- [01.1]: Return false after registration -- let caller find font directly in collection via ImplFindFontFamilyBySearchName()
- [03-01]: Strict ArrayBuffer[] API -- no single-buffer backward compat
- [03-01]: Family-name-only negative cache -- one JS call covers all variants
- [03-01]: Removed SetFamilyName override -- FreeType extracts real metadata for IsBetterMatch() scoring

### Roadmap Evolution

- Phase 1.1 inserted after Phase 1: debug font resolution hook not firing in WASM builds (URGENT)

### Pending Todos

None yet.

### Blockers/Concerns

- Confirm Electron version >= 30 for JSPI support (if < 30, synchronous-only fallback needed)
- Verify HAVE_EMSCRIPTEN_JSPI is set for headless (non-Qt) WASM builds
- JSPI export coverage: verify font substitution call site is covered or add to JSPI_EXPORTS
- Separate abort issue: "libc++abi: terminating" from OOX StorageBase missing input stream -- NOT related to fonts, needs separate investigation
- Electron printErr handler treats all stderr as errors -- should be fixed after font resolution works

## Session Continuity

Last session: 2026-02-12
Stopped at: Phase 03 complete -- multi-variant font resolution applied
Resume file: None
Next action: Verify phase 3 goal achievement
