# Project Research Summary

**Project:** LibreOffice WASM System Font Resolution
**Domain:** Emscripten C++/JS interop for runtime font loading in WASM environments
**Researched:** 2026-02-09
**Confidence:** HIGH

## Executive Summary

This project adds dynamic system font resolution to LibreOffice's WASM build, bridging from C++ font subsystem to JavaScript host via Emscripten. When a document requests a missing font (e.g., "Calibri"), the WASM module calls out to JavaScript, receives the raw font file bytes from the host (Electron/browser), writes them to the Emscripten virtual filesystem, and registers them with LibreOffice's existing font management pipeline. The core challenge is making an inherently asynchronous operation (JS font fetching) appear synchronous to LibreOffice's deeply synchronous font resolution code.

The recommended approach leverages LibreOffice's existing JSPI infrastructure (already used in Qt6 WASM mode) to suspend the WASM thread while awaiting JavaScript promises. The hook point is `FcPreMatchSubstitution::FindFontSubstitute()` in fontsubst.cxx, where fontconfig substitution fails. After receiving font data from JS, we use the proven `AddTempDevFont()` pipeline for registration, which handles fontconfig + PrintFontManager + FreetypeManager in one call. The architecture is minimal: one new C++ class (`WasmFontResolver`), one small modification to fontsubst.cxx, and a JavaScript module providing font data.

Key risks are JSPI main-thread deadlock (mitigate with synchronous Electron IPC or PROXY_TO_PTHREAD build), infinite recursion from font name mismatches (mitigate with a negative cache of attempted fonts), and binary data transfer corruption (mitigate by writing to virtual FS from JS, passing only paths to C++). The existing codebase provides proven patterns for all building blocks; this is integration work, not invention.

## Key Findings

### Recommended Stack

LibreOffice's WASM build already has JSPI infrastructure (used in Qt6 mode for async event handling). The stack leverages this existing work rather than introducing new mechanisms. JSPI has zero binary size overhead compared to the alternative (Asyncify, which adds 20-50% to WASM size). The Emscripten virtual filesystem (MEMFS) is already enabled via `FORCE_FILESYSTEM=1`, so standard POSIX file I/O works from C++.

**Core technologies:**
- **JSPI (`-sJSPI`)**: Sync/async bridge allowing C++ to call async JavaScript without restructuring the font resolution pipeline. Already in use for Qt6 WASM event loop. Zero binary overhead. Requires Electron 30+ or Chrome 123+ at runtime.
- **EM_JS macro**: C++ to JS function interop. Proven pattern in 6+ locations across the codebase (PrimaryBindings.cxx, cpp2uno.cxx). Handles binary data via WASM heap manipulation (`Module._malloc`, `HEAPU8.set`).
- **MEMFS (POSIX file I/O)**: Emscripten's in-memory virtual filesystem. Write font files with standard `fopen`/`fwrite`/`fclose`. Already used throughout LibreOffice. No special Emscripten FS API calls needed from C++.
- **AddTempDevFont()**: Existing VCL API for runtime font registration. Handles fontconfig registration (`FcConfigAppFontAddFile`), PrintFontManager analysis (`analyzeFontFile`), FreeType registration (`FreetypeManager::AddFontFile`), and font collection announcement in one call.

**Critical decision: Use JSPI, not Asyncify.** Asyncify adds 20-50% to binary size, requires maintaining a function whitelist, and performs poorly on large codebases. JSPI is WebAssembly-native stack suspension with no code transformation overhead.

### Expected Features

**Must have (table stakes):**
- **Font family name request** — Pass requested font name (e.g., "Calibri") to JavaScript. Available from `FontSelectPattern.maTargetName`.
- **Raw font file data return** — JavaScript returns TTF/OTF bytes. FreeType needs the full font file for rasterization.
- **Null/failure return** — JavaScript signals "not available" so C++ falls back to fontconfig substitution chain.
- **TTF/OTF format support** — Standard system font formats. FreeType handles both natively.
- **Synchronous C++ to JS call** — Font resolution is called from deep in `FindFontFamily()` synchronous call chain. JSPI makes async JS appear synchronous.
- **Write-to-virtual-FS + AddTempDevFont registration** — After receiving bytes, write to `/tmp/fonts/<name>.ttf` and call existing registration pipeline.
- **Negative result caching** — Track fonts that returned null to avoid repeated JS calls during layout.
- **Guard against re-registration** — Don't re-fetch fonts already registered. Let retry lookup find them via `ImplFindFontFamilyBySearchName()`.

**Should have (competitive):**
- **Font style/weight/width hints** — Forward `FontWeight`, `FontItalic`, `FontWidth` from `FontSelectPattern` so JavaScript returns the correct variant (Bold vs. Regular). Simple integer parameters.
- **Language/script hint** — Forward `meLanguage` for CJK font selection. One integer parameter.
- **Multi-variant batch return** — Let JavaScript return all variants for a family at once (Regular + Bold + Italic + BoldItalic) to avoid N sequential roundtrips.
- **TTC (TrueType Collection) support** — System CJK fonts are often .ttc files. FreeType and `analyzeFontFile()` already handle this via face index iteration.
- **Logging/diagnostics** — Use `SAL_INFO("vcl.fonts", ...)` pattern to log resolution requests, cache hits, JavaScript responses. Essential for debugging.

**Defer (v2+):**
- **Font enumeration API** — Pre-populate LibreOffice's font menu with host system fonts. Only needed for interactive use, not headless PDF conversion.
- **Persistent font cache** — Cache fetched fonts in IndexedDB/IDBFS across sessions. Electron has filesystem persistence already.
- **WOFF/WOFF2 decompression** — System fonts are TTF/OTF/TTC. WOFF is only relevant for web-embedded fonts (defer).

### Architecture Approach

The architecture hooks into the existing font substitution chain at the point where fontconfig gives up. `FcPreMatchSubstitution::FindFontSubstitute()` detects substitution failure, calls `WasmFontResolver::tryResolveFont()`, which bridges to JavaScript via EM_JS, writes font data to virtual FS, registers via the proven `AddTempDevFont()` pipeline, and retries fontconfig query. The retry succeeds because the font is now registered.

**Major components:**
1. **FcPreMatchSubstitution (fontsubst.cxx)** — MODIFY: Add `#ifdef EMSCRIPTEN` block after fontconfig query fails. Call WasmFontResolver, invalidate cache, retry.
2. **WasmFontResolver (NEW: vcl/wasm/)** — C++ singleton that calls JavaScript via EM_JS, receives font data, writes to `/tmp/fonts/`, and calls existing registration APIs (`PrintFontManager::addFontFile()`, `FreetypeManager::AddFontFile()`, `FreetypeManager::AnnounceFonts()`).
3. **JavaScript Font Provider (NEW)** — In-memory cache (Map of fontName -> ArrayBuffer). Exposes `Module.fontProvider.getFont(name)` (synchronous) and `Module.fontProvider.preloadFont(name, url)` (async, called before document load).
4. **PrintFontManager + FreetypeManager (EXISTING, unchanged)** — Singleton font registration pipeline. `addFontFile()` orchestrates fontconfig registration, TrueType parsing, and FreeType glyph cache updates.
5. **PhysicalFontCollection (EXISTING, unchanged)** — Master font collection. Already handles dynamic font addition via `ImplRefreshAllFontData()`.

**Key insight:** The existing `AddTempDevFont()` in freetypetextrender.cxx already implements the full registration pipeline (fontconfig + PrintFontManager + FreetypeManager + collection announcement). Reuse this proven path rather than reimplementing registration logic.

### Critical Pitfalls

1. **EM_JS returns synchronously but JS font resolution is async** — Using `EM_JS` with `await` inside silently returns undefined. Use `EM_ASYNC_JS` instead, which requires JSPI or Asyncify. Guard with `#if HAVE_EMSCRIPTEN_JSPI`. Detected by: font data is always null/zero-length despite JavaScript callback executing.

2. **JSPI_EXPORTS must include the EM_ASYNC_JS function's mangled name** — If the new font resolution function is not listed in `-sJSPI_EXPORTS` in EMSCRIPTEN_INTEL_GCC.mk, the WASM module traps at runtime with `RuntimeError: unreachable` when trying to suspend. Add the function's symbol (underscore-prefixed) to the exports list.

3. **Calling EM_ASYNC_JS from the main browser thread deadlocks** — JSPI suspends the WASM stack and returns a Promise to the event loop. If the caller is on the main browser thread, the event loop is frozen, so the Promise never resolves. Mitigate: Use synchronous Electron IPC (`ipcRenderer.sendSync`) in the JavaScript provider for Electron use case, OR use `PROXY_TO_PTHREAD` build mode to move C++ execution to a worker thread.

4. **FindFontSubstitute() is const — side effects are fragile** — Calling `AddTempDevFont()` (which calls `ImplClearAllFontData()`) from within the const substitution method invalidates the font collection being iterated. Solution: Register font with fontconfig + PrintFontManager during the hook, but defer collection announcement to the retry lookup. Or bracket with `ImplClearAllFontData` / `ImplRefreshAllFontData` RAII guard (see `EmbeddedFontsManager::activateFonts()`).

5. **Binary data transfer between JS and WASM heap corruption** — Font files are large (100KB-10MB). WASM heap growth invalidates ArrayBuffer views (`HEAPU8`). Preferred approach: JavaScript writes font data to virtual FS using `FS.writeFile()`, passes only the file path string back to C++. This avoids pointer-passing and heap growth issues entirely.

6. **Infinite recursion in font substitution after registration** — If the registered font's internal family name doesn't match fontconfig's query (case differences, PostScript name vs. family name), fontconfig still returns empty, triggering another resolution attempt. Mitigate: Maintain `static std::set<OUString> sAttemptedFonts` and check before calling JavaScript. Limit recursion depth.

7. **FcConfigAppFontAddFile requires filesystem path, not URL** — LibreOffice uses `file://` URLs internally, but fontconfig expects native paths. Use `OUStringToOString()` conversion via `INetURLObject::GetFull()` as `PrintFontManager::addFontconfigFile()` does. Verify `FcConfigAppFontAddFile` returns `FcTrue` via SAL_LOG.

## Implications for Roadmap

Based on research, suggested phase structure:

### Phase 1: Core Hook Implementation
**Rationale:** Establish the fundamental C++ to JavaScript bridge and prove end-to-end font resolution. All subsequent work depends on this. JSPI configuration and data transfer patterns must be validated before building features on top.

**Delivers:**
- Single-font resolution (family name only, no style hints)
- EM_ASYNC_JS function with JSPI suspension
- JavaScript font provider with in-memory cache
- Write to virtual FS + AddTempDevFont registration
- Basic negative cache (don't retry failed fonts)
- SAL_INFO logging for debugging

**Addresses (from FEATURES.md):**
- Font family name request
- Raw font data return
- Null/failure return
- TTF/OTF support
- Synchronous C++ to JS call via JSPI
- Write-to-virtual-FS + AddTempDevFont
- Negative result caching

**Avoids (from PITFALLS.md):**
- EM_JS vs EM_ASYNC_JS confusion (use EM_ASYNC_JS from day one)
- Missing JSPI_EXPORTS entry (add to build system immediately)
- Binary data corruption (use FS.writeFile pattern, not pointer passing)
- Infinite recursion (implement negative cache in first version)

**Research needed:** No. All patterns are proven in codebase. JSPI usage matches vcl/source/app/scheduler.cxx. EM_JS matches PrimaryBindings.cxx. AddTempDevFont matches freetypetextrender.cxx.

### Phase 2: Font Variant Support
**Rationale:** Documents typically use multiple weights/styles of the same family (e.g., Arial Regular + Arial Bold). Without style hints, JavaScript must guess or return all variants. With hints, it returns the exact match. Low implementation cost (add integer parameters) with high user value.

**Delivers:**
- Forward FontWeight, FontItalic, FontWidth, FontPitch to JavaScript
- Multi-variant batch return (JavaScript returns array of fonts)
- TTC support validation (confirm CJK .ttc files register correctly)

**Uses (from STACK.md):**
- Same EM_ASYNC_JS pattern, extended parameter list
- FontSelectPattern attribute fields already available

**Implements (from ARCHITECTURE.md):**
- Enhancement to WasmFontResolver::tryResolveFont() signature
- Loop in registration to handle multiple font files

**Avoids (from PITFALLS.md):**
- One-at-a-time font resolution performance trap (batch return reduces roundtrips)

**Research needed:** No. Font attribute enums are documented in fontattributes.hxx. Batch return is simple array iteration.

### Phase 3: Build Optimization and Edge Cases
**Rationale:** With core functionality proven, optimize the WASM binary size and font packaging. Address CJK/large font memory pressure and font menu enumeration for interactive use.

**Delivers:**
- Minimal bundled font set (OpenSymbol + Liberation + Carlito/Caladea, exclude ~130 other fonts)
- Language/script hint for CJK font selection
- Font enumeration API for populating font menus
- Timeout/cancellation for JavaScript resolver

**Addresses:**
- --without-fonts pitfall (selective exclusion, not blanket removal)
- Large CJK fonts in MEMFS performance trap (consider NODEFS or streaming)
- Font menu requirement for interactive (non-headless) use

**Research needed:** Yes, for NODEFS/WORKERFS optimization if memory pressure is observed during CJK document testing. Standard patterns otherwise.

### Phase 4: Production Hardening
**Rationale:** Add robustness features discovered during real-world testing. This phase adapts based on Phase 1-3 findings.

**Delivers:**
- Persistent font cache (IndexedDB via IDBFS) if needed
- Font license validation hooks if required
- UX improvements (loading indicators, font substitution feedback)
- Security hardening (font data validation, path sanitization)

**Addresses:**
- Security mistakes from PITFALLS.md (FreeType exploit surface, path traversal)
- UX pitfalls (blocking render, silent substitution)

**Research needed:** Yes, for IDBFS if implementing persistent cache. UX patterns depend on integration context (Electron vs. browser).

### Phase Ordering Rationale

- **Phase 1 first:** All work depends on the JSPI bridge working. Validate the hardest technical constraint (async-to-sync bridge) before building features.
- **Phase 2 before Phase 3:** Font variant support is high-value, low-risk. Delivers immediate UX improvement (correct Bold/Italic) before tackling build optimization.
- **Phase 3 before Phase 4:** Build optimization reveals memory and performance characteristics needed to inform Phase 4 decisions.
- **Phases are incremental:** Each phase delivers a working system. Can ship after Phase 1 (basic font resolution), Phase 2 (good font resolution), Phase 3 (optimized), or Phase 4 (production-hardened).

### Research Flags

Phases likely needing deeper research during planning:
- **Phase 3:** NODEFS/WORKERFS for large font optimization — niche Emscripten FS API, limited documentation, platform-specific behavior.
- **Phase 4:** IDBFS persistent cache — IndexedDB integration in WASM has edge cases (quota management, async mount).

Phases with standard patterns (skip research-phase):
- **Phase 1:** All patterns proven in codebase (JSPI in scheduler.cxx, EM_JS in PrimaryBindings.cxx, AddTempDevFont in freetypetextrender.cxx).
- **Phase 2:** Font attributes are well-documented VCL structures. Batch return is simple iteration.

## Confidence Assessment

| Area | Confidence | Notes |
|------|------------|-------|
| Stack | HIGH | All technologies already in use in LibreOffice WASM build. JSPI configuration verified in makefile. EM_JS patterns verified in 6+ locations. MEMFS verified via FORCE_FILESYSTEM=1. |
| Features | HIGH | All features derived from direct analysis of VCL font subsystem source code. FontSelectPattern, PhysicalFontCollection, and substitution chain fully inspected. |
| Architecture | HIGH | Hook point verified in fontsubst.cxx. AddTempDevFont pipeline verified in freetypetextrender.cxx. All component boundaries confirmed in source. |
| Pitfalls | MEDIUM-HIGH | Critical pitfalls (JSPI deadlock, EM_ASYNC_JS, heap corruption) verified from codebase and Emscripten patterns. Main thread deadlock risk inferred from threading model analysis. Browser JSPI support based on training data (web verification unavailable). |

**Overall confidence:** HIGH

### Gaps to Address

**Electron version in use:** The downstream Diffchecker Electron app's version determines JSPI support availability. Electron >= 30 required for JSPI (Chromium 124). If the app uses Electron 29 or older, fallback to synchronous-only mode (no EM_ASYNC_JS, pre-load all fonts before document load). **Action:** Confirm Electron version during planning. If < 30, design Phase 1 for synchronous-only JavaScript resolver using `ipcRenderer.sendSync`.

**JSPI export coverage:** Does the font substitution code path run through a JSPI-exported function? The Qt6 WASM mode exports `_emscripten_check_mailbox` which covers the main event loop. But font substitution is called during layout, which may or may not be in that path. If the `FindFontSubstitute` call site is not covered, the new EM_ASYNC_JS function's export must be added to JSPI_EXPORTS. **Action:** Runtime verification in Phase 1. Test font resolution during document layout, verify no JSPI trap.

**Non-Qt headless mode JSPI:** The existing JSPI infrastructure is gated on `ENABLE_QT6 && HAVE_EMSCRIPTEN_JSPI && !HAVE_EMSCRIPTEN_PROXY_TO_PTHREAD`. The target build is headless SVP (no Qt). If JSPI is not active in this mode, synchronous-only JavaScript is required. **Action:** Verify build configuration during planning. Check if `HAVE_EMSCRIPTEN_JSPI` is set for headless builds. If not, plan for synchronous Electron IPC only.

**Memory pressure from large fonts:** Fonts are copied from JS heap to WASM heap (via `_malloc`), then written to MEMFS. A typical font is 200KB-2MB. CJK fonts can be 10-20MB. Multiple large fonts could exhaust WASM memory. **Action:** Monitor during Phase 2 testing with CJK documents. If memory pressure observed, move NODEFS optimization to Phase 2 instead of Phase 3.

## Sources

### Primary (HIGH confidence)
- LibreOffice codebase at `/Users/cadenz/Dev/lode/dev/core/`:
  - `solenv/gbuild/platform/EMSCRIPTEN_INTEL_GCC.mk` — JSPI build configuration, FORCE_FILESYSTEM, EXPORTED_RUNTIME_METHODS
  - `configure.ac` — JSPI and PROXY_TO_PTHREAD configure options, Emscripten version requirements
  - `vcl/qt5/QtInstance.cxx` — Working JSPI patterns (emscripten_proxy_promise, emscripten_promise_await)
  - `vcl/source/app/scheduler.cxx` — JSPI-aware task scheduling
  - `static/source/unoembindhelpers/PrimaryBindings.cxx` — EM_JS patterns for heap manipulation
  - `bridges/source/cpp_uno/gcc3_wasm/cpp2uno.cxx` — EM_JS pattern with UTF8ArrayToString
  - `vcl/unx/generic/gdi/freetypetextrender.cxx` — AddTempDevFont implementation
  - `vcl/unx/generic/fontmanager/fontmanager.cxx` — addFontFile implementation
  - `vcl/unx/generic/fontmanager/fontconfig.cxx` — addFontconfigFile, FcConfigAppFontAddFile
  - `vcl/unx/generic/fontmanager/fontsubst.cxx` — FcPreMatchSubstitution, hook point
  - `vcl/source/font/PhysicalFontCollection.cxx` — FindFontFamily orchestrator
  - `vcl/inc/font/FontSelectPattern.hxx` — Font request data structure
  - `vcl/inc/fontattributes.hxx` — Font attribute fields

### Secondary (MEDIUM confidence)
- V8 blog JSPI documentation — JSPI browser support timeline (Chrome 123, Electron 30)
- Electron release schedule — Chromium version mapping
- Emscripten Asyncify documentation — Binary size overhead claims (20-50%)

### Tertiary (LOW confidence)
- ZetaOffice WASM project — Mentioned in training data, could not verify. Likely bundles fonts, no dynamic resolution.
- Collabora Online (COWASM) — JSPI contributions verified in codebase, font loading approach unverified.

---
*Research completed: 2026-02-09*
*Ready for roadmap: yes*
