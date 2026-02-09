# Pitfalls Research

**Domain:** LibreOffice WASM dynamic font resolution (C++/Emscripten/fontconfig/FreeType)
**Researched:** 2026-02-09
**Confidence:** MEDIUM-HIGH (codebase-verified + domain expertise; web verification unavailable for some Emscripten-specific claims)

## Critical Pitfalls

### Pitfall 1: EM_JS Returns Synchronously But JS Font Resolution Is Async

**What goes wrong:**
`EM_JS` generates a synchronous C function. If the JS body contains `await` or returns a Promise, the C++ caller does not suspend -- it gets `undefined` (cast to 0/null). The font data never arrives, and `AddTempDevFont()` receives garbage or an empty buffer. Builds appear to work but every font resolution silently fails.

**Why it happens:**
Developers conflate `EM_JS` (synchronous JS interop) with `EM_ASYNC_JS` (which requires ASYNCIFY or JSPI to suspend the C++ caller). The Emscripten documentation is ambiguous about this distinction. In many tutorials, `EM_JS` examples use only synchronous JS, so developers assume adding `await` inside `EM_JS` will "just work."

**How to avoid:**
Use `EM_ASYNC_JS` (not `EM_JS`) for the font resolution callback. `EM_ASYNC_JS` requires either `-sASYNCIFY` or `-sJSPI` at link time. Since LibreOffice already conditionally enables JSPI (`ENABLE_EMSCRIPTEN_JSPI` in `solenv/gbuild/platform/EMSCRIPTEN_INTEL_GCC.mk` line 33), use `EM_ASYNC_JS` and guard the entire block with:
```cpp
#if defined(EMSCRIPTEN) && HAVE_EMSCRIPTEN_JSPI
```
If JSPI is not available, fall back to synchronous-only resolution (pre-loaded fonts) rather than attempting ASYNCIFY, which has massive binary size overhead.

**Warning signs:**
- Font resolution callback returns 0-length data
- `SAL_WARN` in `AddTempDevFont` path saying "addFontFile returned empty"
- Documents render with fallback fonts despite JS callback being "registered"
- No errors in the browser console (the Promise just silently resolves after C++ has already moved on)

**Phase to address:**
Phase 1 (Core Hook Implementation) -- this is the very first design decision and gets everything else wrong if missed.

---

### Pitfall 2: JSPI_EXPORTS Must Include the EM_ASYNC_JS Function's Mangled Name

**What goes wrong:**
When using JSPI, any function that can suspend (i.e., any `EM_ASYNC_JS` function) must be listed in `-sJSPI_EXPORTS`. If it is not listed, the WASM module will trap at runtime when the function tries to suspend. The error is a cryptic `RuntimeError: unreachable` or `WebAssembly.RuntimeError: Suspending via JSPI is not allowed from the main thread` depending on browser and configuration.

**Why it happens:**
LibreOffice's current JSPI configuration (line 36 of `EMSCRIPTEN_INTEL_GCC.mk`) only exports two specific mangled symbols. Developers adding a new `EM_ASYNC_JS` function forget to add its generated C symbol name to `JSPI_EXPORTS`. The Emscripten docs don't make it obvious that `EM_ASYNC_JS`-generated functions need explicit JSPI_EXPORTS entries.

**How to avoid:**
After defining your `EM_ASYNC_JS` function (e.g., `em_resolveFontFromHost`), add its exact symbol name to the `JSPI_EXPORTS` list in `solenv/gbuild/platform/EMSCRIPTEN_INTEL_GCC.mk`. For `EM_ASYNC_JS(int, em_resolveFontFromHost, (...), {...})`, the exported symbol is `_em_resolveFontFromHost` (underscore-prefixed). Test with `-sJSPI_EXPORTS=@file` if the list grows unwieldy. Alternatively, use `JSPI_EXPORTS=['*']` during development (matches all -- not for production due to performance overhead).

**Warning signs:**
- `RuntimeError: unreachable` when the font resolution path is first hit
- Works in non-JSPI builds (ASYNCIFY) but fails in JSPI builds
- The exact same code works if you make the JS body synchronous (no `await`)

**Phase to address:**
Phase 1 (Core Hook Implementation) -- must be correct in the build system from day one.

---

### Pitfall 3: Calling EM_ASYNC_JS From the Main Browser Thread Deadlocks

**What goes wrong:**
JSPI suspends the calling WASM function and returns a Promise to the JS event loop. But if the caller is on the main browser thread, suspending it freezes the event loop, which means the Promise can never resolve. Result: permanent deadlock, browser tab hangs.

**Why it happens:**
LibreOffice WASM has two threading models:
1. **Qt5 mode (non-PROXY_TO_PTHREAD):** The main LO logic runs on the main browser thread. Font resolution happens on this thread. JSPI suspension here deadlocks.
2. **Qt6 JSPI mode:** Uses `emscripten_proxy_promise` to move work off the main thread (see `vcl/source/app/scheduler.cxx` line 596-607). But font substitution (`FindFontSubstitute`) is called from the rendering pipeline, which may or may not be on a worker thread.

The `SvpSalInstance` headless mode (what our WASM build uses for headless PDF conversion) runs the event loop via `emscripten_set_main_loop` (see `vcl/headless/svpinst.cxx` line 303). The main thread processes VCL tasks, and font resolution is called during layout, which happens during those tasks.

**How to avoid:**
Two strategies, choose based on threading model:

**Strategy A (Headless/PROXY_TO_PTHREAD mode):** If the build uses `-sPROXY_TO_PTHREAD`, the "main" C++ code runs in a worker thread, and JSPI can suspend it safely because the browser main thread's event loop remains free. Verify this is the case for your build configuration. The README.wasm.md mentions `-sPROXY_TO_PTHREAD` as desirable but notes Qt5 doesn't support it.

**Strategy B (Pre-resolution, avoiding suspension during layout):** Instead of doing async font resolution inside `FindFontSubstitute()`, pre-resolve fonts before document layout begins. Parse the document's font requirements, call the async JS resolution for all needed fonts, then start layout. This avoids the synchronous-call-from-render-path problem entirely.

**Strategy C (Worker thread proxy):** Use `emscripten_proxy_promise` (like the scheduler does in the Qt6 JSPI path) to proxy the font resolution call to a worker thread that can safely suspend. This is complex but proven in the codebase.

**Warning signs:**
- Browser tab freezes (not crashes) when opening a document with a missing font
- Works in Node.js (no browser main thread constraint) but fails in browser
- Works for the first font (if resolved before main loop starts) but hangs on subsequent ones

**Phase to address:**
Phase 1 (Architecture Decision) -- the threading strategy must be decided before writing any code.

---

### Pitfall 4: FindFontSubstitute() Is a const Method -- Side Effects Are Fragile

**What goes wrong:**
`FcPreMatchSubstitution::FindFontSubstitute()` is declared `const` (see `vcl/inc/font/fontsubstitution.hxx` line 64). Modifying state (writing files to VFS, registering fonts with fontconfig/FreeType, calling `AddTempDevFont()`) from within a `const` method requires either `mutable` members or casting away const. More critically, `AddTempDevFont()` calls `ImplClearAllFontData(true)` / `ImplRefreshAllFontData(true)` (see `vcl/source/gdi/embeddedfontsmanager.cxx` lines 407-418), which invalidates the font collection that is *currently being iterated over* by the caller of `FindFontSubstitute`.

**Why it happens:**
The font substitution interface was designed for read-only queries (ask fontconfig for a substitute name). It was never intended for side effects like registering new fonts. Developers naturally want to put the "resolve and register" logic at the exact point where the missing font is detected, but this violates the interface contract.

**How to avoid:**
Do NOT call `AddTempDevFont()` from within `FindFontSubstitute()`. Instead:
1. Inside `FindFontSubstitute()`, detect the missing font and call EM_ASYNC_JS to get the font data.
2. Write the font data to the virtual FS.
3. Call `FcConfigAppFontAddFile()` to register with fontconfig (this is safe -- it just adds to the app font set).
4. Call `PrintFontManager::addFontFile()` to register with the font manager.
5. Return `false` from `FindFontSubstitute()` (font still not substituted).
6. The caller will retry font resolution, and this time fontconfig will find the newly registered font through its normal path.

Alternatively, split the logic: register the font in a separate step, then invalidate the font cache, and let the natural retry mechanism pick it up. The `EmbeddedFontsManager::activateFonts()` pattern (line 510 in `embeddedfontsmanager.cxx`) shows how to properly bracket font registration with `ImplClearAllFontData` / `ImplRefreshAllFontData`.

**Warning signs:**
- Crash or assertion failure in `PhysicalFontCollection` during iteration
- Font appears registered (file exists on VFS) but never gets used
- `DBG_TESTSOLARMUTEX()` assertion failures
- Infinite loop: `FindFontSubstitute` registers font, invalidates cache, gets called again, registers again...

**Phase to address:**
Phase 1 (Core Hook Implementation) -- the registration strategy is the core architectural decision.

---

### Pitfall 5: FcConfigAppFontAddFile Requires a Real Filesystem Path, Not a URL

**What goes wrong:**
LibreOffice internally uses file URLs (`file:///path/to/font.ttf`) for font paths. But `FcConfigAppFontAddFile()` (called by `PrintFontManager::addFontconfigFile()` at `fontconfig.cxx` line 785) expects a native filesystem path, not a URL. In the WASM virtual filesystem, the path must be a valid MEMFS/NODEFS path. If you pass a URL or a path to a non-existent VFS location, `FcConfigAppFontAddFile` returns `FcFalse` and the font is silently not registered.

**Why it happens:**
The `addFontFile()` method in `fontmanager.cxx` (line 141) converts URLs to paths using `INetURLObject` and `osl_getThreadTextEncoding()`. Under Emscripten, the path encoding and VFS mount points may differ from what fontconfig expects. Developers write the font to one VFS path but fontconfig looks at a different resolution of that path.

**How to avoid:**
1. Write font data to a well-known VFS directory (e.g., `/tmp/fonts/` or a directory you know is mounted in MEMFS).
2. Ensure the directory exists in the VFS before writing. Call `FS.mkdir()` from JS or use `osl::Directory::createPath()` from C++.
3. Convert the path correctly: use `OUStringToOString(aPath.GetFull(), osl_getThreadTextEncoding())` like `addFontFile()` does.
4. Verify `FcConfigAppFontAddFile` returns `FcTrue`. The existing code at line 788-790 logs this, so enable SAL_LOG for `vcl.fonts` during development.

**Warning signs:**
- `SAL_INFO("vcl.fonts", "FcConfigAppFontAddFile(\"...\") => false")` in logs
- Font file exists on VFS (can verify with Emscripten `FS.readdir()`) but fontconfig doesn't see it
- `addFontFile()` returns an empty vector

**Phase to address:**
Phase 1 (Core Hook Implementation) -- file path handling must be correct for the registration to work at all.

---

### Pitfall 6: Binary Data Transfer Between JS and WASM Heap Corruption

**What goes wrong:**
Font files are large (100KB-10MB). When transferring binary data from JS to WASM, developers commonly:
1. Allocate WASM heap memory from JS (`Module._malloc(size)`), write data with `Module.HEAPU8.set()`, then pass the pointer to C++. But the WASM heap can be resized (via `memory.grow`), which invalidates all `ArrayBuffer` views (`HEAPU8`, `HEAPU16`, etc.). If a memory growth happens between the `_malloc` call and the `HEAPU8.set()` call, the write goes to the old (detached) buffer.
2. Return a pointer from JS to C++ via `EM_ASYNC_JS` -- but the pointer was allocated on the JS side, and C++ tries to `free()` it later, or vice versa.
3. Use `stringToUTF8` for binary data (which stops at null bytes).

**Why it happens:**
Font files are large enough to trigger WASM memory growth. The standard pattern of malloc-then-set has a TOCTOU window. Additionally, Emscripten's `HEAPU8` is a view that becomes detached on memory growth, and this is not obvious from the API.

**How to avoid:**
Use `EM_ASYNC_JS` to return the size, then separately copy data:
```javascript
// In EM_ASYNC_JS: write to FS, return path length (not binary data directly)
FS.writeFile('/tmp/fonts/myfont.ttf', new Uint8Array(fontData));
return pathLength;
```
**Preferred approach:** Write the font data to the Emscripten virtual filesystem from JS (using `FS.writeFile()`), then pass only the file path (as a string) back to C++. The C++ side reads from the VFS using standard file I/O. This completely avoids heap pointer passing and memory growth issues.

**Warning signs:**
- Corrupted font data (FreeType reports parse errors, `OpenTTFontBuffer` returns `SFErrCodes` errors)
- Intermittent crashes that depend on font file size (small fonts work, large fonts crash)
- `TypeError: Cannot perform Construct on a detached ArrayBuffer` in browser console
- Different behavior between debug (ASSERTIONS=1, which checks for detached buffers) and release builds

**Phase to address:**
Phase 1 (Data Transfer Design) -- the data passing strategy is foundational.

---

### Pitfall 7: Infinite Recursion in Font Substitution After Registration

**What goes wrong:**
The pattern "detect missing font in `FindFontSubstitute` -> resolve from JS -> register font -> retry" can create an infinite loop if:
1. The registered font's family name doesn't exactly match what fontconfig expects (case sensitivity, spacing differences, PostScript vs. family name).
2. The JS callback returns font data but the font doesn't contain the glyphs needed, causing `FcGlyphFallbackSubstitution` to trigger another resolution.
3. Fontconfig's substitution rules transform the font name before matching, so the registered font with the "correct" name still doesn't match the transformed query.

**Why it happens:**
Font naming is notoriously inconsistent. A document requests "Arial", fontconfig may normalize this to "arial" or transform it via substitution rules. The host system might provide `Arial.ttf` which self-identifies as "Arial" in its name table, but fontconfig's matching algorithm considers weight/slant/width as well. The `uselessmatch()` function in `fontsubst.cxx` (line 74) checks multiple attributes, not just the name.

**How to avoid:**
1. Implement a "tried" set (per-session or per-document): if you've already attempted to resolve font family X from JS, don't try again. The `maCachedFontMap` in `FcPreMatchSubstitution` (line 40) provides a model but only caches successful substitutions.
2. Add a `static std::set<OUString> sAttemptedFonts` to track which font families have been sent to JS. Check this before calling `EM_ASYNC_JS`.
3. Limit the recursion depth: if `FindFontSubstitute` has been re-entered more than N times for the same font request, bail out.

**Warning signs:**
- Browser tab becomes unresponsive (spinning, not crashed)
- `SAL_INFO("vcl.fonts")` shows the same font being resolved repeatedly
- Emscripten stack overflow error (deep recursion exhausts the WASM stack, which is only 131072 bytes per `EMSCRIPTEN_INTEL_GCC.mk` line 25)

**Phase to address:**
Phase 1 (Core Hook Implementation) -- the recursion guard is part of the core logic.

---

### Pitfall 8: --without-fonts Removes OpenSymbol, Breaking Symbol Rendering

**What goes wrong:**
Using `--without-fonts` sets the `SCPDEFS` macro `-DWITHOUT_FONTS` and removes the `MORE_FONTS` build type (see `configure.ac` line 14420-14431). This excludes *all* bundled third-party fonts. But the WASM fs image (from `CustomTarget_emscripten_fs_image.mk`) separately includes `opens___.ttf` (OpenSymbol) at line 1712: `$(INSTROOT)/$(LIBO_SHARE_RESOURCE_FOLDER)/common/fonts/opens___.ttf`. So OpenSymbol *might* still be bundled if the package rules don't strip it. However, the `fc_local.conf` entries that set up fontconfig substitution rules (e.g., mapping "Symbol" to "OpenSymbol") depend on the `MORE_FONTS` snippet being included. Without those rules, fontconfig doesn't know about the substitutions.

**Why it happens:**
`--without-fonts` is a coarse flag designed for systems where fonts are pre-installed. In WASM, we want a selective approach: bundle OpenSymbol + Liberation family as minimal fallbacks, but exclude the ~130 other font files. The flag doesn't support this selective behavior.

**How to avoid:**
Do NOT use `--without-fonts` directly. Instead:
1. Keep `--with-fonts` (the default for Emscripten builds, per `configure.ac` line 3458).
2. Modify the WASM fs image packaging (`CustomTarget_emscripten_fs_image.mk`) to exclude specific font files while keeping `opens___.ttf`, the Liberation family, Carlito, Caladea, and `fc_local.conf`.
3. Alternatively, create a custom configuration variable (e.g., `--with-minimal-fonts`) that includes only the essential fallback set.
4. Verify `fc_local.conf` still includes the font substitution rules for common fonts (the `MORE_FONTS` snippet at `external/more_fonts/fc_local.snippet`).

**Warning signs:**
- Document rendering replaces all symbol characters with boxes/tofu
- `IsOpenSymbol()` check in `FindFontSubstitute` (line 106) returns true but the font file is missing
- Fontconfig logs show no substitution rules being loaded

**Phase to address:**
Phase 2 (Build Configuration) -- after the core hook works with a full font set, optimize the bundled set.

---

## Technical Debt Patterns

| Shortcut | Immediate Benefit | Long-term Cost | When Acceptable |
|----------|-------------------|----------------|-----------------|
| Hardcoding VFS font path (e.g., `/tmp/fonts/`) | Quick to implement | Breaks if Emscripten FS layout changes, conflicts with other temp files | MVP only; replace with configurable path before release |
| Using `JSPI_EXPORTS=['*']` | No need to track mangled names | Performance overhead: JSPI wraps every export | Development only; never in production builds |
| Skipping `ImplClearAllFontData`/`ImplRefreshAllFontData` cycle after registration | Avoids complexity of cache invalidation | Newly registered fonts invisible to layout engine until next full font refresh | Never -- fonts won't be usable without cache refresh |
| Caching resolved fonts only in memory (no disk persistence) | Simpler implementation | Every session re-downloads the same fonts from host | Acceptable for MVP; host-side caching can compensate |
| Using `EM_ASM` instead of `EM_JS`/`EM_ASYNC_JS` | Inline code, no separate function | Cannot return values cleanly, no type safety, cannot be async | Never for this use case |

## Integration Gotchas

| Integration | Common Mistake | Correct Approach |
|-------------|----------------|------------------|
| fontconfig (`FcConfigAppFontAddFile`) | Passing a file:// URL instead of native path | Use `OUStringToOString()` conversion through `INetURLObject::GetFull()`, as `PrintFontManager::addFontconfigFile()` does |
| FreeType (`FreetypeManager::AddFontFile`) | Registering with FreeType but not fontconfig (or vice versa) | Must register with both: `addFontconfigFile()` adds to fontconfig's app font set, then `analyzeFontFile()` adds to PrintFontManager, then `FreetypeManager::AddFontFile()` adds to glyph cache. Use `PrintFontManager::addFontFile()` which orchestrates all three. |
| Emscripten FS (`FS.writeFile`) | Writing from JS worker thread without `FS.mount()` on that thread | MEMFS is shared across threads in Emscripten when using pthreads. But NODEFS (if used) requires mounting per-thread. Stick with MEMFS for the temp font directory. |
| `AddTempDevFont()` | Calling without holding the SolarMutex | The method accesses `mpGraphics` and modifies font collections. Must hold SolarMutex. `DBG_TESTSOLARMUTEX()` will catch this in debug builds. |
| `PhysicalFontCollection` | Modifying the font collection while iterating (e.g., during `GetDevFontList`) | Register fonts *before* layout begins, or bracket with `ImplClearAllFontData` / `ImplRefreshAllFontData` which properly rebuilds all caches and font lists. See the `UpdateFontsGuard` RAII class in `embeddedfontsmanager.cxx` line 407. |

## Performance Traps

| Trap | Symptoms | Prevention | When It Breaks |
|------|----------|------------|----------------|
| Resolving fonts one-at-a-time during layout | Document rendering is extremely slow; each font triggers a round-trip to JS/Electron/main process | Batch: parse document font requirements upfront, resolve all missing fonts in one async call before layout | Any document with >3 custom fonts |
| Re-resolving previously failed fonts | JS returns null for a font not on the system, but C++ retries every time a paragraph uses that font | Maintain a negative cache (`sAttemptedFonts` set) with TTL or per-document lifetime | Documents with many paragraphs using unavailable fonts |
| `FcConfigAppFontAddFile` re-scans all app fonts | Each call to `addFontconfigFile` triggers `rWrapper.addFontSet(FcSetApplication)` which re-processes the entire application font set | Register all fonts to fontconfig first, then call `addFontSet` once | >10 fonts registered dynamically per session |
| Large font files in MEMFS consuming WASM memory | CJK fonts can be 10-20MB each; multiple large fonts exhaust the 1GB initial memory | Consider streaming or using NODEFS/WORKERFS to keep font data on the host side rather than in WASM heap. Alternatively, increase `-sTOTAL_MEMORY` or use `-sALLOW_MEMORY_GROWTH` | Loading documents with CJK, Arabic, or other large-charset fonts |

## Security Mistakes

| Mistake | Risk | Prevention |
|---------|------|------------|
| Trusting arbitrary font data from JS without validation | Malformed font files could exploit FreeType parsing vulnerabilities (historically a rich attack surface) | Run `OpenTTFontBuffer()` validation (as `embeddedfontsmanager.cxx` does) before registering. Also check `sufficientTTFRights()` for licensing compliance. |
| Exposing font family names to untrusted JS contexts | Font enumeration is a browser fingerprinting vector; leaking which fonts a document requests could reveal document content | Only expose font names to the trusted host (Electron main process), never to arbitrary web pages. The Electron IPC channel is trusted. |
| Writing font files to predictable paths without sanitization | Path traversal if font family name contains `../` or special characters | Use `rtl::Uri::encode()` for filenames (as `EmbeddedFontsManager::getFileUrlForTemporaryFont()` does at line 128) or generate UUIDs for temp font filenames |

## UX Pitfalls

| Pitfall | User Impact | Better Approach |
|---------|-------------|-----------------|
| Blocking document rendering while fonts resolve | User sees a frozen/blank page for seconds while fonts download from host | Show a loading indicator; or render with fallback fonts first, then re-render when system fonts arrive (progressive rendering) |
| Silent font substitution with no feedback | Documents look "wrong" but user doesn't know why (wrong font was silently used) | Log which fonts were resolved from host vs. substituted by fontconfig. Expose this via SAL_LOG for debugging. |
| Timeout-less JS font resolution | If the Electron main process is slow or unresponsive, the WASM module hangs forever | Add a timeout to the `EM_ASYNC_JS` Promise (e.g., `Promise.race([resolve(), timeout(5000)])`) and fall back gracefully |

## "Looks Done But Isn't" Checklist

- [ ] **Font registration:** Font file written to VFS and `FcConfigAppFontAddFile` returns true -- but `FreetypeManager::AddFontFile()` was never called, so glyph rendering still uses fallback. Verify the *full* `AddTempDevFont()` chain executes.
- [ ] **JSPI export:** `EM_ASYNC_JS` function defined and JS callback works in isolation -- but the function's symbol is not in `JSPI_EXPORTS`, so it traps on first actual call from the WASM render pipeline. Verify by actually opening a document with a missing font.
- [ ] **Font name matching:** Font data returned by JS and registered -- but the font's internal family name (in the TTF name table) doesn't match what fontconfig is searching for. Verify with `fc-query` (or FreeType's `FT_Get_Sfnt_Name`) that the registered font's family name matches the document's font request.
- [ ] **Cache invalidation:** Font registered but `PhysicalFontCollection` still has the old (pre-registration) font list cached. Verify with `OutputDevice::ImplUpdateAllFontData(true)` or equivalent cache flush.
- [ ] **SolarMutex held:** Font registration code works in unit tests but crashes in the actual WASM runtime because the SolarMutex is not held. Verify with debug builds (`DBG_TESTSOLARMUTEX` assertion).
- [ ] **Build with and without JSPI:** Code works with `HAVE_EMSCRIPTEN_JSPI=1` -- but `#ifdef` guards are wrong and the build breaks when JSPI is disabled. Verify both configurations.
- [ ] **Negative path:** JS callback returns null (font not available on system) -- but the C++ code doesn't handle this and tries to register a 0-byte font file. Verify the null/empty response path.

## Recovery Strategies

| Pitfall | Recovery Cost | Recovery Steps |
|---------|---------------|----------------|
| Infinite recursion in FindFontSubstitute | LOW | Add `static std::set` recursion guard. Redeploy. No data loss. |
| Wrong JSPI_EXPORTS (runtime trap) | LOW | Add missing symbol to `EMSCRIPTEN_INTEL_GCC.mk`, rebuild. |
| Heap corruption from JS-to-WASM data transfer | HIGH | Redesign data transfer to use FS.writeFile + path passing instead of pointer passing. Requires reworking the EM_ASYNC_JS interface. |
| Font collection invalidated during iteration | MEDIUM | Restructure to register fonts outside the substitution callback. May require architectural change to pre-resolve fonts before layout. |
| Main thread deadlock (JSPI on main thread) | HIGH | Requires architectural change: either move to PROXY_TO_PTHREAD, implement pre-resolution, or use worker thread proxy pattern. Cannot be patched incrementally. |
| --without-fonts breaks OpenSymbol/fc_local.conf | LOW | Switch to custom minimal font set in WASM fs image. Rebuild. |

## Pitfall-to-Phase Mapping

| Pitfall | Prevention Phase | Verification |
|---------|------------------|--------------|
| EM_JS vs EM_ASYNC_JS confusion | Phase 1: Core Hook | Unit test that verifies async round-trip returns valid font data |
| Missing JSPI_EXPORTS entry | Phase 1: Core Hook | Integration test that triggers font resolution in actual WASM runtime |
| Main thread deadlock | Phase 1: Architecture Decision | Test font resolution from within the event loop under real browser conditions |
| const method side effects | Phase 1: Core Hook | Code review; verify no `ImplClearAllFontData` called from within `FindFontSubstitute` |
| VFS path vs URL confusion | Phase 1: Core Hook | SAL_LOG verification that `FcConfigAppFontAddFile` returns true |
| Heap corruption in data transfer | Phase 1: Data Transfer Design | Test with large (>5MB) CJK font files; check for detached ArrayBuffer errors |
| Infinite recursion | Phase 1: Core Hook | Test with font name that cannot be resolved (JS returns null); verify no hang |
| --without-fonts side effects | Phase 2: Build Config | Verify OpenSymbol renders correctly; verify fc_local.conf substitution rules present |
| One-at-a-time font resolution perf | Phase 3: Optimization | Profile document load time with 10+ custom fonts; compare batch vs. sequential |
| Large CJK fonts in MEMFS | Phase 3: Optimization | Load a document with SimSun/MS Mincho; monitor WASM memory usage |

## Prior Art Research

### ZetaOffice (zetaoffice.net)

**Confidence:** LOW (unable to verify via web fetch; based on training data only)

ZetaOffice is a LibreOffice-based WASM project by Nicolo' Pretto / Nicola Gatto that provides a full in-browser office suite built on LibreOffice Technology. Based on available information, ZetaOffice focuses on running LO in the browser with a Qt6/WASM frontend. No specific evidence found of dynamic system font resolution -- they likely bundle fonts into the WASM data file. Their JSPI work (if any) would be focused on the Qt event loop integration, not font loading.

**Implication:** We are likely the first to attempt dynamic font loading from host. No prior solution to copy from.

### Collabora Online (COWASM)

**Confidence:** MEDIUM (referenced in `static/README.wasm.md`)

Collabora Online's WASM effort ("COWASM") builds LibreOffice headless for use as a document processing engine. Per `README.wasm.md`, this uses the headless (non-Qt) path with `--disable-gui`. Collabora Online traditionally handles font management server-side (fonts are installed on the server running the LO backend). In the WASM variant, fonts are bundled into the `.data` file.

Collabora contributed the JSPI/PROXY_TO_PTHREAD work visible in `comphelper/source/misc/emscriptenthreading.cxx` and the scheduler changes. Their focus was on making the Qt event loop work with WASM threading, not on font loading.

**Implication:** Collabora's JSPI infrastructure is available to us, but they haven't solved the font-loading-from-host problem. Their threading model decisions are relevant reference material.

### Community Patches

**Confidence:** LOW (no evidence found in git log or codebase)

No evidence found of community patches for dynamic WASM font loading in the LibreOffice codebase. The font-related Emscripten code is limited to:
- The `fc_local.conf` generation with Emscripten-specific rendering hints (`postprocess/CustomTarget_fontconfig.mk` line 25-31)
- OpenSymbol bundling in the WASM fs image
- Standard fontconfig/FreeType usage via the `vcl_headless_freetype_code` path

**Implication:** This is genuinely novel work within the LibreOffice ecosystem. Extra caution warranted -- no battle-tested patterns to follow.

## Sources

- LibreOffice source code analysis (HIGH confidence):
  - `vcl/unx/generic/fontmanager/fontsubst.cxx` -- font substitution hooks
  - `vcl/unx/generic/fontmanager/fontconfig.cxx` line 785 -- `addFontconfigFile` implementation
  - `vcl/unx/generic/fontmanager/fontmanager.cxx` line 141 -- `addFontFile` flow
  - `vcl/unx/generic/gdi/freetypetextrender.cxx` line 96 -- `AddTempDevFont` chain
  - `vcl/source/gdi/embeddedfontsmanager.cxx` -- existing dynamic font registration pattern
  - `solenv/gbuild/platform/EMSCRIPTEN_INTEL_GCC.mk` -- JSPI/build configuration
  - `vcl/source/app/scheduler.cxx` line 596 -- JSPI proxy pattern
  - `static/README.wasm.md` -- WASM build documentation
  - `vcl/headless/svpinst.cxx` -- Emscripten event loop
  - `configure.ac` -- `--without-fonts` and `with_fonts` logic
  - `postprocess/CustomTarget_fontconfig.mk` -- fc_local.conf generation
  - `static/CustomTarget_emscripten_fs_image.mk` -- WASM bundled files
- Emscripten documentation (MEDIUM confidence, from training data):
  - `EM_JS` vs `EM_ASYNC_JS` semantics
  - JSPI_EXPORTS requirements
  - WASM memory growth and ArrayBuffer detachment
  - PROXY_TO_PTHREAD threading model
- ZetaOffice / Collabora Online (LOW confidence -- training data only, could not verify via web)

---
*Pitfalls research for: LibreOffice WASM dynamic font resolution*
*Researched: 2026-02-09*
