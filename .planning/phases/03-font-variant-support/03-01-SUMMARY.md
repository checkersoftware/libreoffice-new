---
phase: 03-font-variant-support
plan: 01
subsystem: fonts
tags: [wasm, freetype, emscripten, jspi, font-resolution, ttc]

# Dependency graph
requires:
  - phase: 02-caching-and-diagnostics
    provides: negative cache infrastructure and SAL_INFO diagnostic logging
provides:
  - Multi-variant ArrayBuffer[] font resolution from JS host
  - Family-name-only negative cache (one JS call per family)
  - FreeType-extracted font metadata (no SetFamilyName override)
  - TTC collection support via existing addFontFile/CountTTCFonts pipeline
affects: []

# Tech tracking
tech-stack:
  added: []
  patterns: [newline-separated multi-path VFS resolution, family-name-only caching]

key-files:
  created: []
  modified:
    - vcl/unx/generic/fontmanager/fontsubst.cxx

key-decisions:
  - "Strict ArrayBuffer[] API — no single-buffer backward compat"
  - "Family-name-only negative cache — one JS call covers all variants"
  - "Removed SetFamilyName override — FreeType extracts real metadata for IsBetterMatch() scoring"
  - "Empty font name passed to registerFontFromVFS — FreeType name table takes precedence"

patterns-established:
  - "Multi-file VFS writes: indexed files {safeName}_{i}.ttf with newline-separated path return"
  - "Family-level caching: cache by family name only, not per-variant"

# Metrics
duration: 5min
completed: 2026-02-12
---

# Phase 3: Font Variant Support Summary

**ArrayBuffer[]-based multi-variant font resolution with FreeType-extracted metadata and family-name-only negative cache**

## Performance

- **Duration:** 5 min
- **Completed:** 2026-02-12
- **Tasks:** 5
- **Files modified:** 1

## Accomplishments
- JS resolver now returns ArrayBuffer[] with all variants of a font family at once
- Each buffer written to VFS as indexed file, registered via FreeType which extracts real family name, weight, italic, and width
- Negative cache simplified to `std::unordered_set<OUString>` keyed by family name only — one JS call per family regardless of style hints
- TTC files work automatically through existing `addFontFile`/`CountTTCFonts` pipeline
- `SetFamilyName` override removed so `IsBetterMatch()` scoring works correctly across Regular/Bold/Italic/etc.

## Task Commits

All 5 tasks committed atomically in a single commit (all modify the same file):

1. **Task 1: Simplify negative cache** - `04d3b518e2e2` (refactor)
2. **Task 2: Update em_startFontResolve for ArrayBuffer[]** - `04d3b518e2e2` (feat)
3. **Task 3: Multi-path splitting in FindFontSubstitute** - `04d3b518e2e2` (feat)
4. **Task 4: Remove SetFamilyName override** - `04d3b518e2e2` (refactor)
5. **Task 5: Clean up includes and comments** - `04d3b518e2e2` (refactor)

## Files Created/Modified
- `vcl/unx/generic/fontmanager/fontsubst.cxx` - Multi-variant font resolution with ArrayBuffer[], family-name-only negative cache, FreeType-extracted metadata

## Decisions Made
- Strict ArrayBuffer[] API with no single-buffer backward compat — cleaner contract
- Family-name-only negative cache — when JS returns all variants at once, style hints in the cache key are unnecessary
- Removed SetFamilyName entirely rather than conditionally — FreeType's extracted name is always more accurate

## Deviations from Plan
None - plan executed exactly as written

## Issues Encountered
None

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- All three phases of the font resolution roadmap are now code-complete
- Ready for verification against phase 3 success criteria

---
*Phase: 03-font-variant-support*
*Completed: 2026-02-12*
