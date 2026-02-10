---
phase: 01-core-font-resolution
plan: 01
subsystem: fonts
tags: [emscripten, wasm, jspi, fontconfig, freetype, em_async_js]

# Dependency graph
requires: []
provides:
  - "WASM font resolution hook (em_resolveFontFromHost) in fontsubst.cxx"
  - "FS exported unconditionally in EXPORTED_RUNTIME_METHODS for Emscripten builds"
  - "JS API contract: Module.resolveSystemFont / Module.resolveSystemFontSync"
  - "registerFontFromVFS() replicating AddTempDevFont pipeline"
affects: [02-robustness, 03-variant-support]

# Tech tracking
tech-stack:
  added: [emscripten EM_ASYNC_JS, emscripten EM_JS]
  patterns: [ifdef-guarded WASM hooks, static singleton pointer capture, negative cache via tried-set]

key-files:
  modified:
    - "vcl/unx/generic/fontmanager/fontsubst.cxx"
    - "solenv/gbuild/platform/EMSCRIPTEN_INTEL_GCC.mk"

key-decisions:
  - "Export FS unconditionally rather than only in Qt6 builds -- FORCE_FILESYSTEM=1 already set so no size cost"
  - "Use OString constructor instead of OString::Concat for char* to OString conversion"
  - "Quality boost of 5800 matches AddTempDevFont exactly for consistent font priority"
  - "s_aTriedFonts as both recursion guard and negative cache -- one lookup per font name per session"

patterns-established:
  - "WASM-specific code behind #ifdef EMSCRIPTEN with JSPI/sync split via #if HAVE_EMSCRIPTEN_JSPI"
  - "Static PhysicalFontCollection* captured during RegisterFontSubstitutors() for later use"
  - "Font registration via PrintFontManager::addFontFile + FreetypeManager::AddFontFile + AnnounceFonts"

# Metrics
duration: 3min
completed: 2026-02-10
---

# Phase 1 Plan 1: Core Font Resolution Summary

**EM_ASYNC_JS/EM_JS font resolution hook in fontsubst.cxx calling Module.resolveSystemFont from JavaScript, with font registration via PrintFontManager+FreetypeManager pipeline and fontconfig retry**

## Performance

- **Duration:** 3 min
- **Started:** 2026-02-10T19:09:49Z
- **Completed:** 2026-02-10T19:12:57Z
- **Tasks:** 2
- **Files modified:** 2

## Accomplishments
- FS exported unconditionally in EXPORTED_RUNTIME_METHODS so FS.writeFile/mkdirTree work in all WASM builds
- Complete WASM font resolution hook: EM_ASYNC_JS (JSPI) and EM_JS (sync fallback) calling JavaScript to resolve missing fonts
- registerFontFromVFS() replicating the exact AddTempDevFont pipeline (addFontFile + AddFontFile + AnnounceFonts)
- Intercept-register-retry logic in FindFontSubstitute() with negative cache preventing infinite recursion
- All code behind #ifdef EMSCRIPTEN -- desktop builds completely unchanged

## Task Commits

Each task was committed atomically:

1. **Task 1: Add "FS" to EXPORTED_RUNTIME_METHODS** - `27dd31a25e3d` (feat)
2. **Task 2: Implement WASM font resolution hook** - `2eb3e8f2da41` (feat)

## Files Created/Modified
- `solenv/gbuild/platform/EMSCRIPTEN_INTEL_GCC.mk` - Moved "FS" outside Qt6 conditional in EXPORTED_RUNTIME_METHODS
- `vcl/unx/generic/fontmanager/fontsubst.cxx` - Added EM_ASYNC_JS/EM_JS font resolution hook, registerFontFromVFS(), intercept-register-retry logic, static collection pointer, tried-font cache

## Decisions Made
- Exported "FS" unconditionally: FORCE_FILESYSTEM=1 is already set so there is no binary size cost, and it ensures FS.writeFile() works in all WASM builds regardless of Qt6
- Used OString(pPath) constructor instead of plan's OString::Concat(pPath) -- Concat is for concatenation chains, not direct char* conversion (Rule 1 bug fix)
- Matched AddTempDevFont quality boost (5800) and registration sequence exactly for font priority consistency

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed OString::Concat(pPath) to OString(pPath)**
- **Found during:** Task 2 (WASM font resolution hook)
- **Issue:** Plan specified `OString::Concat(pPath)` to convert the char* path from JavaScript, but OString::Concat is a concatenation helper, not a char* constructor
- **Fix:** Changed to `OString(pPath)` which correctly constructs an OString from a null-terminated C string
- **Files modified:** vcl/unx/generic/fontmanager/fontsubst.cxx
- **Verification:** Bracket/brace balance check passed; OString(const char*) constructor is the standard API
- **Committed in:** 2eb3e8f2da41 (Task 2 commit)

---

**Total deviations:** 1 auto-fixed (1 bug)
**Impact on plan:** Essential correctness fix. No scope creep.

## Issues Encountered
None

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Core font resolution hook is complete and ready for Phase 2 (robustness: error handling, caching, metrics)
- JS host must implement Module.resolveSystemFont(familyName) returning Promise<ArrayBuffer|null> for JSPI builds
- JS host must implement Module.resolveSystemFontSync(familyName) for non-JSPI builds
- Blockers from STATE.md remain: confirm Electron >= 30 for JSPI, verify HAVE_EMSCRIPTEN_JSPI in headless builds

## Self-Check: PASSED

All files verified present, all commits verified in git log.

---
*Phase: 01-core-font-resolution*
*Completed: 2026-02-10*
