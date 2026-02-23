# Diffchecker LibreOffice Fork Changes

This document tracks all source code changes made to LibreOffice on the `sysfont-wasm` branch relative to `origin/master`. Its purpose is to serve as a reference when updating the fork to a newer upstream version of LibreOffice — each change explains **what** was modified, **why**, and **what to watch for** during conflict resolution.

For context on the overall WASM build system, see the [lo-wasm README](https://github.com/nicediffchecker/diffchecker/blob/master/packages/web/lib/convert/pdf/lo-wasm/README.md) in the Diffchecker repo.

## Table of Contents

- [Overview](#overview)
- [Changed Files](#changed-files)
  - [vcl/unx/generic/fontmanager/fontsubst.cxx](#fontsubstcxx)
  - [vcl/source/font/PhysicalFontCollection.cxx](#physicalfontcollectioncxx)
  - [vcl/unx/generic/glyphs/freetype_glyphcache.cxx](#freetype_glyphcachecxx)
  - [solenv/gbuild/platform/EMSCRIPTEN_INTEL_GCC.mk](#emscripten_intel_gccmk)
  - [static/CustomTarget_emscripten_fs_image.mk](#customtarget_emscripten_fs_imagemk)
  - [autogen.input](#autogeninput)
  - [sw/source/uibase/config/modcfg.cxx](#modcfgcxx)
  - [officecfg/registry/schema/org/openoffice/Office/Writer.xcs](#writerxcs)
  - [.gitignore](#gitignore)

---

## Overview

The changes fall into three categories:

1. **WASM System Font Resolution** — The core feature. Allows LibreOffice running in WASM to request fonts from the host environment (Electron) at runtime, so documents render with the correct fonts instead of metric-compatible substitutes.
2. **Headless Build Optimizations** — Strips UI resources, splash images, themes, and other files from the Emscripten virtual filesystem that are unnecessary for headless document-to-PDF conversion.
3. **Redline Color Customization** — Changes default tracked-change (redline) colors from "auto" to explicit RGB values, and adds environment variable overrides so Diffchecker can control them.

---

## Changed Files

### fontsubst.cxx

**Path:** `vcl/unx/generic/fontmanager/fontsubst.cxx`

**Purpose:** Core WASM system font resolution — the C++ side of the bridge that requests fonts from the host JS/Electron environment.

**What changed:**

All changes are guarded by `#ifdef EMSCRIPTEN` so they have zero impact on native builds.

#### New includes

`emscripten.h`, `emscripten/proxying.h`, `emscripten/threading.h`, `unordered_set`, `cstdlib`, `freetype_glyphcache.hxx`, `sal/log.hxx`.

#### `s_pFontCollection` static pointer + `setWasmFontCollection()` setter

A file-static `PhysicalFontCollection*` that tracks which font collection is currently being searched. The setter is called from `PhysicalFontCollection::FindFontFamily()` (see [PhysicalFontCollection.cxx](#physicalfontcollectioncxx)) immediately before each PreMatchHook invocation.

**Why this design:** When we dynamically load a font from the host, we need to register it into the LO font pipeline via `FreetypeManager::AnnounceFonts(collection)`. That function requires a pointer to the `PhysicalFontCollection` to announce into. However, the PreMatchHook (`FindFontSubstitute`) is a virtual method on a stateless singleton — it has no access to the collection that's currently searching. The hook only receives a `FontSelectPattern&` (the search query), not the collection doing the searching.

We can't just stash the pointer once at registration time either, because LibreOffice creates multiple `PhysicalFontCollection` instances for different `OutputDevice` types (e.g., one for screen rendering, another for PDF export). A font registered into the wrong collection won't be found by the current search. So the pointer must be updated to `this` right before every PreMatchHook call, which is why the setter exists in `PhysicalFontCollection.cxx` and is called at two separate call sites.

#### `s_aWasmNegativeCache`

An `unordered_set<OUString>` that caches font family names where the host JS reported the font as unavailable (or all font file registrations failed).

**Why this design:** Each font resolution round-trip is expensive — it blocks the C++ worker pthread, dispatches to the JS main thread, which does an IPC to the Electron main process. LibreOffice's internal default fonts (Segoe UI, Liberation Sans, CJK fonts, Lohit Hindi, etc.) trigger the PreMatchHook on every conversion even when the document doesn't use them. Without the negative cache, each of these would make a full IPC round-trip on every conversion. The cache is keyed by family name only (not weight/style) because the JS resolver returns all variants for a family at once.

#### `FontResolveRequest` struct

Shared state between the calling pthread and the main-thread proxy callback. Contains the family name (input), result VFS paths (output), and the Emscripten proxy context.

**Why this design:** Emscripten's `emscripten_proxy_sync_with_ctx` requires a single `void*` argument to pass to the main-thread callback. The struct bundles all input/output data into one allocation on the calling thread's stack, and the proxy context (`em_proxying_ctx*`) is filled in by the callback so the JS completion handler can later call `emscripten_proxy_finish` to unblock the waiting pthread.

#### `em_startFontResolve()` (EM_JS)

JavaScript function that runs on the main thread. Calls `globalThis.__resolveSystemFont(familyName)` (installed by Diffchecker's `libreoffice-wasm.ts`). Receives an `ArrayBuffer[]` of all font variants, writes each to Emscripten's VFS at `/tmp/fonts/<sanitized_name>_<index>.ttf`, and calls `_em_fontResolveComplete` on completion or failure.

**Why `globalThis` instead of Module:** The resolver can't be a property on the Emscripten `Module` object because the WASM module is loaded inside an iframe, and the resolver is installed by the parent page's code. `globalThis.__resolveSystemFont` is set on the iframe's global scope by `libreoffice-wasm.ts` before the module starts, making it accessible from `EM_JS` code. It may be possible to switch to using `Module` instead by adding the resolver to Emscripten's export list (via `EXPORTED_RUNTIME_METHODS` or `INCOMING_MODULE_JS_API` in `EMSCRIPTEN_INTEL_GCC.mk`), which would be cleaner than polluting the global scope — but `globalThis` works and hasn't caused issues.

**Why write to VFS then pass paths back to C++:** Font data arrives as JS `ArrayBuffer`s, but the LO font pipeline (`PrintFontManager::addFontFile`, `FreetypeManager::AddFontFile`) expects filesystem paths. Writing to Emscripten's MEMFS virtual filesystem at `/tmp/fonts/` bridges this gap — the C++ side sees real file paths it can open with standard POSIX I/O.

**Why `stringToNewUTF8`:** The VFS paths string needs to be returned to C++ across the async boundary. `stringToNewUTF8` allocates a C string on the Emscripten heap that persists after the JS callback returns, so the C++ side can read it and `free()` it later.

#### `em_fontResolveComplete()` (extern "C" EMSCRIPTEN_KEEPALIVE)

C callback invoked by JS when the async Promise resolves. Stores the result path string and calls `emscripten_proxy_finish()` to unblock the waiting pthread.

**Why a separate completion callback:** The font resolution is async (JS Promise from IPC). `emscripten_proxy_sync_with_ctx` blocks the pthread until `emscripten_proxy_finish` is called. The main-thread callback (`fontResolveOnMainThread`) kicks off the async JS work but must NOT call `proxy_finish` itself — that would unblock the pthread before the Promise resolves. Instead, the JS `.then()` handler calls this C function, which calls `proxy_finish` only after the font data is written to VFS.

#### `resolveFontFromHost()`

Blocks the calling pthread via `emscripten_proxy_sync_with_ctx()`, dispatching `fontResolveOnMainThread` to the main thread. Returns a newline-separated string of VFS paths, or nullptr if the font is unavailable.

**Why blocking sync instead of JSPI or async callbacks:** Emscripten JSPI (JavaScript Promise Integration) was considered but not used because it requires specific toolchain support and has compatibility issues. The proxy-sync approach is simpler: the C++ worker thread blocks on a futex (cheap — no busy-wait), the JS main thread runs the async resolver, and signals the futex when done. This works because LibreOffice's WASM build runs its main work on a pthread, leaving the JS main thread free for async operations.

#### `registerFontFromVFS()`

Registers a font file from the VFS path with the font pipeline. Replicates the logic of `FreeTypeTextRenderImpl::AddTempDevFont()` using singletons (`PrintFontManager`, `FreetypeManager`) and the stored `s_pFontCollection` pointer.

**Why replicate `AddTempDevFont` instead of calling it:** `AddTempDevFont` is a method on `FreeTypeTextRenderImpl` (the platform text render backend), which isn't accessible from this static context. The font registration logic itself is straightforward — `PrintFontManager::addFontFile()` parses the font and creates font IDs, then `FreetypeManager::AddFontFile()` creates the FreeType face entries, and `AnnounceFonts()` makes them visible in the collection. We extracted just these steps.

**Why not override the family name:** The function passes an empty `rFontName` and lets FreeType extract the real family name from the font file's `name` table. This is critical because the JS resolver returns ALL variants (Regular, Bold, Italic, BoldItalic) for a family at once. If we overrode every variant's name to the requested search name, LO's `IsBetterMatch()` scoring wouldn't be able to distinguish variants — it would pick arbitrarily instead of matching weight/italic correctly.

#### Hook into `FcPreMatchSubstitution::FindFontSubstitute()`

The WASM resolution block is inserted inside `FindFontSubstitute()`, **before** fontconfig's `GetFcSubstitute()` runs. The logic:

1. Check if the target font is actually missing from the `PhysicalFontCollection` (via `FindFontFamily`). This is important because fontconfig's substitution hook fires for ALL font requests, including fonts that are already available — we only want to call out to JS for genuinely missing fonts.
2. Check the negative cache. If the family was already tried and not found, skip directly to fontconfig substitution.
3. Call `resolveFontFromHost()` to ask the host for the font.
4. If font files are returned, register each one and `return false`.

**Why `return false` instead of `return true`:** This is the key trick. The PreMatchHook's contract is: return `true` if you performed a substitution (the caller uses `rFontSelData.maSearchName` as the substitute), or `false` if you didn't (the caller re-searches with whatever `maSearchName` is set to). By returning `false` after registering the real font AND resetting `maSearchName` to the target name, we tell the caller "I didn't substitute, search again" — and now the real font is in the collection, so `ImplFindFontFamilyBySearchName()` in the caller finds it directly. If we returned `true`, the caller would treat the font as a *substitution* and apply substitution-specific behavior, which we don't want — it's the actual requested font.

**Why reset `maSearchName`:** Before the WASM block runs, `FindMetricCompatibleFont()` may have already changed `maSearchName` to a metric-compatible substitute (e.g., "Calibri" → "carlito"). If we don't reset it back to the target name, the caller's `ImplFindFontFamilyBySearchName()` would search for "carlito" instead of "calibri", missing the newly registered Calibri font entirely.

#### Minor: `aOut` declaration change

`const` removed from `aOut` and `bHaveSubstitute` check reordered — the emptiness check (`aOut.maSearchName.isEmpty()`) now comes before `uselessmatch()`. This was necessary because the upstream code had `const` on `aOut` and combined the two checks. With the WASM block inserted above, the flow needed the checks to be separated for clarity, and `const` was dropped since it's not needed.

**Conflict risk:** HIGH — This is the most conflict-prone file. If upstream refactors `FcPreMatchSubstitution::FindFontSubstitute()` or changes the font matching pipeline, the EMSCRIPTEN block will need to be re-integrated. The key invariants are: (1) the WASM block must run before fontconfig substitution, (2) it must use `FindFontFamily` to check actual availability (not rely on the hook's own substitution result), (3) returning `false` after successful registration must cause the caller to find the newly registered font, and (4) `maSearchName` must be reset to the target name before returning.

---

### PhysicalFontCollection.cxx

**Path:** `vcl/source/font/PhysicalFontCollection.cxx`

**Purpose:** Ensures the `s_pFontCollection` pointer in `fontsubst.cxx` always points to the correct collection before the PreMatchHook fires.

**What changed:**

All changes are guarded by `#ifdef EMSCRIPTEN`.

1. **`extern` declaration** — Declares `setWasmFontCollection()` (defined in `fontsubst.cxx`).

2. **Two `setWasmFontCollection(this)` calls** in `PhysicalFontCollection::FindFontFamily()`:
   - Before the first `mpPreMatchHook->FindFontSubstitute()` call (~line 1083)
   - Before the second `mpPreMatchHook->FindFontSubstitute()` call after `FindMetricCompatibleFont` (~line 1119)

   Both use `const_cast<PhysicalFontCollection*>(this)` since `FindFontFamily` is a const method, but font registration requires mutating the collection.

**Why two call sites:** `FindFontFamily()` calls the PreMatchHook at two different points in its search cascade. The first call is the primary substitution attempt. The second call comes later in a loop that iterates over semicolon-separated font name tokens and also tries `FindMetricCompatibleFont()` before the hook. Both paths can trigger WASM font resolution, so both need the pointer updated. If only the first call site had the setter, a font requested via the metric-compatible path would be registered into whatever collection `s_pFontCollection` happened to point to from a previous (possibly different) search.

**Why `const_cast`:** `FindFontFamily()` is declared `const` in LO's API. We can't change the signature without touching the base class and all callers. The `const_cast` is safe here because `PhysicalFontCollection` is not actually immutable — `AnnounceFonts()` (which adds fonts to the collection) is a non-const method, and the collection is routinely mutated during initialization. The `const` on `FindFontFamily` is a design convention, not a strict guarantee.

**Why not pass the collection through the hook interface:** The `PreMatchFontSubstitution::FindFontSubstitute()` virtual method signature is `bool FindFontSubstitute(FontSelectPattern&) const` — it only takes the search pattern, not the collection. Changing this interface would require modifying the base class in `font/fontsubstitution.hxx` and all implementations across platforms (Windows, macOS, generic Unix), which is a much larger and riskier change. The static pointer approach keeps the modification contained to the EMSCRIPTEN-guarded code.

**Conflict risk:** MODERATE — If upstream changes the structure of `FindFontFamily()` (e.g., reorders the hook calls, adds new search steps, or changes the loop structure), the `setWasmFontCollection` calls need to stay immediately before each `mpPreMatchHook->FindFontSubstitute()` invocation. The pattern to look for is: anywhere `mpPreMatchHook->FindFontSubstitute(rFSD)` is called, `setWasmFontCollection(this)` must appear right before it.

---

### freetype_glyphcache.cxx

**Path:** `vcl/unx/generic/glyphs/freetype_glyphcache.cxx`

**Purpose:** Fixes HarfBuzz blob creation for fonts loaded from Emscripten's MEMFS virtual filesystem.

**What changed:**

The `CreateHbBlob()` function was rewritten. The original code had a special case for `/:FD:/` file-descriptor paths (used by the Android port) that used `pFontFile->Map()`, and all other paths used `hb_blob_create_from_file()`.

The new logic:
1. **Always try `pFontFile->Map()` first** — If the font file can be memory-mapped, create the HarfBuzz blob from the mapped buffer. This works correctly on all platforms including Emscripten's MEMFS.
2. **Fallback to `hb_blob_create_from_file()`** — Only if mapping fails, try loading from the file path directly (works on native platforms).

**Why:** `hb_blob_create_from_file()` is HarfBuzz's own file loading function — it uses standard C `fopen`/`fread` internally. On native platforms this works fine, but on Emscripten, HarfBuzz is compiled as a separate library that may not have its file I/O routed through Emscripten's VFS layer. This means `hb_blob_create_from_file("/tmp/fonts/Calibri_0.ttf")` fails for dynamically loaded fonts even though the file exists in MEMFS.

`FreetypeFontFile::Map()` works because LO's own code goes through Emscripten's `mmap()` emulation, which correctly handles MEMFS files. By trying `Map()` first for all paths, we ensure dynamically loaded fonts always get valid HarfBuzz data. The `hb_blob_create_from_file()` fallback is kept for native platforms where `Map()` might not be available.

This change also removes the Android-specific `/:FD:/` path parsing. The `/:FD:/` prefix was a hack for Android's content URI scheme where fonts are accessed via file descriptors rather than paths. Since `Map()` handles FD-backed files too (via the original code's same `Map()` call), the unified approach covers both Android and Emscripten without special-casing.

**Conflict risk:** LOW-MODERATE — If upstream modifies `CreateHbBlob()` (e.g., adds new special cases or changes the FD path handling), the key requirement is that the `Map()` path must be tried first, and `hb_blob_create_from_file()` must only be a fallback.

---

### EMSCRIPTEN_INTEL_GCC.mk

**Path:** `solenv/gbuild/platform/EMSCRIPTEN_INTEL_GCC.mk`

**Purpose:** Emscripten linker flags for the WASM build.

**What changed:**

In `gb_EMSCRIPTEN_LDFLAGS`:

1. **`ASSERTIONS=1` → `ASSERTIONS=0`** — Disables Emscripten runtime assertions for a release build. `ASSERTIONS=1` adds runtime checks for memory access, stack overflow, and API usage — useful for debugging but adds significant overhead and code size to the WASM binary. Upstream uses `ASSERTIONS=1` because their WASM builds are experimental/dev-focused; we use `0` for production.

2. **`FS` always exported** — Moved `"FS"` from inside the `$(if $(ENABLE_QT6),...)` conditional to the unconditional part of `EXPORTED_RUNTIME_METHODS`. Upstream only exports `FS` when building with Qt6 (their interactive WASM demo), because that's the only upstream use case that needs JS-side filesystem access. Our headless build doesn't use Qt6 but needs `FS` for two things: (a) `libreoffice-wasm.ts` writes input documents to the VFS via `FS.writeFile()` before conversion and reads output PDFs via `FS.readFile()` after, and (b) the font resolution JS code writes font buffers to `/tmp/fonts/` via `FS.writeFile()`.

3. **Added `"stringToNewUTF8"`** — Added to `EXPORTED_RUNTIME_METHODS`. This Emscripten utility function allocates a UTF-8 C string on the WASM heap from a JS string. It's used in `em_startFontResolve()` to pass the newline-separated VFS paths string back to C++. Unlike `UTF8ToString` (which goes C→JS), `stringToNewUTF8` goes JS→C and heap-allocates so the string survives the async callback boundary.

4. **Removed `FETCH=1`** — The `FETCH` flag enables Emscripten's Fetch API for network requests. Not needed for headless operation where all I/O goes through the VFS.

**Conflict risk:** MODERATE — If upstream changes the `EXPORTED_RUNTIME_METHODS` list or adds/removes linker flags, merge carefully. The key requirements are: `FS` must always be exported (not gated on Qt6), `stringToNewUTF8` must be in the list, and `ASSERTIONS=0` for release builds.

---

### CustomTarget_emscripten_fs_image.mk

**Path:** `static/CustomTarget_emscripten_fs_image.mk`

**Purpose:** Controls which files are bundled into Emscripten's virtual filesystem (`soffice.data`). This file directly impacts the WASM binary size.

**What changed:**

~900+ lines were commented out (lines prefixed with `#`), removing files from the virtual filesystem that are not needed for headless document-to-PDF conversion. The commented-out files include:

- **Splash/branding images** — `intro.png`, `intro-highres.png`, shell logos/SVGs
- **UI dialog definitions** — `.ui` files for CUI, SFX2, SVX, SVT, Writer, Calc, Impress, Chart, etc.
- **Toolbar/menubar/statusbar XML** — UI layout definitions for all modules
- **Gallery files** — Clip art, backgrounds, shapes
- **Wizard CSS** — Wizard styling files
- **Icon themes** — Theme resource files
- **Fonts** — Bundled font files (fonts are loaded dynamically from the host instead)

Files that are **kept** (not commented out):
- UNO API type libraries (`offapi`, `udkapi`, `oovbaapi`)
- Bootstrap/config RC files
- `services.rdb`
- Filter data files (needed for format conversion)
- Registry `.xcd` configuration files
- Fontconfig data files
- Langtag data

**Important:** Some UI files that appear to be "UI only" are actually required for conversions to complete. If upstream adds new files to this list, they should be tested before commenting them out. If a conversion hangs or fails after commenting out a file, add it back.

**Conflict risk:** HIGH — This file changes frequently upstream as new UI files are added or existing ones are renamed/moved. During a rebase, expect many conflicts in this file. The resolution strategy is: accept upstream's new entries, then re-comment-out any that fall into the categories above. Test conversions after resolving.

---

### autogen.input

**Path:** `autogen.input`

**Purpose:** LibreOffice build configuration flags. This file was previously `.gitignore`d upstream — we un-ignored and committed it so the WASM build configuration is tracked.

**What it contains:**

```
--host=wasm32-local-emscripten          # Cross-compile target
--with-wasm-module=calc writer impress  # Include Writer, Calc, and Impress modules
--with-package-format=emscripten        # Emscripten packaging
--with-lang=                            # No extra language packs
--with-locales=en                       # English locale only
--with-parallelism                      # Parallel build
--without-fonts                         # Don't bundle fonts (loaded dynamically from host)
--disable-gui                           # Headless mode
--enable-wasm-strip                     # Strip WASM-specific UI code
--enable-ccache                         # Use ccache for faster rebuilds
--enable-customtarget-components        # Required for WASM component registration
--enable-release-build                  # Release optimizations
```

Plus ~25 `--disable-*` flags removing features not needed for headless conversion: scripting, database connectivity, extensions, PDF import, OpenCL/GL/Skia, LDAP, etc.

**Conflict risk:** NONE — This is a new file, not present upstream. No conflicts possible.

---

### modcfg.cxx

**Path:** `sw/source/uibase/config/modcfg.cxx`

**Purpose:** Allows overriding tracked-change (redline) colors via environment variables at runtime.

**What changed:**

At the end of `SwRevisionConfig::Load()`, after the normal config loading from the registry, three environment variable checks were added:

- `LO_REDLINE_INSERT_COLOR` — Overrides the insert redline color (hex RGB, e.g., `008800`)
- `LO_REDLINE_DELETE_COLOR` — Overrides the delete redline color (hex RGB, e.g., `FF0000`)
- `LO_REDLINE_CHANGE_COLOR` — Overrides the attribute-change redline color (hex RGB, e.g., `0000FF`)

Each reads the env var with `getenv()`, parses it as a hex uint32, and sets the corresponding `m_nColor` field.

**Why env vars instead of just changing Writer.xcs defaults:** The `Writer.xcs` defaults (see [Writer.xcs](#writerxcs)) provide the base colors, but env vars add a second layer of flexibility. Env vars can be changed at runtime by the Electron app without rebuilding LibreOffice — useful if Diffchecker wants different colors for different contexts (e.g., light vs dark mode, or per-customer branding). They're set via Emscripten's `ENV` object before the WASM module starts, which populates the C `environ` that `getenv()` reads from.

**Why placed at the end of `Load()`:** By running after the normal config load, the env vars always win over both the schema defaults (`Writer.xcs`) and any user-level config overrides. This ensures deterministic colors regardless of what config files exist in the VFS.

**Conflict risk:** LOW — The code is appended at the end of an existing function. Only conflicts if upstream significantly refactors `SwRevisionConfig::Load()`.

---

### Writer.xcs

**Path:** `officecfg/registry/schema/org/openoffice/Office/Writer.xcs`

**Purpose:** Changes default redline colors from "auto" (-1) to explicit RGB values.

**What changed:**

Three `<value>` elements changed:

| Setting | Old (upstream) | New | Color |
|---------|---------------|-----|-------|
| Insert color | `-1` (auto) | `34816` | Green (`0x008800`) |
| Delete color | `-1` (auto) | `16711680` | Red (`0xFF0000`) |
| Changed attribute color | `-1` (auto) | `255` | Blue (`0x0000FF`) |

**Why:** This makes sure that the redline colors will not be yellow by default. This can be changed back to -1 if wanted now that we have the env var setup.

**Conflict risk:** LOW — Only three `<value>` lines changed. Unless upstream restructures the Writer schema, conflicts are unlikely.

---

## Update Checklist

When rebasing onto a new upstream version:

1. **`fontsubst.cxx`** — Most likely to conflict. Verify the EMSCRIPTEN block still runs before fontconfig substitution in `FindFontSubstitute()`. Check that the `return false` trick still works with the caller's search logic.
2. **`PhysicalFontCollection.cxx`** — Verify `setWasmFontCollection(this)` calls are still immediately before each `mpPreMatchHook->FindFontSubstitute()` call.
3. **`freetype_glyphcache.cxx`** — Verify `CreateHbBlob()` still tries `Map()` first. If upstream adds new special cases, ensure MEMFS paths still go through `Map()`.
4. **`EMSCRIPTEN_INTEL_GCC.mk`** — Check if upstream changed `EXPORTED_RUNTIME_METHODS`. Ensure `FS`, `stringToNewUTF8` are present. Keep `ASSERTIONS=0`.
5. **`CustomTarget_emscripten_fs_image.mk`** — Expect heavy conflicts. Accept upstream additions, then re-comment-out UI/theme/gallery/font files. Test conversions.
6. **`Writer.xcs`** — Re-apply the three color value changes if they get overwritten.
7. **`modcfg.cxx`** — Re-apply the env var block at the end of `SwRevisionConfig::Load()`.
8. **`autogen.input`** — No conflicts (our file). Review upstream's `static/README.wasm.md` for any new required flags.
9. **`.gitignore`** — Re-remove the `/autogen.input` line if upstream re-adds it.
