---
phase: 01-core-font-resolution
verified: 2026-02-10T19:20:00Z
status: human_needed
score: 6/6
human_verification:
  - test: "WASM build font callback invocation"
    expected: "Document requesting 'Calibri' triggers JavaScript callback with 'Calibri' string"
    why_human: "Requires WASM build, document rendering, and JavaScript callback instrumentation"
  - test: "Font registration and rendering"
    expected: "JavaScript writes TTF/OTF to VFS, returns path, font renders correctly"
    why_human: "Requires WASM build, visual verification of font rendering output"
  - test: "Graceful fallback on empty return"
    expected: "JavaScript returns empty string, LibreOffice falls through to fallback font without crash"
    why_human: "Requires WASM build and runtime behavior testing"
  - test: "JSPI suspension and resumption"
    expected: "WASM thread suspends during JavaScript callback and resumes with result"
    why_human: "Requires WASM build with JSPI enabled, threading behavior observation"
  - test: "Desktop build unchanged"
    expected: "Non-WASM builds compile and run identically with zero behavioral changes"
    why_human: "Requires desktop build, compilation testing, and behavioral regression testing"
---

# Phase 1: Core Font Resolution Verification Report

**Phase Goal:** A document requesting a missing font triggers a JavaScript callback that provides font data, and the font renders correctly in the output

**Verified:** 2026-02-10T19:20:00Z
**Status:** human_needed
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | A WASM build calling FindFontSubstitute() for a missing font invokes the EM_ASYNC_JS JavaScript callback with the font family name as a UTF-8 string | ✓ VERIFIED | Lines 254-259: `OString aUtf8 = OUStringToOString(rFontSelData.maTargetName, RTL_TEXTENCODING_UTF8); char* pPath = em_resolveFontFromHost(aUtf8.getStr());` — UTF-8 conversion + JS call present |
| 2 | When JavaScript writes a TTF/OTF file to the VFS and returns the path, C++ registers it via PrintFontManager + FreetypeManager + AnnounceFonts and the retry via GetFcSubstitute() finds it | ✓ VERIFIED | Lines 271-289: `registerFontFromVFS()` called (implements addFontFile + AddFontFile + AnnounceFonts), retry via `GetFcSubstitute()`, result cached and returned |
| 3 | When JavaScript returns empty/null (font unavailable), the hook marks the name as tried and falls through to the existing fontconfig fallback chain without crash or hang | ✓ VERIFIED | Lines 301-305: null check `if (pPath)` with SAL_INFO logging on null, no crash path. Line 252: `s_aTriedFonts.insert()` prevents retry |
| 4 | Non-JSPI builds (HAVE_EMSCRIPTEN_JSPI=0) get a synchronous EM_JS fallback path where the JS callback must be synchronous | ✓ VERIFIED | Lines 96-116: `#else` block with `EM_JS(char*, em_resolveFontFromHost, ...)` calling `Module.resolveSystemFontSync` |
| 5 | Desktop (non-WASM) builds compile identically -- all new code is behind #ifdef EMSCRIPTEN guards | ✓ VERIFIED | All new code in lines 27-35, 61-157, 161-163, 247-308 is inside `#ifdef EMSCRIPTEN` blocks. Zero changes visible outside guards. |
| 6 | FS.writeFile() and FS.mkdirTree() work inside the EM_ASYNC_JS/EM_JS body because FS is in EXPORTED_RUNTIME_METHODS | ✓ VERIFIED | Line 30 of EMSCRIPTEN_INTEL_GCC.mk: `"FS"` now exported unconditionally (moved outside Qt6 conditional). EM_ASYNC_JS uses FS at lines 87-88, EM_JS at lines 113-114. |

**Score:** 6/6 truths verified

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `vcl/unx/generic/fontmanager/fontsubst.cxx` | WASM font resolution hook with EM_ASYNC_JS/EM_JS + intercept-register-retry logic | ✓ VERIFIED | Contains `em_resolveFontFromHost` (EM_ASYNC_JS at line 75, EM_JS at line 102), `registerFontFromVFS()` at line 124, intercept block in FindFontSubstitute at line 248, static collection pointer at line 66, tried-font cache at line 69. Substantive: 173 lines added (commit 2eb3e8f2da41). Wired: called from FindFontSubstitute(), uses PrintFontManager/FreetypeManager singletons. |
| `solenv/gbuild/platform/EMSCRIPTEN_INTEL_GCC.mk` | FS exported unconditionally in EXPORTED_RUNTIME_METHODS | ✓ VERIFIED | Line 30: `"FS"` moved outside `$(if $(ENABLE_QT6),...)` conditional. Substantive: meaningful change (1 line modified, commit 27dd31a25e3d). Wired: EXPORTED_RUNTIME_METHODS consumed by Emscripten linker. |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|----|--------|---------|
| fontsubst.cxx (FindFontSubstitute) | em_resolveFontFromHost (EM_ASYNC_JS) | `char* pPath = em_resolveFontFromHost(aUtf8.getStr())` | ✓ WIRED | Line 259: direct call with UTF-8 string parameter |
| fontsubst.cxx (FindFontSubstitute) | PrintFontManager::get().addFontFile() + FreetypeManager::get().AddFontFile() + AnnounceFonts() | Font registration replicating AddTempDevFont logic | ✓ WIRED | Lines 129-153: `registerFontFromVFS()` calls addFontFile (line 130), AddFontFile (line 150), AnnounceFonts (line 153). Called from line 271. |
| fontsubst.cxx (RegisterFontSubstitutors) | static PhysicalFontCollection* pointer | `s_pFontCollection = pFontCollection` stored during init | ✓ WIRED | Line 162: pointer stored. Line 66: static declaration. Lines 126, 153, 248: usage in registerFontFromVFS and FindFontSubstitute. |
| FindFontSubstitute intercept | registerFontFromVFS | Retry loop after registration | ✓ WIRED | Lines 271-289: registerFontFromVFS called, then GetFcSubstitute retry, result cached on success |
| EM_ASYNC_JS/EM_JS | JavaScript FS API | FS.writeFile(), FS.mkdirTree() | ✓ WIRED | Lines 87-88 (EM_ASYNC_JS), 113-114 (EM_JS): FS.mkdirTree() and FS.writeFile() called. FS exported in EXPORTED_RUNTIME_METHODS (line 30 of EMSCRIPTEN_INTEL_GCC.mk) |

### Requirements Coverage

Phase 1 maps to requirements REQ-F01 through REQ-F06 and REQ-N01 through REQ-N04.

| Requirement | Status | Evidence |
|-------------|--------|----------|
| REQ-F01: Font Family Name Request | ✓ SATISFIED | Lines 254-259: UTF-8 conversion + em_resolveFontFromHost call with family name |
| REQ-F02: Raw Font File Data Return | ✓ SATISFIED | Lines 86-89 (EM_ASYNC_JS), 112-115 (EM_JS): JS writes to VFS via FS.writeFile(), returns path string |
| REQ-F03: Null/Failure Return Handling | ✓ SATISFIED | Lines 301-305: null path check with SAL_INFO logging, falls through gracefully. Line 252: s_aTriedFonts prevents retry |
| REQ-F04: TTF and OTF Format Support | ✓ SATISFIED | No format-specific restrictions in registerFontFromVFS(). PrintFontManager/FreeType handle both formats. |
| REQ-F05: JSPI Synchronous Bridge | ✓ SATISFIED | Lines 72-94: EM_ASYNC_JS with JSPI (HAVE_EMSCRIPTEN_JSPI guard). Lines 96-116: EM_JS sync fallback. JSPI infrastructure exists in EMSCRIPTEN_INTEL_GCC.mk lines 33-37. |
| REQ-F06: Write-to-Virtual-FS and AddTempDevFont Registration | ✓ SATISFIED | Lines 124-155: registerFontFromVFS() replicates AddTempDevFont pipeline exactly (addFontFile + AddFontFile + AnnounceFonts with quality boost 5800) |
| REQ-N01: Platform Isolation | ✓ SATISFIED | All new code behind #ifdef EMSCRIPTEN (lines 27-35, 61-157, 161-163, 247-308). Zero desktop changes. |
| REQ-N02: Binary Compatibility | ✓ SATISFIED | Hook is additive — fires only when `!bHaveSubstitute` (line 248). Existing fontconfig fallback chain unchanged. |
| REQ-N03: No Binary Size Overhead (Desktop) | ✓ SATISFIED | All new code conditional on EMSCRIPTEN. Desktop builds see zero changes. WASM uses JSPI (zero overhead) not Asyncify. |
| REQ-N04: Simple JS API Contract | ✓ SATISFIED | JS API documented in code comments (lines 72-74, 98-101): Module.resolveSystemFont(name) -> Promise<ArrayBuffer\|null>, Module.resolveSystemFontSync(name) -> ArrayBuffer\|null |

**All Phase 1 requirements satisfied** by code inspection.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| fontsubst.cxx | 55 | TODO comment (pre-existing) | ℹ️ Info | Pre-existing cache TODO in FcGlyphFallbackSubstitution (not from this phase) |
| fontsubst.cxx | 359 | TODO comment (pre-existing) | ℹ️ Info | Pre-existing cache TODO in FindFontSubstitute (not from this phase) |

**No blockers or warnings from this phase.** The TODOs are pre-existing.

### Human Verification Required

All automated checks PASSED. The following items need human verification because they require a WASM build, runtime execution, and observable behavior:

#### 1. JavaScript Callback Invocation

**Test:** Build LibreOffice as WASM with JSPI enabled. Render a document requesting "Calibri" (not bundled). Instrument JavaScript with `Module.resolveSystemFont = (name) => { console.log("Requested:", name); return null; }`. Check console output.

**Expected:** Console logs "Requested: Calibri" (or the requested font name). SAL_INFO log shows "WASM font resolution: requesting \"Calibri\" from host" (vcl.fonts area).

**Why human:** Requires WASM build, document rendering pipeline, and JavaScript callback observation. Cannot be verified by static code inspection alone.

#### 2. Font Registration and Rendering

**Test:** Implement `Module.resolveSystemFont = async (name) => { /* fetch TTF bytes */ return fontArrayBuffer; }` to return a valid TTF/OTF file. Render a document requesting that font. Visually inspect the output.

**Expected:** Font is registered (SAL_INFO: "WASM font resolution: received path ..., registering" and "retry succeeded"). Text in the document renders using the provided font, not a fallback.

**Why human:** Requires WASM build, font file provisioning, visual rendering verification. Static code cannot confirm font pipeline integration end-to-end.

#### 3. Graceful Fallback on Empty Return

**Test:** Implement `Module.resolveSystemFont = async (name) => null;`. Render a document requesting a font that returns null.

**Expected:** SAL_INFO log shows "WASM font resolution: host returned no font for \"<name>\"". Document renders with LibreOffice's normal fallback font. No crash, no hang, no infinite loop.

**Why human:** Requires WASM build and runtime behavior testing. Need to confirm LibreOffice's existing fallback chain continues correctly.

#### 4. JSPI Suspension and Resumption

**Test:** Build WASM with ENABLE_EMSCRIPTEN_JSPI=TRUE. Implement `Module.resolveSystemFont` as async (returns Promise). Use a 100ms delay before resolving. Render a document requesting a font.

**Expected:** WASM thread suspends during the JavaScript callback (100ms delay). Rendering completes successfully after resume. No deadlock, no threading errors.

**Why human:** Requires JSPI-enabled WASM build and threading behavior observation. JSPI suspension is runtime behavior not statically verifiable.

#### 5. Desktop Build Unchanged

**Test:** Build LibreOffice on desktop (Linux/macOS/Windows) without EMSCRIPTEN defined. Run existing font substitution tests. Render test documents. Compare binary size with baseline.

**Expected:** Zero compilation errors or warnings. All existing font tests pass. No behavioral differences. Binary size unchanged (all new code is behind `#ifdef EMSCRIPTEN`).

**Why human:** Requires desktop build environment, compilation, test suite execution, and regression testing. Cannot be fully verified by code inspection.

## Summary

**All 6 observable truths VERIFIED** by static code inspection.

**All required artifacts present and substantive:**
- `vcl/unx/generic/fontmanager/fontsubst.cxx`: 173 lines added, complete EM_ASYNC_JS/EM_JS hook + registerFontFromVFS + intercept-register-retry logic
- `solenv/gbuild/platform/EMSCRIPTEN_INTEL_GCC.mk`: FS exported unconditionally

**All key links WIRED:**
- FindFontSubstitute → em_resolveFontFromHost
- em_resolveFontFromHost → JavaScript Module.resolveSystemFont
- JavaScript → FS.writeFile (FS exported in EXPORTED_RUNTIME_METHODS)
- FindFontSubstitute → registerFontFromVFS → PrintFontManager → FreetypeManager → AnnounceFonts

**All Phase 1 requirements SATISFIED** (REQ-F01 through REQ-F06, REQ-N01 through REQ-N04).

**No blocking anti-patterns** — only pre-existing TODOs documented.

**5 items flagged for human verification** — all require WASM build and runtime testing:
1. JavaScript callback invocation with font name
2. Font registration and rendering
3. Graceful fallback on empty return
4. JSPI suspension and resumption
5. Desktop build unchanged

**Commits verified:**
- `27dd31a25e3d`: FS export (Task 1)
- `2eb3e8f2da41`: WASM font resolution hook (Task 2)

The code implementation is complete and correct according to the plan. The phase goal is **achieved at the code level**. Human verification is required to confirm **runtime behavior** in a WASM build with JavaScript callbacks, JSPI suspension, and end-to-end font rendering.

---

_Verified: 2026-02-10T19:20:00Z_
_Verifier: Claude (gsd-verifier)_
