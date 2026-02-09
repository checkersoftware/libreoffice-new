# Stack Research: Emscripten Interop for WASM System Font Resolution

**Domain:** Emscripten C++/JS interop for runtime font resolution in LibreOffice WASM
**Researched:** 2026-02-09
**Confidence:** MEDIUM (web tools unavailable; findings based on codebase evidence + training data)

## Executive Summary

LibreOffice's WASM build already has mature JSPI infrastructure. The codebase uses `-sJSPI` with `emscripten_proxy_promise` / `emscripten_promise_await` for async bridging in the Qt6 VCL plugin. Font resolution should use the same JSPI mechanism rather than introducing Asyncify. For data transfer, `EM_JS` with WASM heap manipulation (already proven in `PrimaryBindings.cxx`) is the right pattern. Files can be written to the Emscripten MEMFS virtual filesystem using standard POSIX `fopen`/`fwrite` from C++ -- no special API needed since `FORCE_FILESYSTEM=1` is already set.

---

## Recommended Stack

### Core Technologies

| Technology | Version | Purpose | Why Recommended | Confidence |
|------------|---------|---------|-----------------|------------|
| JSPI (`-sJSPI`) | Emscripten >= 3.1.46 | Sync/async bridge for C++ calling async JS | Already used in LibreOffice WASM (Qt6 mode). Zero binary size overhead vs Asyncify. Codebase has working patterns. | HIGH |
| `EM_JS` macro | Emscripten >= 3.1.46 | C++ to JS function interop | Standard mechanism, 6 existing usages in codebase (`PrimaryBindings.cxx`, `cpp2uno.cxx`, `initjsunoscripting.cxx`, `shellexec_em.cxx`). Well-understood patterns for binary data. | HIGH |
| MEMFS (POSIX writes) | Emscripten >= 3.1.46 | Writing font files to virtual FS | `FORCE_FILESYSTEM=1` already set in build. Standard `fopen`/`fwrite`/`fclose` works. No special Emscripten FS API calls needed from C++. | HIGH |
| `AddTempDevFont()` | LibreOffice VCL | Runtime font registration | Existing API, already implemented for headless/FreeType backend. Handles fontconfig registration + FreeType cache update + font collection announcement. | HIGH |

### Supporting Libraries

| Library | Purpose | When to Use | Confidence |
|---------|---------|-------------|------------|
| `emscripten/promise.h` | `emscripten_promise_await()` to block C++ on a JS Promise | When C++ needs to synchronously wait for async JS font resolution result | HIGH |
| `emscripten/proxying.h` | `emscripten_proxy_promise()` to proxy work to another thread | If font resolution runs on a non-main thread and needs to call JS on the main thread | MEDIUM |
| `emscripten/val.h` | `emscripten::val` for rich JS object interop | Alternative to `EM_JS` for complex data marshalling, but `EM_JS` is simpler for our use case | HIGH |
| fontconfig (`FcConfigAppFontAddFile`) | Register font file with fontconfig | Called internally by `PrintFontManager::addFontconfigFile()` -- not called directly | HIGH |

---

## JSPI vs ASYNCIFY: Decision

**Use JSPI. Do not use Asyncify.**

### Why JSPI

1. **Already in use.** LibreOffice's Emscripten build has conditional JSPI support (`ENABLE_EMSCRIPTEN_JSPI=TRUE`, `--enable-emscripten-jspi`). The Qt6 VCL plugin uses `emscripten_proxy_promise` + `emscripten_promise_await` extensively (see `vcl/qt5/QtInstance.cxx`, `vcl/source/app/scheduler.cxx`).

2. **Zero binary size overhead.** JSPI is a WebAssembly engine feature, not a code transform. Asyncify adds ~20-50% to WASM binary size by instrumenting every function that might yield with stack save/restore code. For LibreOffice (already a massive binary), this is prohibitive.

3. **No function whitelist maintenance.** Asyncify requires `-sASYNCIFY_IMPORTS` to list which imports can suspend, and `-sASYNCIFY_WHITELIST` or similar to control which functions get instrumented. In a codebase as large as LibreOffice, maintaining this whitelist is error-prone.

4. **Better performance.** JSPI suspends at the engine level (V8/SpiderMonkey) without stack copying. Asyncify must save and restore the entire call stack on each yield.

### JSPI Requirements

| Requirement | Version | Notes | Confidence |
|-------------|---------|-------|------------|
| Emscripten | >= 3.1.46 (already minimum) | LibreOffice `configure.ac` sets `EMSCRIPTEN_MIN_MAJOR=3`, `EMSCRIPTEN_MIN_MINOR=1`, `EMSCRIPTEN_MIN_TINY=46` | HIGH |
| Chrome/Chromium | >= 123 | JSPI shipped enabled by default in Chrome 123 (March 2024). Origin trial started Chrome 110. | MEDIUM (from training data) |
| Electron | >= 30 (Chrome 124) | Electron 30 uses Chromium 124. Electron 29 uses Chromium 122. **Electron >= 30 required.** | MEDIUM (from training data) |
| Firefox | Not yet stable | JSPI was behind a flag in Firefox. Status as of 2026 unclear. | LOW |
| Safari | No support known | WebKit has no known JSPI implementation as of training data cutoff. | LOW |
| Node.js | >= 22 | V8-based, should inherit Chrome support. `--experimental-wasm-stack-switching` flag may be needed in older versions. | MEDIUM |

### JSPI Build Configuration (already in codebase)

From `solenv/gbuild/platform/EMSCRIPTEN_INTEL_GCC.mk` lines 33-37:
```makefile
ifeq ($(ENABLE_EMSCRIPTEN_JSPI),TRUE)
gb_EMSCRIPTEN_LDFLAGS += \
    -sJSPI \
    -sJSPI_EXPORTS=_emscripten_check_mailbox,...
endif
```

**Key detail:** `-sJSPI_EXPORTS` must list any exported C++ functions that may call into async JS. For our font resolution hook, if the function is called from the main event loop (through fontconfig substitution), the JSPI suspension propagates up through the already-registered exports. If our new EM_JS function is called from a code path not covered by existing JSPI_EXPORTS, the export list may need extending -- but the existing `_emscripten_check_mailbox` export already covers the main thread event processing path.

### What NOT to use: Asyncify

| Avoid | Why | Use Instead |
|-------|-----|-------------|
| `-sASYNCIFY` | 20-50% binary size overhead, complex whitelist management, slower suspend/resume. LibreOffice doesn't use it anywhere. | JSPI (`-sJSPI`) |
| `EM_ASYNC_JS` | Requires Asyncify (it's syntactic sugar for `EM_JS` + `Asyncify.handleAsync()`). Not used anywhere in LibreOffice codebase. | `EM_JS` + JSPI promise pattern |
| `emscripten_sleep()` | Requires Asyncify. Used for simple delays; not appropriate for structured async operations. | `emscripten_promise_await()` for JSPI |

---

## Binary Data Transfer Pattern: JS to C++

### Recommended Pattern: EM_JS with WASM Heap Write

The C++ side allocates memory, passes the pointer to JS, and JS fills it. This is the proven pattern from `PrimaryBindings.cxx` (lines 63-93).

**Pattern for font data:**

```cpp
#ifdef EMSCRIPTEN
#include <emscripten.h>
#include <emscripten/promise.h>

// EM_JS function that takes a font family name (C string)
// and returns a pointer to {size, data} struct, or 0 on failure.
//
// The JS side:
// 1. Calls a user-provided callback (e.g., Module.resolveFont)
// 2. Gets back an ArrayBuffer/Uint8Array with font data
// 3. Allocates WASM memory with Module._malloc
// 4. Copies the font data into WASM heap
// 5. Returns the pointer and size
EM_JS(int, js_resolve_font, (const char* familyName, int* outSize), {
    // Read the C string from WASM memory
    var name = UTF8ToString(familyName);

    // Call the host-provided resolver
    // This MUST be synchronous or the function must be called through JSPI
    var fontData = Module.resolveSystemFont(name);
    if (!fontData) {
        return 0; // No font found
    }

    // fontData is a Uint8Array or ArrayBuffer
    var bytes = new Uint8Array(fontData);
    var size = bytes.length;

    // Allocate WASM memory and copy font data
    var ptr = Module._malloc(size);
    Module.HEAPU8.set(bytes, ptr);

    // Write size to output parameter
    Module.HEAP32[outSize >> 2] = size;

    return ptr;
});
#endif
```

### Why This Pattern

1. **`UTF8ToString`** is already in `EXPORTED_RUNTIME_METHODS` (see `EMSCRIPTEN_INTEL_GCC.mk` line 30).
2. **`Module._malloc`** / **`Module._free`** are always available in Emscripten builds.
3. **`Module.HEAPU8.set()`** is the canonical way to bulk-copy bytes into WASM memory.
4. **`Module.HEAP32`** for writing the size output parameter follows the same pattern as `PrimaryBindings.cxx` using `HEAPU16` and `HEAPU32`.

### JSPI Integration for Async Resolution

If the JS font resolver is asynchronous (Electron IPC), the EM_JS function must return a Promise, and the call site must go through JSPI. There are two approaches:

**Approach A: Synchronous JS wrapper (simpler, recommended for Electron)**

If running in Electron with Node.js integration, the font resolver can use synchronous IPC (`ipcRenderer.sendSync`) or `fs.readFileSync`, making the `EM_JS` function fully synchronous. No JSPI needed for this specific call.

```javascript
// In Electron preload or Module setup:
Module.resolveSystemFont = function(familyName) {
    // Synchronous IPC to main process
    return ipcRenderer.sendSync('resolve-font', familyName);
    // Returns Uint8Array or null
};
```

**Approach B: Async JS with JSPI suspension (if sync IPC unavailable)**

If the font resolution must be async, the approach is more complex. The EM_JS function returns a value, but the *calling C++ function* must be in a JSPI-exported path so the engine can suspend the WASM stack while waiting for the JS Promise.

This requires:
1. The call originates from a JSPI-enabled thread/export
2. The JS side returns a Promise that resolves with font data
3. Using `emscripten_promise_await()` in C++ to wait on the result

**Recommendation: Use Approach A (synchronous JS) for the initial implementation.** Electron's `ipcRenderer.sendSync` or direct `fs.readFileSync` avoids JSPI complexity for the font resolution call specifically. Only move to Approach B if synchronous resolution proves infeasible.

### Data Flow Summary

```
C++ (FindFontSubstitute)
  |
  v
EM_JS js_resolve_font(familyName, &outSize)
  |
  v
JS: Module.resolveSystemFont(name)    <-- User implements this
  |
  v
JS: _malloc(size), HEAPU8.set(data, ptr)
  |
  v
C++: receives ptr + size
  |
  v
C++: fwrite() to /tmp/fonts/<fontfile>.ttf (MEMFS)
  |
  v
C++: AddTempDevFont("file:///tmp/fonts/<fontfile>.ttf", familyName)
  |
  v
C++: free(ptr)   // Free the EM_JS-allocated buffer
```

---

## Emscripten Virtual FS: Writing Files from C++

### Use Standard POSIX I/O

The LibreOffice WASM build has `FORCE_FILESYSTEM=1` set (line 30 of `EMSCRIPTEN_INTEL_GCC.mk`). This means the full Emscripten filesystem is available, backed by MEMFS (in-memory filesystem) by default.

**Writing a font file from C++:**

```cpp
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>

bool writeFontToVFS(const char* fontData, int fontDataSize, const char* fileName) {
    // Ensure directory exists
    mkdir("/tmp/fonts", 0777);  // MEMFS ignores permissions but mkdir is needed

    // Construct path
    char path[256];
    snprintf(path, sizeof(path), "/tmp/fonts/%s", fileName);

    // Write using standard POSIX I/O
    FILE* f = fopen(path, "wb");
    if (!f) return false;

    size_t written = fwrite(fontData, 1, fontDataSize, f);
    fclose(f);

    return (written == static_cast<size_t>(fontDataSize));
}
```

### Why Standard POSIX and Not Emscripten FS API

| Approach | When to Use | Confidence |
|----------|-------------|------------|
| `fopen`/`fwrite`/`fclose` (POSIX) | **Use this.** Works from C++, no special includes. Fontconfig and FreeType read files via POSIX too. | HIGH |
| `FS.writeFile()` (JS API) | Only usable from JavaScript. Would require `EM_ASM` call from C++. Unnecessary complexity. | HIGH |
| `FS.createDataFile()` (JS API) | Creates files from JS. Same issue -- not callable from C++. | HIGH |

### Directory Structure

Use `/tmp/fonts/` as the staging directory:
- `/tmp` exists in MEMFS by default
- Standard location for temporary files
- No special directory creation beyond `mkdir("/tmp/fonts", 0777)`
- Files persist for the lifetime of the WASM instance (MEMFS is in-memory)

### NODEFS Consideration

NODEFS mounts a real host filesystem directory into the WASM virtual FS. For Electron + Node.js integration, this could theoretically allow direct font file access without copying. However:

| Approach | Pro | Con | Recommendation |
|----------|-----|-----|----------------|
| MEMFS + copy from JS | Simple, portable, no Node.js dependency | Font data copied into WASM memory | **Use this** |
| NODEFS mount | Direct file access, no copy | Requires Node.js, filesystem sync issues, WASM build must enable `NODEFS`, more complex error handling | Defer to future optimization |

---

## Font Registration: AddTempDevFont Pipeline

### How It Works (verified from codebase)

From `vcl/unx/generic/gdi/freetypetextrender.cxx` lines 96-129:

```
AddTempDevFont(fontCollection, fileURL, fontName)
  |
  +--> PrintFontManager::addFontFile(fileURL)
  |      |
  |      +--> addFontconfigFile(path)              // FcConfigAppFontAddFile()
  |      +--> analyzeFontFile(dirID, fileName)      // Parse font metrics
  |      +--> Returns vector<fontID>
  |
  +--> For each fontID:
  |      +--> FreetypeManager::AddFontFile(path, faceNum, variantNum, fontID, attributes)
  |      +--> aDFA.IncreaseQualityBy(5800)          // Boost priority
  |
  +--> FreetypeManager::AnnounceFonts(fontCollection)  // Update available font list
```

### Key Points

1. **File URL format**: `AddTempDevFont` expects a `file://` URL (e.g., `file:///tmp/fonts/arial.ttf`), not a POSIX path.

2. **Font name parameter**: The second parameter (`rFontName`) is optional -- if non-empty, it overrides the family name parsed from the font file. Pass the requested family name to ensure it matches what the document expects.

3. **Quality boost**: `IncreaseQualityBy(5800)` ensures runtime-registered fonts are preferred over fontconfig substitutes.

4. **fontconfig registration**: `addFontconfigFile()` calls `FcConfigAppFontAddFile()` which registers the font with fontconfig. This means subsequent fontconfig lookups (including from `FcPreMatchSubstitution`) will find the font.

5. **Thread safety**: `AddTempDevFont` must be called with the SolarMutex held (standard for all VCL operations). In the font substitution path (`FindFontSubstitute`), the SolarMutex is already held.

6. **Font re-lookup after registration**: After calling `AddTempDevFont` and the font is registered, the substitution function needs to retry the fontconfig lookup. The `FindFontSubstitute` function can return `false` (no substitution needed, retry with original name) or the caller needs to re-enter the font selection path.

---

## Alternatives Considered

| Recommended | Alternative | When to Use Alternative |
|-------------|-------------|-------------------------|
| JSPI | Asyncify | Only if targeting environments without JSPI support (Firefox, Safari). Would require maintaining function whitelist and accept 20-50% binary size increase. |
| `EM_JS` + heap write | `emscripten::val` | If complex JS object manipulation is needed. `val` is more ergonomic for objects but heavier for raw binary data. |
| POSIX `fwrite` to MEMFS | NODEFS mount | If font files are very large and memory is constrained. NODEFS avoids the copy but requires Node.js filesystem access. |
| Synchronous JS resolver | Async JS + JSPI suspension | If running in a pure browser context (no Electron sync IPC). Would need full JSPI promise chain. |

---

## What NOT to Use

| Avoid | Why | Use Instead |
|-------|-----|-------------|
| Asyncify (`-sASYNCIFY`) | Massive binary size overhead (20-50%), complex whitelist, slower. Not used anywhere in LibreOffice WASM. | JSPI (`-sJSPI`) |
| `EM_ASYNC_JS` macro | Requires Asyncify. Not used in LibreOffice codebase. | `EM_JS` with synchronous JS or JSPI |
| `emscripten_sleep()` | Requires Asyncify. A crude timer, not structured async. | `emscripten_promise_await()` if JSPI needed |
| `FS.writeFile()` from C++ via `EM_ASM` | Unnecessary indirection. POSIX I/O works fine from C++. | `fopen`/`fwrite`/`fclose` |
| `emscripten_wget` / Fetch API | For downloading remote resources. We have font data in-memory from JS, not from a URL. | Direct heap memory copy |
| Browser Local Font Access API | Requires user permission, limited browser support, not available in Node.js/Electron main process. | Electron IPC-based resolution |

---

## Stack Patterns by Variant

**If JSPI is enabled (recommended configuration):**
- Use `EM_JS` for synchronous C++ to JS calls
- If JS side must be async, the call must originate from a JSPI-exported function path
- Existing JSPI exports likely cover the VCL event processing path that reaches font substitution
- If not, add the font resolution call site to `-sJSPI_EXPORTS`

**If JSPI is NOT enabled (fallback):**
- Use synchronous `EM_JS` only (no async bridging possible)
- The JS font resolver MUST be synchronous (Electron `sendSync`, `readFileSync`)
- This is actually simpler but limits the JS implementation options
- No Asyncify -- the binary size cost is not acceptable for LibreOffice

**If running in pure browser (no Electron):**
- JSPI required for async font resolution
- Browser must support JSPI (Chrome >= 123)
- No synchronous filesystem access available
- Consider pre-loading known fonts at startup instead of on-demand resolution

---

## Version Compatibility

| Component | Compatible With | Notes |
|-----------|-----------------|-------|
| `-sJSPI` | Emscripten >= 3.1.46 | LibreOffice minimum version. JSPI linker flag available. |
| `-sJSPI` | Chrome/Chromium >= 123 | JSPI enabled by default since March 2024. |
| `-sJSPI` | Electron >= 30 | Chromium 124 base. Electron 29 (Chromium 122) may also work. |
| `FORCE_FILESYSTEM=1` | All Emscripten versions | Already set in LibreOffice build. Enables POSIX filesystem. |
| `AddTempDevFont()` | LibreOffice VCL (all versions) | Stable API, implemented across all backends. |
| `FcConfigAppFontAddFile()` | fontconfig >= 2.6 | Standard fontconfig API. Used by LibreOffice on all Unix-like platforms. |
| `UTF8ToString` | Emscripten >= 3.1.46 | Already in `EXPORTED_RUNTIME_METHODS`. |

---

## Open Questions

1. **JSPI export coverage**: Does the font substitution code path (called during document layout) run through an already JSPI-exported function? If font resolution happens during initial document loading on the main thread, it likely goes through the Qt event loop which IS JSPI-exported. But if it happens during a synchronous `Scheduler::CallbackTaskScheduling` invocation, the JSPI_EXPORTS list may need updating. **Needs runtime verification.**

2. **Non-Qt headless mode**: The existing JSPI infrastructure is gated on `ENABLE_QT6 && HAVE_EMSCRIPTEN_JSPI && !HAVE_EMSCRIPTEN_PROXY_TO_PTHREAD`. If the target Electron build uses a different VCL backend (pure headless SVP without Qt), the JSPI plumbing may not be active. In that case, synchronous JS is the only option, which is fine for Electron (use `sendSync`). **Needs build configuration clarification.**

3. **Electron version in use**: The downstream consumer (Diffchecker Electron app) Electron version determines JSPI support. Electron >= 30 is required for JSPI. If the app uses an older Electron, synchronous-only mode is required. **Needs confirmation from project context.**

4. **Memory pressure from font copying**: Fonts are copied from JS heap to WASM heap (via `_malloc`), then written to MEMFS (another copy). A typical font file is 200KB-2MB. For documents using many fonts, peak memory usage increases by 2x per font during registration. After `fclose`, the MEMFS copy is the only one (the `_malloc` buffer is freed). **Likely not a problem but worth monitoring.**

---

## Sources

- `solenv/gbuild/platform/EMSCRIPTEN_INTEL_GCC.mk` -- Build flags, JSPI configuration, FORCE_FILESYSTEM, EXPORTED_RUNTIME_METHODS (HIGH confidence, primary source)
- `configure.ac` lines 2294-2308, 4378-4392 -- JSPI and PROXY_TO_PTHREAD configure options (HIGH confidence, primary source)
- `config_host/config_emscripten.h.in` -- HAVE_EMSCRIPTEN_JSPI default values (HIGH confidence, primary source)
- `vcl/qt5/QtInstance.cxx` -- Working JSPI patterns: `emscripten_proxy_promise`, `emscripten_promise_await` (HIGH confidence, primary source)
- `vcl/source/app/scheduler.cxx` -- JSPI-aware task scheduling (HIGH confidence, primary source)
- `static/source/unoembindhelpers/PrimaryBindings.cxx` -- EM_JS patterns for heap manipulation, `HEAPU8`, `HEAPU16`, `HEAPU32`, `_malloc`, `_free` (HIGH confidence, primary source)
- `bridges/source/cpp_uno/gcc3_wasm/cpp2uno.cxx` -- EM_JS pattern with `UTF8ArrayToString` (HIGH confidence, primary source)
- `desktop/source/app/initjsunoscripting.cxx` -- EM_JS with `emscripten::val`, `EM_ASM` patterns (HIGH confidence, primary source)
- `vcl/unx/generic/gdi/freetypetextrender.cxx` lines 96-129 -- `AddTempDevFont` implementation (HIGH confidence, primary source)
- `vcl/unx/generic/fontmanager/fontmanager.cxx` lines 141-165 -- `addFontFile` implementation (HIGH confidence, primary source)
- `vcl/unx/generic/fontmanager/fontconfig.cxx` lines 785-799 -- `addFontconfigFile` / `FcConfigAppFontAddFile` (HIGH confidence, primary source)
- `vcl/unx/generic/fontmanager/fontsubst.cxx` -- `FcPreMatchSubstitution::FindFontSubstitute` (HIGH confidence, primary source)
- V8 blog JSPI documentation -- JSPI browser support timeline (MEDIUM confidence, training data)
- Electron release schedule -- Chromium version mapping (MEDIUM confidence, training data)
- Emscripten Asyncify documentation -- Binary size overhead claims (MEDIUM confidence, training data)

---
*Stack research for: LibreOffice WASM system font resolution -- Emscripten interop mechanisms*
*Researched: 2026-02-09*
