# Phase 1: Core Font Resolution - Research

**Researched:** 2026-02-09
**Domain:** Emscripten C++/JS interop for runtime font resolution in LibreOffice WASM
**Confidence:** HIGH

## Summary

Phase 1 implements the core mechanism: when LibreOffice WASM requests a font that is not bundled, a JavaScript callback is invoked with the font family name, JavaScript writes the font file to the Emscripten virtual filesystem and returns the path, C++ registers the font via the existing `AddTempDevFont()` pipeline, and the font lookup retries successfully. The empty-string return path gracefully falls through to LibreOffice's existing fallback chain.

All core technologies are already proven in the LibreOffice WASM codebase. JSPI infrastructure exists (`-sJSPI`, `JSPI_EXPORTS` in `EMSCRIPTEN_INTEL_GCC.mk`). `EM_JS` patterns exist in 6+ locations (`PrimaryBindings.cxx`, `cpp2uno.cxx`, `initjsunoscripting.cxx`). The `AddTempDevFont()` pipeline in `freetypetextrender.cxx` handles fontconfig + PrintFontManager + FreetypeManager registration. The hook point at `FcPreMatchSubstitution::FindFontSubstitute()` in `fontsubst.cxx` is confirmed compiled for WASM via `vcl_headless_freetype_code` in `vcl/Library_vcl.mk`.

The critical technical decision is using `EM_ASYNC_JS` with `-sJSPI`. Emscripten documentation confirms `EM_ASYNC_JS` works with both `-sASYNCIFY` and `-sJSPI`, and that `JSPI_EXPORTS` are NOT needed for `EM_ASYNC_JS` functions (Emscripten handles this automatically). This corrects a claim in prior research (PITFALLS.md Pitfall 2) that the function's mangled name must be manually added to `JSPI_EXPORTS`. The data transfer strategy avoids heap corruption by having JavaScript write to the virtual FS via `FS.writeFile()` and returning only the file path string to C++.

**Primary recommendation:** Use `EM_ASYNC_JS` with `-sJSPI` for the font resolution callback. JS writes font data to VFS via `FS.writeFile()`, returns file path. C++ calls `AddTempDevFont()` to register. Guard with `#if defined(EMSCRIPTEN) && HAVE_EMSCRIPTEN_JSPI` for async path, with a synchronous `EM_JS` fallback for non-JSPI builds.

## Standard Stack

### Core

| Component | Version/Location | Purpose | Why Standard | Confidence |
|-----------|-----------------|---------|--------------|------------|
| `EM_ASYNC_JS` macro | Emscripten >= 3.1.46 | Async C++ to JS interop with JSPI suspension | Works with `-sJSPI` (confirmed in Emscripten docs). Generates async-capable C function without manual `JSPI_EXPORTS` entries. | HIGH |
| `-sJSPI` linker flag | Emscripten >= 3.1.46 | WASM stack suspension for async JS | Already in LibreOffice build (`EMSCRIPTEN_INTEL_GCC.mk` line 33-37). Zero binary overhead vs Asyncify. | HIGH |
| `FS.writeFile()` (JS) | Emscripten MEMFS | Write font data to virtual filesystem from JS | Avoids heap corruption from pointer passing. `FORCE_FILESYSTEM=1` already set. | HIGH |
| `AddTempDevFont()` | `vcl/unx/generic/gdi/freetypetextrender.cxx` line 96-129 | Font registration pipeline | Existing API: fontconfig + PrintFontManager + FreetypeManager. Quality boost of 5800. | HIGH |
| `FcPreMatchSubstitution::FindFontSubstitute()` | `vcl/unx/generic/fontmanager/fontsubst.cxx` line 100-172 | Hook point for WASM intercept | Called when fontconfig cannot find the requested font. Already has MRU cache. Compiled for WASM via `vcl_headless_freetype_code`. | HIGH |
| `PrintFontManager::addFontFile()` | `vcl/unx/generic/fontmanager/fontmanager.cxx` line 141-165 | Font file registration orchestrator | Calls `addFontconfigFile()` + `analyzeFontFile()`. Returns vector of fontIDs. | HIGH |

### Supporting

| Component | Location | Purpose | When to Use | Confidence |
|-----------|----------|---------|-------------|------------|
| `EM_JS` macro | Emscripten | Synchronous C++ to JS calls | Fallback path when JSPI is not available. Also for simple synchronous utility calls. | HIGH |
| `UTF8ToString` | Emscripten runtime | Convert C string pointer to JS string | Already in `EXPORTED_RUNTIME_METHODS` (line 30 of `EMSCRIPTEN_INTEL_GCC.mk`). | HIGH |
| `stringToNewUTF8` | Emscripten runtime | Convert JS string to C string pointer | For returning file path string from JS to C++. Caller must `free()` the result. | HIGH |
| `FcConfigAppFontAddFile()` | fontconfig | Register font file with fontconfig | Called internally by `PrintFontManager::addFontconfigFile()` at `fontconfig.cxx` line 785-799. | HIGH |
| `FreetypeManager::AnnounceFonts()` | `vcl/unx/generic/glyphs/freetype_glyphcache.cxx` | Push fonts into PhysicalFontCollection | Called by `AddTempDevFont()` to make new fonts visible to font lookups. | HIGH |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| `EM_ASYNC_JS` | `EM_JS` (synchronous only) | Works without JSPI but requires synchronous JS callback (e.g., `ipcRenderer.sendSync` in Electron). Cannot support async font resolution from browser fetch. Use as fallback for `!HAVE_EMSCRIPTEN_JSPI`. |
| `FS.writeFile()` from JS | POSIX `fwrite()` from C++ with heap pointer passing | Requires `Module._malloc()` + `HEAPU8.set()` which risks heap corruption on memory growth. FS.writeFile is safer. |
| Asyncify (`-sASYNCIFY`) | N/A | 20-50% binary size overhead, complex whitelist management. LibreOffice does not use it. Not acceptable. |
| `emscripten::val` | N/A | More ergonomic for complex objects but heavier for simple string + file path exchange. `EM_ASYNC_JS` is simpler. |

## Architecture Patterns

### Recommended File Locations

```
vcl/
  unx/generic/fontmanager/
    fontsubst.cxx              # MODIFY: Add #ifdef EMSCRIPTEN block in FindFontSubstitute()
  source/font/
    PhysicalFontCollection.cxx # NO CHANGES needed
solenv/gbuild/platform/
  EMSCRIPTEN_INTEL_GCC.mk     # MODIFY: Add "FS" to EXPORTED_RUNTIME_METHODS (non-Qt path)
```

Note: No new files are needed for the MVP. The `EM_ASYNC_JS` function and resolver logic can live directly in `fontsubst.cxx` behind `#ifdef EMSCRIPTEN`. A separate `WasmFontResolver` class can be factored out later if the logic grows.

### Pattern 1: EM_ASYNC_JS with VFS File Path Return

**What:** The `EM_ASYNC_JS` function takes a font family name (C string), calls a user-provided JS callback, JS writes font data to VFS via `FS.writeFile()`, and the function returns a C string pointer to the VFS path (or 0 on failure).

**When to use:** Inside `FindFontSubstitute()` when fontconfig returns no useful substitute and the font has not been previously attempted.

**Critical detail:** `EM_ASYNC_JS` with `-sJSPI` does NOT require manual `JSPI_EXPORTS` entries. Emscripten handles this automatically for `EM_ASYNC_JS`, `ccall`, and Embind async support (confirmed in Emscripten docs: "JSPI_IMPORTS and JSPI_EXPORTS aren't needed when using various helpers mentioned above such as: EM_ASYNC_JS").

**Example:**
```cpp
// Source: Emscripten docs (asyncify.html) + LibreOffice codebase patterns
#if defined(EMSCRIPTEN) && HAVE_EMSCRIPTEN_JSPI
#include <emscripten.h>

// Returns: pointer to VFS path string (caller must free), or 0 on failure
EM_ASYNC_JS(char*, em_resolveFontFromHost, (const char* pFamilyName), {
    var familyName = UTF8ToString(pFamilyName);

    // Call host-provided resolver (must return Promise<ArrayBuffer|null>)
    if (!Module.resolveSystemFont) return 0;
    var fontData = await Module.resolveSystemFont(familyName);
    if (!fontData) return 0;

    // Write to VFS
    var safeName = familyName.replace(/[^a-zA-Z0-9_-]/g, '_');
    var path = '/tmp/fonts/' + safeName + '.ttf';
    try {
        FS.mkdirTree('/tmp/fonts');
    } catch(e) {} // ignore if exists
    FS.writeFile(path, new Uint8Array(fontData));

    // Return path as C string (caller must free)
    return stringToNewUTF8(path);
});
#endif
```

### Pattern 2: Intercept-Register-Retry in FindFontSubstitute

**What:** After fontconfig returns no match (or a useless match), check a "tried" cache, call the `EM_ASYNC_JS` function, register the returned font via `AddTempDevFont()`, and retry the fontconfig query.

**When to use:** The only interception point. Added as an `#ifdef EMSCRIPTEN` block inside the existing `FcPreMatchSubstitution::FindFontSubstitute()`.

**Critical details:**
1. The `FindFontSubstitute()` method is `const`. Font resolution state (tried-set, caching) must use `mutable` members or a static/singleton.
2. After `AddTempDevFont()` successfully registers the font, fontconfig will find it on retry via `GetFcSubstitute()`.
3. Access to `PhysicalFontCollection*` is needed for `AddTempDevFont()`. This is available because `RegisterFontSubstitutors()` (line 53 of `fontsubst.cxx`) receives the collection pointer. Store it in a static or pass through the hook.

**Example flow:**
```cpp
// Inside FcPreMatchSubstitution::FindFontSubstitute(), after line 135:
// const bool bHaveSubstitute = !uselessmatch( rFontSelData, aOut );
#if defined(EMSCRIPTEN) && HAVE_EMSCRIPTEN_JSPI
    if (!bHaveSubstitute)
    {
        // Check tried-set to avoid infinite loops
        static std::set<OUString> sTriedFonts;
        if (sTriedFonts.find(rFontSelData.maTargetName) == sTriedFonts.end())
        {
            sTriedFonts.insert(rFontSelData.maTargetName);

            OString aUtf8 = OUStringToOString(rFontSelData.maTargetName,
                                               RTL_TEXTENCODING_UTF8);
            char* pPath = em_resolveFontFromHost(aUtf8.getStr());
            if (pPath)
            {
                OUString aPath = OUString::fromUtf8(pPath);
                free(pPath);

                // Convert to file:// URL and register
                OUString aFileURL = "file://" + aPath;
                // ... call AddTempDevFont with pFontCollection ...

                // Retry fontconfig query
                OUString aDummy2;
                const auto aRetry = GetFcSubstitute(rFontSelData, aDummy2);
                if (!aRetry.maSearchName.isEmpty()
                    && !uselessmatch(rFontSelData, aRetry))
                {
                    rCachedFontMap.push_front(value_type(rFontSelData, aRetry));
                    if (rCachedFontMap.size() > 256)
                        rCachedFontMap.pop_back();
                    rFontSelData = aRetry;
                    return true;
                }
            }
        }
    }
#endif
```

### Pattern 3: PhysicalFontCollection Access for AddTempDevFont

**What:** The `FcPreMatchSubstitution` hook does not have a direct `PhysicalFontCollection*`. We need it for `AddTempDevFont()`. The cleanest approach: store the pointer during `RegisterFontSubstitutors()`.

**Options evaluated:**

| Approach | Pro | Con | Recommendation |
|----------|-----|-----|----------------|
| Store `PhysicalFontCollection*` in static during `RegisterFontSubstitutors()` | Simple, collection is passed directly | Pointer could become stale if collection is rebuilt | Use this. Collection lifetime matches application lifetime. |
| Call `PrintFontManager::addFontFile()` + `FreetypeManager::AddFontFile()` + `AnnounceFonts()` directly using singletons | No stored pointer needed | Duplicates the logic of `AddTempDevFont()` | Avoid -- fragile duplication |
| Add `PhysicalFontCollection*` member to `FcPreMatchSubstitution` | Clean OOP | Requires modifying the class definition | Alternative if static feels wrong |

**Recommended:** Store collection pointer in a `static PhysicalFontCollection*` set during `RegisterFontSubstitutors()`.

### Anti-Patterns to Avoid

- **Do NOT modify `PhysicalFontCollection::FindFontFamily()`**: This is platform-generic code with a 300+ line multi-stage fallback. WASM-specific code belongs in the platform-specific `fontsubst.cxx`.

- **Do NOT use Asyncify**: Not used anywhere in LibreOffice. 20-50% binary overhead. `-sJSPI` is already in the build system.

- **Do NOT skip fontconfig registration**: `AddTempDevFont()` calls `addFontconfigFile()` which calls `FcConfigAppFontAddFile()`. This is required for the retry via `GetFcSubstitute()` to find the font. Registering only with FreetypeManager/PrintFontManager is insufficient.

- **Do NOT pass font data as heap pointers**: JS-to-WASM heap pointer passing is fragile (memory growth detaches `HEAPU8`). Use `FS.writeFile()` from JS and return only the path string.

- **Do NOT use `EM_ASM` for this**: Cannot return values cleanly, no type safety, cannot be async.

- **Do NOT call `ImplClearAllFontData()`/`ImplRefreshAllFontData()` from inside `FindFontSubstitute()`**: These invalidate the font collection that is currently being iterated. `AddTempDevFont()` + `AnnounceFonts()` incrementally adds to the collection without full rebuild.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Font registration | Custom fontconfig/FreeType/PrintFontManager calls | `FreeTypeTextRenderImpl::AddTempDevFont()` | Already orchestrates the 3-step registration (fontconfig + PrintFontManager + FreetypeManager). Handles TTC, font variations, quality scoring. |
| Font file URL conversion | Manual path-to-URL string manipulation | `INetURLObject` (as `addFontFile()` does) | Handles encoding, special characters, platform differences correctly. |
| Fontconfig substitution queries | Direct `FcFontMatch()` calls | `GetFcSubstitute()` -> `PrintFontManager::Substitute()` | Already wraps fontconfig with proper config, charset handling. |
| Font name normalization | Custom lowercasing/stripping | `GetEnglishSearchFontName()` | LibreOffice's canonical font name normalization, used throughout the codebase. |
| File path sanitization | Manual regex on font family names | `rtl::Uri::encode()` (as `EmbeddedFontsManager::getFileUrlForTemporaryFont()` does) | Handles unicode, special characters, path traversal prevention. |

**Key insight:** The font registration pipeline is already complete and battle-tested. The only new code needed is the bridge to JavaScript and the intercept logic in `FindFontSubstitute()`. Everything else is glue to existing APIs.

## Common Pitfalls

### Pitfall 1: EM_JS vs EM_ASYNC_JS Confusion

**What goes wrong:** Using `EM_JS` (synchronous) with `await` inside the JS body. The `await` is silently ignored -- the function returns `undefined` (cast to 0/null) before the Promise resolves.
**Why it happens:** `EM_JS` generates a plain synchronous C function. Only `EM_ASYNC_JS` generates an async-capable function that works with JSPI.
**How to avoid:** Use `EM_ASYNC_JS` for the font resolution callback. Guard with `#if defined(EMSCRIPTEN) && HAVE_EMSCRIPTEN_JSPI`.
**Warning signs:** Font resolution always returns null despite JS callback executing. No errors in console.

### Pitfall 2: JSPI_EXPORTS -- NOT Required for EM_ASYNC_JS (Prior Research Correction)

**What was feared:** That `EM_ASYNC_JS` functions need to be manually added to `JSPI_EXPORTS` in the build system.
**Reality (verified):** Emscripten docs explicitly state: "JSPI_IMPORTS and JSPI_EXPORTS aren't needed when using various helpers mentioned above such as: EM_ASYNC_JS, Embind's Async support, ccall, etc." This means our `EM_ASYNC_JS` function does NOT need an entry in `JSPI_EXPORTS` in `EMSCRIPTEN_INTEL_GCC.mk`.
**Impact:** Simplifies the build system change significantly. No need to track mangled symbol names.

### Pitfall 3: Main Thread Deadlock with JSPI

**What goes wrong:** JSPI suspends the WASM stack and returns a Promise to the JS event loop. If the caller is on the main browser thread, the event loop freezes, and the Promise cannot resolve.
**Why it happens:** LibreOffice headless WASM runs the event loop via `emscripten_set_main_loop` on the main thread. Font resolution is called during layout, which happens on this thread.
**How to avoid:** Two strategies:
1. **(Preferred for Electron):** Make the JS `Module.resolveSystemFont` callback itself synchronous using `ipcRenderer.sendSync()` or `fs.readFileSync()`. Then `EM_ASYNC_JS` resolves immediately without needing to yield to the event loop. JSPI still wraps the call but there is no actual suspension.
2. **(For browser):** Use `-sPROXY_TO_PTHREAD` to move C++ execution to a worker thread. The main thread event loop remains free to resolve Promises.
**Warning signs:** Browser tab freezes (not crashes) when opening a document with missing fonts.

### Pitfall 4: FindFontSubstitute() Is const -- Side Effects Need Care

**What goes wrong:** `FindFontSubstitute()` is declared `const`. Calling `AddTempDevFont()` (which modifies PrintFontManager, FreetypeManager, and PhysicalFontCollection) from a `const` method requires `mutable` state or const_cast.
**How to avoid:** The existing `maCachedFontMap` is already `mutable`. Add the tried-set as `mutable` too. The font registration via singletons (`PrintFontManager::get()`, `FreetypeManager::get()`) works from `const` context because they are separate objects. The `PhysicalFontCollection*` addition via `AddTempDevFont` -> `AnnounceFonts` is additive (no invalidation of iterators).
**Warning signs:** Compiler errors about modifying state in const method.

### Pitfall 5: Infinite Recursion in Font Substitution

**What goes wrong:** Register font -> retry -> fontconfig still returns "useless match" (name table mismatch) -> try again -> register again -> infinite loop.
**How to avoid:** Maintain a `static std::set<OUString> sTriedFonts` of font family names already attempted via JS. Check BEFORE calling `EM_ASYNC_JS`. Never attempt the same name twice per session. This also serves as the negative cache (REQ-F03).
**Warning signs:** Tab becomes unresponsive, SAL_INFO shows same font repeatedly, WASM stack overflow.

### Pitfall 6: FcConfigAppFontAddFile Requires Native Path, Not URL

**What goes wrong:** `FcConfigAppFontAddFile()` at `fontconfig.cxx` line 788 expects a native filesystem path (e.g., `/tmp/fonts/Calibri.ttf`), not a `file://` URL. Passing a URL causes silent failure.
**How to avoid:** The `addFontFile()` method handles URL-to-path conversion via `INetURLObject`. Pass a proper `file:///tmp/fonts/...` URL to `addFontFile()` and let it handle conversion. Verify with SAL_LOG `vcl.fonts` that `FcConfigAppFontAddFile` returns true.
**Warning signs:** `SAL_INFO("vcl.fonts", "FcConfigAppFontAddFile(...) => false")` in logs.

### Pitfall 7: HAVE_EMSCRIPTEN_JSPI May Not Be Set for Headless Builds

**What goes wrong:** `HAVE_EMSCRIPTEN_JSPI` is set by `--enable-emscripten-jspi` in configure. The existing JSPI usage in the codebase is gated on `ENABLE_QT6 && HAVE_EMSCRIPTEN_JSPI && !HAVE_EMSCRIPTEN_PROXY_TO_PTHREAD`. Our headless (non-Qt) build may not pass `--enable-emscripten-jspi`.
**How to avoid:** Verify the target build configuration includes `--enable-emscripten-jspi`. Our hook guards on `#if defined(EMSCRIPTEN) && HAVE_EMSCRIPTEN_JSPI` (independent of Qt6). If the build does NOT have JSPI, provide a synchronous-only fallback using plain `EM_JS` where the JS callback must be synchronous.
**Warning signs:** `HAVE_EMSCRIPTEN_JSPI` is 0 in `config_host/config_emscripten.h` (the `.in` template defaults to 0).

### Pitfall 8: "FS" Not in EXPORTED_RUNTIME_METHODS for Non-Qt Builds

**What goes wrong:** The `FS.writeFile()` call inside `EM_ASYNC_JS` requires `"FS"` in Emscripten's `EXPORTED_RUNTIME_METHODS`. Currently, `"FS"` is only conditionally included when `ENABLE_QT6` is set (line 30 of `EMSCRIPTEN_INTEL_GCC.mk`: `$(if $(ENABLE_QT6),$(COMMA)"FS"...)`).
**How to avoid:** Add `"FS"` unconditionally to `EXPORTED_RUNTIME_METHODS` in `EMSCRIPTEN_INTEL_GCC.mk`, or add it conditionally for `ENABLE_EMSCRIPTEN_JSPI` as well. Without this, the JS code inside `EM_ASYNC_JS` cannot call `FS.writeFile()` or `FS.mkdirTree()`.
**Warning signs:** `ReferenceError: FS is not defined` in browser console when font resolution fires.

## Code Examples

### Example 1: Complete EM_ASYNC_JS Font Resolution Function

```cpp
// Source: Emscripten asyncify docs + LibreOffice EM_JS patterns (PrimaryBindings.cxx)
// Location: vcl/unx/generic/fontmanager/fontsubst.cxx

#if defined(EMSCRIPTEN) && HAVE_EMSCRIPTEN_JSPI
#include <emscripten.h>

// Resolves a font family name via JavaScript host callback.
// Returns: VFS path to written font file (caller must free()), or 0 if unavailable.
// JS side must implement Module.resolveSystemFont(familyName) -> Promise<ArrayBuffer|null>
EM_ASYNC_JS(char*, em_resolveFontFromHost, (const char* pFamilyName), {
    var familyName = UTF8ToString(pFamilyName);
    if (!Module.resolveSystemFont) {
        return 0;
    }
    try {
        var fontData = await Module.resolveSystemFont(familyName);
        if (!fontData || fontData.byteLength === 0) {
            return 0;
        }
        // Sanitize filename: replace non-alphanumeric chars with underscore
        var safeName = familyName.replace(/[^a-zA-Z0-9_-]/g, '_');
        var path = '/tmp/fonts/' + safeName + '.ttf';
        try { FS.mkdirTree('/tmp/fonts'); } catch(e) {}
        FS.writeFile(path, new Uint8Array(fontData));
        return stringToNewUTF8(path);
    } catch(e) {
        console.warn('Font resolution failed for: ' + familyName, e);
        return 0;
    }
});
#endif
```

### Example 2: Synchronous Fallback (Non-JSPI Build)

```cpp
// Source: LibreOffice EM_JS patterns (cpp2uno.cxx, shellexec_em.cxx)
// Location: vcl/unx/generic/fontmanager/fontsubst.cxx

#if defined(EMSCRIPTEN) && !HAVE_EMSCRIPTEN_JSPI
#include <emscripten.h>

// Synchronous font resolution -- JS callback MUST be synchronous
// (e.g., ipcRenderer.sendSync in Electron, or pre-loaded cache lookup)
EM_JS(char*, em_resolveFontFromHostSync, (const char* pFamilyName), {
    var familyName = UTF8ToString(pFamilyName);
    if (!Module.resolveSystemFontSync) {
        return 0;
    }
    var fontData = Module.resolveSystemFontSync(familyName);
    if (!fontData || fontData.byteLength === 0) {
        return 0;
    }
    var safeName = familyName.replace(/[^a-zA-Z0-9_-]/g, '_');
    var path = '/tmp/fonts/' + safeName + '.ttf';
    try { FS.mkdirTree('/tmp/fonts'); } catch(e) {}
    FS.writeFile(path, new Uint8Array(fontData));
    return stringToNewUTF8(path);
});
#endif
```

### Example 3: JS Host Contract (What the Electron Developer Implements)

```javascript
// Source: Project FEATURES.md JS API Contract
// Electron preload.js or Module setup

// Async version (for JSPI-enabled builds):
Module.resolveSystemFont = async function(familyName) {
    // Option A: Electron IPC (async)
    const fontPath = await ipcRenderer.invoke('resolve-font', familyName);
    if (!fontPath) return null;
    const fs = require('fs');
    return fs.readFileSync(fontPath); // Returns Buffer (ArrayBuffer-like)

    // Option B: Pre-loaded cache
    // return fontCache.get(familyName) || null;
};

// Sync version (for non-JSPI builds):
Module.resolveSystemFontSync = function(familyName) {
    const fontPath = ipcRenderer.sendSync('resolve-font', familyName);
    if (!fontPath) return null;
    const fs = require('fs');
    return fs.readFileSync(fontPath);
};
```

### Example 4: AddTempDevFont Registration (Existing API Usage)

```cpp
// Source: vcl/unx/generic/gdi/freetypetextrender.cxx line 96-129
// This is the EXISTING code -- do not rewrite, just call it.

// pFontCollection: stored during RegisterFontSubstitutors()
// aFileURL: "file:///tmp/fonts/Calibri.ttf"
// rFontName: "Calibri" (from document request)
bool bRegistered = AddTempDevFont(pFontCollection, aFileURL, rFontName);
// bRegistered == true means font is now in fontconfig + PrintFontManager + FreetypeManager
// The retry via GetFcSubstitute() will now find it.
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Asyncify (`-sASYNCIFY`) for async C++/JS | JSPI (`-sJSPI`) | Emscripten 3.1.46+ / Chrome 123 (2024) | Zero binary overhead, no function whitelisting. `-sASYNCIFY=2` is deprecated in favor of `-sJSPI`. |
| Manual `JSPI_EXPORTS` for `EM_ASYNC_JS` | Automatic (not needed) | Emscripten docs (current) | `EM_ASYNC_JS` handles JSPI export registration automatically. |
| Pointer-based data transfer (heap copy) | VFS file path return pattern | Best practice | Avoids WASM memory growth heap corruption issues. |

**Deprecated/outdated:**
- `-sASYNCIFY=2`: Deprecated. Use `-sJSPI` instead (per Emscripten settings reference).
- Manual `JSPI_EXPORTS` for `EM_ASYNC_JS` functions: Not needed (per Emscripten docs).

## Key Design Decisions for Planner

### Decision 1: Where to Hook

**Choice:** `FcPreMatchSubstitution::FindFontSubstitute()` in `fontsubst.cxx` line 100-172.

**Rationale:** This is the platform-specific (Unix/fontconfig) substitution hook. It is called at line 1076 and 1109 of `PhysicalFontCollection.cxx` during `FindFontFamily()`. After the hook returns true with an updated `rFontSelData`, the caller looks up the font by the new `maSearchName` at line 1085 or 1114 and finds it (because `AnnounceFonts` added it to the collection).

**Key flow:**
1. `FindFontFamily()` (line 1076) calls `mpPreMatchHook->FindFontSubstitute(rFSD)`.
2. Our modified `FindFontSubstitute()` detects no substitute, calls JS, registers font.
3. Returns `true` with `rFSD` updated to the new font's search name.
4. `FindFontFamily()` (line 1085) calls `ImplFindFontFamilyBySearchName()` and finds it.

### Decision 2: PhysicalFontCollection Pointer Access

**Choice:** Store `static PhysicalFontCollection* s_pFontCollection` in `fontsubst.cxx`, set during `RegisterFontSubstitutors()` (line 53).

**Rationale:** `AddTempDevFont()` requires a `PhysicalFontCollection*`. The collection is passed to `RegisterFontSubstitutors()` already. Storing it statically is safe because the collection's lifetime matches the application. The existing hook objects (`aSubstPreMatch`, `aSubstFallback`) are already static.

### Decision 3: Desktop Build Isolation

**Choice:** All changes behind `#ifdef EMSCRIPTEN` (and `HAVE_EMSCRIPTEN_JSPI` for async path).

**Impact:** Zero behavioral changes on desktop builds. No new warnings, no binary size increase, no font resolution differences.

### Decision 4: EXPORTED_RUNTIME_METHODS Update

**Choice:** Add `"FS"` to `EXPORTED_RUNTIME_METHODS` unconditionally (or gated on `ENABLE_EMSCRIPTEN_JSPI`).

**Rationale:** Currently `"FS"` is only exported for Qt6 builds. Our `EM_ASYNC_JS` / `EM_JS` code needs `FS.writeFile()` and `FS.mkdirTree()`. Without this, the JS code gets `ReferenceError: FS is not defined`.

## Open Questions

1. **HAVE_EMSCRIPTEN_JSPI for headless builds**
   - What we know: `HAVE_EMSCRIPTEN_JSPI` requires `--enable-emscripten-jspi` in configure. It is not Qt-dependent at the C++ level. The config_emscripten.h.in template defaults to 0.
   - What's unclear: Whether the downstream build system passes `--enable-emscripten-jspi` for headless (non-Qt) WASM builds.
   - Recommendation: Design for both paths (`HAVE_EMSCRIPTEN_JSPI` = 0 and 1). Provide synchronous `EM_JS` fallback. Verify downstream build config early in implementation.

2. **Electron version (JSPI browser support)**
   - What we know: JSPI is enabled by default in Chrome >= 123, Electron >= 30 (Chromium 124). JSPI is now phase 4 in W3C (effectively standardized) and available in Chrome 137 and Firefox 139.
   - What's unclear: Exact Electron version used by the downstream Diffchecker app.
   - Recommendation: Not a blocker for implementation. JSPI is widely available in 2026. The synchronous fallback handles older versions.

3. **Font name matching after registration**
   - What we know: `AddTempDevFont` optionally overrides the font family name (line 116-117 of `freetypetextrender.cxx`). Fontconfig queries use the name from the font's name table by default.
   - What's unclear: If the font's internal name (e.g., "Calibri" in the name table) differs from what fontconfig normalizes to.
   - Recommendation: Pass the requested family name as `rFontName` to `AddTempDevFont()` so it overrides the internal name. This ensures the retry matches.

4. **"FS" in EXPORTED_RUNTIME_METHODS when EM_ASYNC_JS is used**
   - What we know: Inside `EM_ASYNC_JS` / `EM_JS` blocks, the code runs in the Module scope, which may already have access to the internal `FS` object without it being in `EXPORTED_RUNTIME_METHODS`.
   - What's unclear: Whether `FS` is accessible inside `EM_ASYNC_JS` blocks regardless of `EXPORTED_RUNTIME_METHODS`.
   - Recommendation: Test empirically. If `FS` is not accessible, add to `EXPORTED_RUNTIME_METHODS`. LOW risk -- easy fix.

## Sources

### Primary (HIGH confidence)
- `vcl/unx/generic/fontmanager/fontsubst.cxx` -- Full source read. Hook point verified: `FcPreMatchSubstitution::FindFontSubstitute()` line 100-172. MRU cache mechanism verified.
- `vcl/unx/generic/gdi/freetypetextrender.cxx` -- Full source read. `AddTempDevFont()` pipeline verified: line 96-129. Quality boost of 5800 confirmed. `AnnounceFonts()` call confirmed.
- `vcl/unx/generic/fontmanager/fontmanager.cxx` -- `addFontFile()` verified: line 141-165. URL-to-path conversion, `addFontconfigFile()` call, `analyzeFontFile()` chain confirmed.
- `vcl/unx/generic/fontmanager/fontconfig.cxx` -- `addFontconfigFile()` verified: line 785-799. `FcConfigAppFontAddFile()` call with native path (not URL) confirmed.
- `vcl/source/font/PhysicalFontCollection.cxx` -- `FindFontFamily()` verified: line 966-1295. `mpPreMatchHook` call sites at line 1076 and 1109 confirmed. Post-hook lookup at line 1085 confirmed.
- `vcl/inc/font/fontsubstitution.hxx` -- Interface definition verified. `FindFontSubstitute()` is `const` (line 64).
- `solenv/gbuild/platform/EMSCRIPTEN_INTEL_GCC.mk` -- Full source read. JSPI flags (line 33-37), EXPORTED_RUNTIME_METHODS (line 30), FORCE_FILESYSTEM (line 30) confirmed. "FS" only in Qt6 path.
- `vcl/Library_vcl.mk` -- `vcl_headless_freetype_code` includes `fontsubst` (line 660) confirmed compiled for WASM headless.
- `config_host/config_emscripten.h.in` -- `HAVE_EMSCRIPTEN_JSPI` defaults to 0 (line 12).
- `configure.ac` -- `--enable-emscripten-jspi` sets `HAVE_EMSCRIPTEN_JSPI` (line 4378-4384). Independent of Qt.
- `static/source/unoembindhelpers/PrimaryBindings.cxx` -- `EM_JS` patterns: `Module._malloc`, `HEAPU16`, `HEAPU32` (line 63-104).
- `vcl/source/app/scheduler.cxx` -- JSPI proxy pattern reference (line 596-607).

### Secondary (MEDIUM confidence)
- [Emscripten Asynchronous Code docs](https://emscripten.org/docs/porting/asyncify.html) -- Confirmed `EM_ASYNC_JS` works with `-sJSPI`. Confirmed `JSPI_EXPORTS` not needed for `EM_ASYNC_JS`. Quote: "JSPI_IMPORTS and JSPI_EXPORTS aren't needed when using various helpers mentioned above such as: EM_ASYNC_JS".
- [Emscripten Settings Reference](https://emscripten.org/docs/tools_reference/settings_reference.html) -- `-sASYNCIFY=2` is deprecated, use `-sJSPI` instead. `JSPI_EXPORTS` default is empty array.
- [V8 JSPI blog](https://v8.dev/blog/jspi) -- JSPI browser support timeline and API design.

### Tertiary (LOW confidence)
- WebSearch results for JSPI browser support (Chrome 137, Firefox 139) -- Could not verify exact version numbers against official release notes.
- Electron version-to-Chromium mapping (Electron 30 = Chromium 124) -- Based on training data, not verified against official Electron releases for 2026.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- All components verified from source code. EM_ASYNC_JS + JSPI confirmed from Emscripten docs.
- Architecture: HIGH -- Hook point, data flow, and registration pipeline all verified from source. No assumptions needed.
- Pitfalls: HIGH -- Critical pitfalls verified from codebase (const method, fontconfig path requirements) and Emscripten docs (EM_ASYNC_JS/JSPI relationship). Prior research PITFALL-2 (JSPI_EXPORTS) corrected based on official docs.
- Desktop isolation: HIGH -- `#ifdef EMSCRIPTEN` pattern confirmed working in existing codebase.

**Research date:** 2026-02-09
**Valid until:** 2026-03-09 (30 days -- stable domain, no fast-moving dependencies)
