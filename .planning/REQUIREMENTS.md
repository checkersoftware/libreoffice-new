# Requirements — LibreOffice WASM System Font Resolution

**Version:** v1
**Scope:** Core font resolution hook + style hints + TTC support

## Functional Requirements

### REQ-F01: Font Family Name Request
**Priority:** P1 (Table Stakes)
**Description:** When a requested font is not found during font substitution, the C++ hook passes the font family name (UTF-8 string from `FontSelectPattern.maTargetName`) to a JavaScript callback via `EM_ASYNC_JS`. This is the primary lookup key.
**Acceptance:** A document requesting "Calibri" triggers a JS callback with the string "Calibri".

### REQ-F02: Raw Font File Data Return
**Priority:** P1 (Table Stakes)
**Description:** The JavaScript callback returns raw font file bytes (TTF/OTF/TTC) to the C++ side. The JS side writes font data to the Emscripten virtual filesystem via `FS.writeFile()` and returns the file path string to C++. C++ then reads from the virtual FS path.
**Acceptance:** A TTF file written by JS to `/tmp/fonts/calibri.ttf` is readable by C++ via standard POSIX file I/O.

### REQ-F03: Null/Failure Return Handling
**Priority:** P1 (Table Stakes)
**Description:** When the JavaScript callback cannot provide the requested font, it returns an empty string (no file path). C++ treats this as "not found" and falls through to the existing fontconfig substitution/fallback chain. No crash, no hang.
**Acceptance:** Requesting a font that the host doesn't have results in LibreOffice using its normal fallback font, not a crash or infinite loop.

### REQ-F04: TTF and OTF Format Support
**Priority:** P1 (Table Stakes)
**Description:** The hook accepts TrueType (.ttf) and OpenType (.otf) font files. FreeType natively supports both formats. No additional format handling is needed in the C++ hook.
**Acceptance:** Both .ttf and .otf files from the host register successfully and render text correctly.

### REQ-F05: JSPI Synchronous Bridge
**Priority:** P1 (Table Stakes)
**Description:** The C++ → JS call uses `EM_ASYNC_JS` with JSPI (JavaScript Promise Integration) to suspend the WASM thread while JavaScript resolves the font. This makes the inherently async JS operation appear synchronous to LibreOffice's synchronous font resolution pipeline. The function must be added to `-sJSPI_EXPORTS` in the Emscripten build configuration.
**Acceptance:** Font resolution from JS completes without deadlocking, and the C++ caller receives the result synchronously. WASM thread suspends and resumes correctly via JSPI.
**Fallback:** If JSPI is not available in the build (`!HAVE_EMSCRIPTEN_JSPI`), the hook is disabled and font resolution falls through to the existing pipeline. A synchronous-only path using `EM_JS` (without async) can work if the JS callback itself is synchronous (e.g., `ipcRenderer.sendSync` in Electron).

### REQ-F06: Write-to-Virtual-FS and AddTempDevFont Registration
**Priority:** P1 (Table Stakes)
**Description:** After receiving font data from JS (written to Emscripten MEMFS), C++ calls `AddTempDevFont(pFontCollection, fileURL, fontName)` to register the font with the existing pipeline: fontconfig (`FcConfigAppFontAddFile`), PrintFontManager (`analyzeFontFile`), FreetypeManager (`AddFontFile`), and PhysicalFontCollection (`AnnounceFonts`). The font is then available for subsequent lookups.
**Acceptance:** After registration, `PhysicalFontCollection::ImplFindFontFamilyBySearchName()` finds the newly registered font on retry.

### REQ-F07: Negative Result Caching
**Priority:** P1 (Table Stakes)
**Description:** Maintain a set of font family names that JavaScript reported as unavailable. Before calling JS, check this cache. If the font was previously reported as unavailable, skip the JS call and fall through immediately. This prevents repeated JS roundtrips during document layout for the same missing font.
**Acceptance:** A font that JS reports as unavailable triggers only ONE JS call per session, regardless of how many times layout requests it.

### REQ-F08: Re-Registration Guard
**Priority:** P1 (Table Stakes)
**Description:** Maintain a set of font family names that were successfully fetched and registered. Before calling JS, check this set. If the font was already registered, skip the JS call — the normal font lookup will find it.
**Acceptance:** A font that was previously fetched and registered triggers zero additional JS calls.

### REQ-F09: Diagnostic Logging
**Priority:** P1 (Table Stakes)
**Description:** Log font resolution activity using the existing `SAL_INFO("vcl.fonts", ...)` pattern. Log: font name requested, cache hit/miss, JS call made, JS response (success/null), `AddTempDevFont` result (success/failure), retry outcome.
**Acceptance:** Setting `SAL_LOG=+INFO.vcl.fonts` shows the complete font resolution flow for debugging.

### REQ-F10: Font Style/Weight/Width/Pitch Hints
**Priority:** P2 (Differentiator — included in v1)
**Description:** Forward `FontWeight` (BOLD, NORMAL, etc.), `FontItalic` (NONE, OBLIQUE, NORMAL), `FontWidth` (CONDENSED, NORMAL, EXPANDED), and `FontPitch` (FIXED, VARIABLE) from `FontSelectPattern` alongside the family name to the JS callback. This enables the host to return the correct font variant (e.g., `Calibri-Bold.ttf` instead of `Calibri-Regular.ttf`).
**Acceptance:** A document using bold Arial triggers a JS callback that includes weight=BOLD, and the host can distinguish this from a regular-weight request.

### REQ-F11: TTC (TrueType Collection) Support
**Priority:** P2 (Differentiator — included in v1)
**Description:** Accept `.ttc` font files from the host. FreeType and `PrintFontManager::analyzeFontFile()` already handle TTC by iterating face indices. No special handling needed in the hook — just ensure TTC files are not rejected.
**Acceptance:** A CJK `.ttc` font file from the host registers all contained faces and renders CJK text correctly.

## Non-Functional Requirements

### REQ-N01: Platform Isolation
**Priority:** P1
**Description:** All modifications must be behind `#ifdef EMSCRIPTEN` preprocessor guards. No changes to the font resolution pipeline on desktop platforms (Windows, macOS, Linux). The existing font substitution behavior is completely unchanged for non-WASM builds.
**Acceptance:** Desktop builds compile and run with zero behavioral changes. No new warnings or errors.

### REQ-N02: Binary Compatibility
**Priority:** P1
**Description:** Changes must not break the existing VCL font substitution pipeline. The hook is additive — it adds a new resolution path that runs only when fontconfig fails on WASM, before the existing fallback chain.
**Acceptance:** Existing font substitution tests pass. Documents that rendered correctly before the change still render correctly.

### REQ-N03: No Binary Size Overhead (Desktop)
**Priority:** P1
**Description:** Desktop builds must not include any Emscripten-specific code. WASM builds should not add significant binary size beyond the hook implementation itself (no Asyncify — use JSPI which has zero binary overhead).
**Acceptance:** Desktop build size is unchanged. WASM binary size increase is limited to the hook code itself (estimated <5KB).

### REQ-N04: Simple JS API Contract
**Priority:** P1
**Description:** The JavaScript callback interface must be simple enough for the host (Electron app) to implement in <50 lines. The C++ side defines the contract; the host implements it.
**Acceptance:** API contract is: receive font family name + style hints → return font file path on virtual FS (or empty string). Documented in source comments.

## Out of Scope (v1)

- **OS-01:** Electron IPC handler and JS-side font resolution logic
- **OS-02:** Browser Local Font Access API support
- **OS-03:** Font caching/persistence across sessions
- **OS-04:** Font enumeration API for font menus
- **OS-05:** Multi-variant batch return (JS returns all styles at once)
- **OS-06:** Language/script hint forwarding
- **OS-07:** WOFF/WOFF2 decompression
- **OS-08:** Persistent font cache (IDBFS)
- **OS-09:** Buffer-based AddTempDevFont (skip MEMFS write)

## Traceability

| Requirement | Research Source | Pitfall Addressed |
|-------------|---------------|-------------------|
| REQ-F01 | FEATURES.md: Font family name request | — |
| REQ-F02 | FEATURES.md: Raw font data return; PITFALLS.md: Binary data transfer | PITFALL-5: Heap corruption |
| REQ-F03 | FEATURES.md: Null/failure return | PITFALL-6: Infinite recursion |
| REQ-F04 | FEATURES.md: TTF/OTF support | — |
| REQ-F05 | STACK.md: JSPI; PITFALLS.md: EM_JS vs EM_ASYNC_JS | PITFALL-1: EM_JS confusion; PITFALL-2: JSPI_EXPORTS; PITFALL-3: Main thread deadlock |
| REQ-F06 | FEATURES.md: Write-to-VFS + AddTempDevFont; ARCHITECTURE.md | PITFALL-4: const method side effects; PITFALL-7: path vs URL |
| REQ-F07 | FEATURES.md: Negative result caching | PITFALL-6: Infinite recursion |
| REQ-F08 | FEATURES.md: Re-registration guard | — |
| REQ-F09 | FEATURES.md: Logging/diagnostics | — |
| REQ-F10 | FEATURES.md: Font style/weight hints | — |
| REQ-F11 | FEATURES.md: TTC support | — |
| REQ-N01 | PROJECT.md: Platform constraint | — |
| REQ-N02 | PROJECT.md: Binary compat constraint | — |
| REQ-N03 | STACK.md: JSPI vs Asyncify | — |
| REQ-N04 | PROJECT.md: API contract constraint | — |

---
*Requirements defined: 2026-02-09*
*v1 scope: 11 functional + 4 non-functional requirements*
