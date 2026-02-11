---
phase: 02-caching-and-diagnostics
plan: 01
status: complete
started: 2026-02-11
completed: 2026-02-11
duration: 5min
---

## What Was Built

Replaced the Phase 1 `s_aTriedFonts` simple string set with a structured negative-only font cache (`s_aWasmNegativeCache`) keyed by family name + style hints (weight, italic, width, pitch). Added SAL_INFO/SAL_WARN diagnostic logging at every decision point in the WASM font resolution flow.

## Key Decisions

- **No positive cache needed:** Once a font is registered via AddTempDevFont, the PhysicalFontCollection caller finds it via `FindFontFamily()` before `FindFontSubstitute()` is ever called again
- **Cache key includes style hints:** Ready for Phase 3 variant support without further cache changes
- **Failed registration cached as negative:** Corrupt font files won't trigger repeated JS calls

## Key Files

### Modified
- `vcl/unx/generic/fontmanager/fontsubst.cxx` — WasmFontCacheKey struct, WasmFontCacheKeyHash, s_aWasmNegativeCache, SAL_INFO logging

## Self-Check: PASSED

- [x] s_aTriedFonts fully removed (0 grep matches)
- [x] s_aWasmNegativeCache with 4 references (declaration, check, 2 inserts)
- [x] WasmFontCacheKey struct with 5 fields (name + 4 style hints)
- [x] Negative cache insert on both JS-empty and registration-failure paths
- [x] SAL_INFO at: cache hit, JS request (with style hints), JS response, registration success
- [x] SAL_WARN at: registration failure
- [x] No positive cache logic
- [x] All changes within #ifdef EMSCRIPTEN

## Deviations

None.
