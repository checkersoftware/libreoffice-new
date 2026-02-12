---
phase: 03-font-variant-support
verified: 2026-02-12T17:30:00Z
status: passed
score: 5/5 must-haves verified
must_haves:
  truths:
    - "JS resolver returning ArrayBuffer[] with multiple font variants causes all variants to be written to VFS and registered"
    - "A font family in the negative cache is keyed by family name only — requesting the same family with different style hints does NOT trigger a second JS call"
    - "FreeType extracts the real family name, weight, italic, and width from each registered font file — no override from C++"
    - "TTC files returned as one ArrayBuffer entry register all contained faces automatically"
    - "An empty array from JS adds the family to the negative cache with a single entry"
  artifacts:
    - path: "vcl/unx/generic/fontmanager/fontsubst.cxx"
      provides: "Multi-variant font resolution with family-name-only negative cache"
      contains: "std::unordered_set<OUString>"
  key_links:
    - from: "em_startFontResolve EM_JS"
      to: "FindFontSubstitute EMSCRIPTEN block"
      via: "newline-separated paths in pResultPath"
      pattern: "split.*\\n"
    - from: "registerFontFromVFS"
      to: "PhysicalFontCollection"
      via: "FreeType-extracted FontAttributes (no SetFamilyName override)"
      pattern: "aDFA\\.IncreaseQualityBy"
---

# Phase 3: Font Variant Support Verification Report

**Phase Goal:** The host can provide the exact font variant (bold, italic, condensed) a document needs, and TrueType Collection files register all contained faces

**Verified:** 2026-02-12T17:30:00Z
**Status:** PASSED
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | JS resolver returning ArrayBuffer[] with multiple font variants causes all variants to be written to VFS and registered | ✓ VERIFIED | `em_startFontResolve` EM_JS (lines 86-132) iterates `fontDataArray` with `Array.isArray()` check, writes indexed files `/tmp/fonts/{safeName}_{i}.ttf`, joins paths with `\n`, returns to C++. `FindFontSubstitute` (lines 323-346) splits with `getToken(0, '\n', nIdx)` and registers each via `registerFontFromVFS(aFileURL, OUString())`. |
| 2 | A font family in the negative cache is keyed by family name only — requesting the same family with different style hints does NOT trigger a second JS call | ✓ VERIFIED | Negative cache is `std::unordered_set<OUString> s_aWasmNegativeCache` (line 72). Cache key is `OUString aFamilyKey = rFontSelData.maTargetName` (line 297). Cache check uses `.count(aFamilyKey)` (line 299), insert uses `.insert(aFamilyKey)` (lines 361, 367). No style hints in key. |
| 3 | FreeType extracts the real family name, weight, italic, and width from each registered font file — no override from C++ | ✓ VERIFIED | `registerFontFromVFS` (lines 177-209) calls `rMgr.addFontFile(rFileURL)` which triggers FreeType analysis. `FontAttributes aDFA = pFont->m_aFontAttributes` (line 194) gets FreeType-extracted attributes. `SetFamilyName` removed (verified by grep — 0 hits). Comment lines 196-198 explicitly states "Do NOT override family name — let FreeType's extracted name from the font file's name table stand." |
| 4 | TTC files returned as one ArrayBuffer entry register all contained faces automatically | ✓ VERIFIED | `registerFontFromVFS` calls `rMgr.addFontFile(rFileURL)` (line 183) which returns `std::vector<psp::fontID>`. Loop iterates all returned face IDs (lines 188-205). `PrintFontManager::addFontFile()` internally calls `CountTTCFonts()` and handles TTC iteration. No TTC-specific gating in the hook — TTC works through existing pipeline. |
| 5 | An empty array from JS adds the family to the negative cache with a single entry | ✓ VERIFIED | `em_startFontResolve` (lines 98-102) checks `!Array.isArray(fontDataArray) || fontDataArray.length === 0`, calls `_em_fontResolveComplete(pReq, 0)` which returns `nullptr` to C++. `FindFontSubstitute` (lines 363-368) checks `if (!pPaths)`, inserts `s_aWasmNegativeCache.insert(aFamilyKey)`. |

**Score:** 5/5 truths verified

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `vcl/unx/generic/fontmanager/fontsubst.cxx` | Multi-variant font resolution with family-name-only negative cache | ✓ VERIFIED | File exists, modified in commit `04d3b518e2e2`. Contains `std::unordered_set<OUString> s_aWasmNegativeCache` (line 72). ArrayBuffer[] iteration in EM_JS (lines 108-117). Path splitting in FindFontSubstitute (lines 327-346). No `SetFamilyName` (grep verified). All patterns present. |

**Artifact Verification Details:**

**Level 1 - Exists:** ✓ File present at expected path  
**Level 2 - Substantive:** ✓ Contains `std::unordered_set<OUString>` (line 72), `Array.isArray` (line 98), `paths.join('\n')` (line 125), `getToken(0, '\n', nIdx)` (line 328), `IncreaseQualityBy(5800)` (line 195), no `SetFamilyName` (0 grep hits)  
**Level 3 - Wired:** ✓ Used by `FindFontSubstitute` (line 294-371 EMSCRIPTEN block), integrated with `PhysicalFontCollection` (line 207 `AnnounceFonts`), called by existing hook infrastructure

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|----|--------|---------|
| `em_startFontResolve` EM_JS | `FindFontSubstitute` EMSCRIPTEN block | newline-separated paths in `pResultPath` | ✓ WIRED | EM_JS joins paths with `paths.join('\n')` (line 125), returns via `stringToNewUTF8(result)` (line 127). C++ receives in `char* pPaths = resolveFontFromHost(...)` (line 316), splits with `aPaths.getToken(0, '\n', nIdx)` (line 328). |
| `registerFontFromVFS` | `PhysicalFontCollection` | FreeType-extracted FontAttributes (no SetFamilyName override) | ✓ WIRED | `registerFontFromVFS` calls `rMgr.addFontFile(rFileURL)` → FreeType extracts metadata → `FontAttributes aDFA = pFont->m_aFontAttributes` (line 194) → `aDFA.IncreaseQualityBy(5800)` (line 195) → `rFreetypeManager.AddFontFile(...)` (line 204) → `rFreetypeManager.AnnounceFonts(s_pFontCollection)` (line 207). No `SetFamilyName` call (removed in Task 4). |

### Requirements Coverage

| Requirement | Status | Evidence |
|-------------|--------|----------|
| REQ-F10: Font Variant Selection | ✓ SATISFIED | JS callback returns ArrayBuffer[] (EM_JS lines 96-127), C++ registers all variants with FreeType metadata intact (lines 323-346), no `SetFamilyName` override (verified by grep), `IsBetterMatch()` can use real weight/italic/width for scoring. |
| REQ-F11: TTC Support | ✓ SATISFIED | `registerFontFromVFS` calls `rMgr.addFontFile()` which returns `std::vector<psp::fontID>` (line 183), iterates all face IDs (lines 188-205). TTC iteration handled by existing `PrintFontManager::addFontFile()` → `CountTTCFonts()` pipeline. No TTC-specific gating. |

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| `vcl/unx/generic/fontmanager/fontsubst.cxx` | 56, 431 | `// TODO: add a cache` (FcGlyphFallbackSubstitution) | ℹ️ Info | Pre-existing TODO comments in non-EMSCRIPTEN code (FcGlyphFallbackSubstitution class). Not introduced in this phase. Not blocking. |

**Anti-pattern scan notes:**
- No TODO/FIXME in EMSCRIPTEN-specific code
- No placeholder implementations
- No empty returns or stub handlers
- All logging uses SAL_INFO/SAL_WARN (not console.log-only stubs)
- Pre-existing TODOs in fontconfig wrapper classes are outside phase scope

### Human Verification Required

None. All must-haves are verifiable programmatically via code inspection. The phase implements infrastructure — end-to-end testing of font variant selection and TTC rendering requires integration testing with the Electron app and actual font files, which is outside the scope of phase verification (that would be in system integration testing).

**Why no human verification needed:**
1. **ArrayBuffer[] handling:** Verified by reading EM_JS implementation (lines 96-127) — array iteration, indexed file writes, path joining are all present
2. **Family-name-only caching:** Verified by checking cache key construction (line 297) — no style hints included
3. **FreeType metadata extraction:** Verified by absence of `SetFamilyName` (grep) and explicit comment (lines 196-198)
4. **TTC support:** Verified by `addFontFile()` vector return and iteration (lines 183-205) — TTC handling is in existing pipeline
5. **Empty array handling:** Verified by EM_JS check (lines 98-102) and C++ null-path handling (lines 363-368)

All verification done via static code analysis against concrete patterns.

---

## Overall Status: PASSED

**Summary:** All 5 observable truths verified. Required artifact exists, is substantive, and is wired. All key links verified. Requirements REQ-F10 and REQ-F11 satisfied. No blocking anti-patterns. Phase goal achieved.

**Phase Goal Achieved:**
- ✓ Host can provide exact font variant (bold, italic, condensed) via ArrayBuffer[] return
- ✓ FreeType extracts variant metadata (weight, italic, width) without C++ override
- ✓ TrueType Collection files register all contained faces automatically via existing pipeline

**Ready to Proceed:** Yes. Phase 3 code-complete and verified. All three phases of the font resolution roadmap now complete.

---

_Verified: 2026-02-12T17:30:00Z_  
_Verifier: Claude (gsd-verifier)_
