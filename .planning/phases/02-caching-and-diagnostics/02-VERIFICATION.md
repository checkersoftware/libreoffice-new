---
phase: 02-caching-and-diagnostics
verified: 2026-02-11T23:57:36Z
status: passed
score: 4/4 must-haves verified
re_verification: false
---

# Phase 02: Caching and Diagnostics Verification Report

**Phase Goal:** Font resolution does not make redundant JavaScript calls, and the complete resolution flow is observable through diagnostic logging

**Verified:** 2026-02-11T23:57:36Z

**Status:** passed

**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | A font JS reported as unavailable triggers only ONE JavaScript call per session -- subsequent lookups hit the negative cache and skip the JS call | ✓ VERIFIED | Line 313: `s_aWasmNegativeCache.count(aCacheKey)` checks cache before JS call; Line 355, 361: negative cache inserted on JS-empty or registration-failure; Line 315-317: SAL_INFO logs "negative hit... skipping JS call" |
| 2 | A font previously fetched and registered triggers zero additional JavaScript calls -- the PhysicalFontCollection caller finds it before FindFontSubstitute is ever called | ✓ VERIFIED | Line 303: `s_pFontCollection->FindFontFamily(rFontSelData.maTargetName)` guard at entry to WASM block ensures registered fonts never reach resolution logic; Line 348-350: comment documents "font is now in PhysicalFontCollection, so FindFontFamily() in the caller finds it before we're called again" |
| 3 | If AddTempDevFont fails (corrupt file), the result is cached as negative -- no retry on subsequent lookups | ✓ VERIFIED | Line 343-355: `registerFontFromVFS()` returns false → SAL_WARN logged → `s_aWasmNegativeCache.insert(aCacheKey)` prevents retry |
| 4 | SAL_LOG=+INFO.vcl.fonts shows the complete resolution flow: font requested, cache hit/miss, JS call, response, registration result | ✓ VERIFIED | Line 315: cache hit log; Line 322-327: JS request with style hints; Line 339-341: JS response received; Line 345-347: registration success; Line 353-354: registration failure (SAL_WARN); Line 359-360: JS returned empty |

**Score:** 4/4 truths verified

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `vcl/unx/generic/fontmanager/fontsubst.cxx` | WasmFontCacheKey struct, WasmFontCacheKeyHash struct, s_aWasmNegativeCache replacing s_aTriedFonts, SAL_INFO logging at key points | ✓ VERIFIED | Lines 72-88: WasmFontCacheKey with 5 fields (maFamilyName + 4 style hints) and operator==; Lines 90-101: WasmFontCacheKeyHash with boost-style hash_combine (0x9e3779b9); Line 103: s_aWasmNegativeCache as unordered_set; Lines 32-33: includes unordered_set and functional; s_aTriedFonts fully removed (0 grep matches) |

**All artifacts exist, substantive, and wired.**

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|----|--------|---------|
| WasmFontCacheKey | rFontSelData (FontSelectPattern) | Cache key constructed from maTargetName + GetWeight/GetItalic/GetWidthType/GetPitch | ✓ WIRED | Lines 305-311: aCacheKey constructed with all 5 fields from rFontSelData |
| s_aWasmNegativeCache | resolveFontFromHost | Negative cache checked before JS call; inserted after JS returns empty or registration fails | ✓ WIRED | Line 313: cache.count() checked before Line 331: resolveFontFromHost() call; Lines 355, 361: cache.insert() after failure paths |

**All key links verified.**

### Requirements Coverage

Phase 02 maps to requirements REQ-F07, REQ-F08, REQ-F09 per ROADMAP.md.

| Requirement | Status | Supporting Truths |
|-------------|--------|-------------------|
| REQ-F07: Negative caching for unavailable fonts | ✓ SATISFIED | Truth 1, Truth 3 |
| REQ-F08: Re-registration guard | ✓ SATISFIED | Truth 2 |
| REQ-F09: Diagnostic logging | ✓ SATISFIED | Truth 4 |

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| - | - | - | - | None detected |

**Notes:**
- Lines 57, 425: Pre-existing TODO comments unrelated to Phase 02 (FcGlyphFallbackSubstitution cache)
- Lines 122, 136, 139: console.warn statements in EM_JS block (JavaScript side) — expected and appropriate for JS-side logging
- No empty implementations, no stub handlers, no placeholder logic
- All phase changes within `#ifdef EMSCRIPTEN` blocks (lines 28-37, 64-219, 223-present, 301-365)

### Human Verification Required

#### 1. Negative Cache Prevents Redundant JS Calls

**Test:** 
1. Build WASM LibreOffice with Phase 02 changes
2. Load a document requesting a font that the host does NOT have (e.g., "NonexistentFont")
3. Set SAL_LOG=+INFO.vcl.fonts before rendering
4. Render the document (triggers font resolution)
5. Check stderr output

**Expected:**
- First lookup: "WASM font resolution: requesting \"NonexistentFont\" from host" followed by "JS returned empty"
- Subsequent lookups (same session): "WASM font cache: negative hit for \"NonexistentFont\", skipping JS call"
- No additional "requesting from host" logs for the same font

**Why human:** Requires runtime testing in WASM build with live document rendering and JS integration. Cannot verify session-level caching behavior statically.

#### 2. Re-registration Guard Works

**Test:**
1. Build WASM LibreOffice with Phase 02 changes
2. Load a document requesting a font that the host HAS (e.g., "Arial")
3. Set SAL_LOG=+INFO.vcl.fonts
4. Render the document multiple times (or render multiple pages with the same font)
5. Check stderr output

**Expected:**
- First lookup: "WASM font resolution: requesting \"Arial\" from host" → "JS returned \"/tmp/fonts/Arial.ttf\"" → "registration succeeded"
- Subsequent lookups: NO additional "requesting from host" logs (FindFontFamily() guard prevents re-entry to WASM block)

**Why human:** Requires runtime verification that PhysicalFontCollection::FindFontFamily() finds the registered font before FindFontSubstitute() is called. Static analysis shows the guard exists (line 303), but runtime behavior depends on caller's call chain.

#### 3. Complete Diagnostic Flow Observable

**Test:**
1. Build WASM LibreOffice with Phase 02 changes
2. Load a document with multiple fonts: one available, one unavailable, one that fails registration (corrupt file)
3. Set SAL_LOG=+INFO.vcl.fonts
4. Render the document
5. Check stderr output for complete flow

**Expected:**
Logs should show:
- Font requested with style hints: "requesting \"FontName\" from host (w=400 i=0 wd=5 p=2)"
- Cache hit: "negative hit for \"FontName\", skipping JS call"
- JS response: "JS returned \"/tmp/fonts/FontName.ttf\"" or "JS returned empty"
- Registration outcome: "registration succeeded" or "registration failed" (SAL_WARN)

**Why human:** Requires visual inspection of log output to verify completeness and clarity. Static analysis confirms log points exist, but actual readability and usefulness needs human judgment.

#### 4. Failed Registration Cached as Negative

**Test:**
1. Build WASM LibreOffice with Phase 02 changes
2. Modify JS resolver to return a corrupt/invalid font file for a specific font name
3. Set SAL_LOG=+INFO.vcl.fonts
4. Render document requesting the corrupt font multiple times
5. Check stderr output

**Expected:**
- First lookup: "JS returned \"/tmp/fonts/CorruptFont.ttf\"" → "registration failed for \"file:///tmp/fonts/CorruptFont.ttf\"" (SAL_WARN)
- Subsequent lookups: "negative hit for \"CorruptFont\", skipping JS call"

**Why human:** Requires testing with intentionally corrupt font file. Static analysis shows the code path exists (lines 343-355), but runtime behavior with actual corrupt files needs verification.

### Gaps Summary

No gaps found. All must-haves verified against the codebase:

1. **Negative cache infrastructure:** `WasmFontCacheKey` struct with 5 fields, `WasmFontCacheKeyHash` with boost-style combining, `s_aWasmNegativeCache` as unordered_set — all present and correctly implemented
2. **Cache logic:** Check before JS call (line 313), insert on JS-empty (line 361) and registration-failure (line 355) — both paths covered
3. **Re-registration guard:** `FindFontFamily()` check at WASM block entry (line 303) ensures registered fonts never reach resolution logic
4. **Diagnostic logging:** SAL_INFO at all decision points (cache hit, JS request, JS response, registration success) and SAL_WARN for failures
5. **s_aTriedFonts removed:** Zero grep matches confirm complete removal
6. **Desktop build isolation:** All changes within `#ifdef EMSCRIPTEN` blocks

The implementation matches the PLAN specification exactly. Human verification is needed to confirm runtime behavior (caching works across document renders, logs are readable, corrupt files handled correctly), but the code structure is sound and complete.

---

_Verified: 2026-02-11T23:57:36Z_  
_Verifier: Claude (gsd-verifier)_
