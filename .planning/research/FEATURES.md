# Feature Research

**Domain:** LibreOffice WASM system font resolution (C++ hook + JS callback API contract)
**Researched:** 2026-02-09
**Confidence:** HIGH (based on direct codebase analysis of VCL font subsystem)

## Feature Landscape

### Table Stakes (Must Have or the Feature Does Not Work)

Features without which the font resolution mechanism is non-functional.

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| **Font family name request** | The C++ hook must send the requested font family name to JS. This is the minimum data needed to resolve a font. Available via `rFontSelData.maTargetName` in `FontSelectPattern`. | LOW | UTF-8 string, pass via `EM_JS` char pointer. The name comes from the document (e.g., "Calibri", "Arial"). |
| **Raw font file data return** | JS must return the actual font file bytes (TTF/OTF) to C++. LibreOffice uses FreeType for rasterization, which needs the raw font file. There is no "just give me a name" path -- without bundled fonts, the bytes must come from the host. | MEDIUM | JS allocates into WASM heap via `Module._malloc()`, writes bytes, returns pointer + length. C++ writes to Emscripten virtual FS, then calls `AddTempDevFont()`. |
| **Null/failure return** | JS must be able to signal "font not available" so C++ falls through to its existing fallback chain (fontconfig substitution, generic glyph fallback list, default font). Without this, a missing font would hang or crash. | LOW | Return null pointer or length 0. C++ checks and continues normal fallback. |
| **TTF and OTF format support** | These are the two standard system font formats. FreeType (used by the headless/SVP backend) natively supports both. If the hook cannot accept these, it cannot handle the vast majority of system fonts. | LOW | No extra work -- FreeType handles both. The `analyzeFontFile()` path in `PrintFontManager` already checks extensions and delegates to FreeType. |
| **Synchronous C++ to JS call** | The font resolution is called from deep inside `PhysicalFontCollection::FindFontFamily()` and `FcPreMatchSubstitution::FindFontSubstitute()`. These are synchronous C++ call chains with no async provisions. The JS call MUST appear synchronous to the C++ caller. | HIGH | Requires JSPI (preferred, no binary overhead) or ASYNCIFY (adds ~10% binary size). JSPI wraps the `EM_JS` function to suspend WASM execution while a JS Promise resolves. This is the single hardest technical constraint. |
| **Write-to-virtual-FS + AddTempDevFont registration** | After receiving font bytes, C++ must write them to a file path on Emscripten's MEMFS and call `AddTempDevFont(pFontCollection, fileURL, fontName)` to register with FreeType/PrintFontManager. This is the existing API for runtime font addition. | MEDIUM | Path: write bytes to `/tmp/fonts/<familyname>.ttf`, convert to `file:///tmp/fonts/...` URL, call `AddTempDevFont()`. The `FreeTypeTextRenderImpl::AddTempDevFont()` delegates to `PrintFontManager::addFontFile()` which calls `analyzeFontFile()` (FreeType inspection) then `FreetypeManager::AddFontFile()` + `AnnounceFonts()`. |
| **Negative result caching** | If JS says "I don't have Calibri," the hook must not ask again for Calibri during this session. Without this, every text layout operation on a document using that font would trigger a JS roundtrip. The existing `FcPreMatchSubstitution` already caches (up to 256 entries, MRU). | LOW | Simple `std::unordered_set<OUString>` of family names that returned null. Check before calling JS. |
| **Guard against re-registration** | If a font was already fetched and registered via `AddTempDevFont()`, the hook must not fetch it again. The `PhysicalFontCollection` will find it via `ImplFindFontFamilyBySearchName()` on retry, but the hook itself must recognize "I already fetched this one." | LOW | Same cache as negative results, but tracking positive results. Or simply: after `AddTempDevFont()`, re-enter the normal font lookup which will now succeed. |

### Differentiators (Makes It Robust/Production-Ready)

Features that are not strictly required for basic operation but make the system reliable under real-world conditions.

| Feature | Value Proposition | Complexity | Notes |
|---------|-------------------|------------|-------|
| **Font style/weight/width hint forwarding** | Send `FontWeight` (e.g., BOLD), `FontItalic` (e.g., ITALIC_NORMAL), `FontWidth`, and `FontPitch` alongside the family name. This lets the JS host return the correct variant (e.g., `Calibri-Bold.ttf` not `Calibri-Regular.ttf`). Without this, the host must guess or return all variants. Available from `FontSelectPattern` which inherits `FontAttributes`. | LOW | Add integer parameters to the EM_JS call: weight (enum 0-10), italic (enum 0-2), width (enum 0-9), pitch (enum 0-2). JS host uses these to pick the best file from system font metadata. |
| **Multi-variant batch return** | Let JS return multiple font files in one call (e.g., Regular + Bold + Italic + BoldItalic for a family). Documents typically use multiple weights/styles of the same family. Fetching all variants at once avoids N sequential roundtrips as layout progresses through the document. | MEDIUM | JS returns an array of {data, length} pairs. C++ loops and calls `AddTempDevFont()` for each. Complicates the API but big performance win for typical documents. |
| **TTC (TrueType Collection) support** | Some system fonts (especially CJK) are packaged as `.ttc` files containing multiple faces. FreeType and `PrintFontManager::analyzeFontFile()` already handle TTC -- multiple `PrintFont` entries are created per collection index. The hook just needs to accept `.ttc` files without special-casing. | LOW | Already handled by the existing `AddTempDevFont()` path. `analyzeFontFile()` iterates `FT_Open_Face` with increasing face indices. |
| **WOFF/WOFF2 decompression** | System fonts are never WOFF, but web fonts embedded in documents could be. FreeType does NOT natively handle WOFF2. Supporting this would require a decompression step. | HIGH | Not a priority -- system fonts are TTF/OTF/TTC. If needed later, decompress on JS side before returning raw TTF/OTF bytes. Do not add WOFF decompression to the C++ hook. |
| **Positive result caching with persistence** | Cache successfully-resolved font data in WASM's MEMFS (or IndexedDB via Emscripten's IDBFS) so fonts survive page reloads. For the Electron use case, fonts persist on the filesystem anyway, so this matters less. | MEDIUM | Host-side concern per PROJECT.md out-of-scope. But the C++ side should use deterministic file paths (`/tmp/fonts/<normalized_name>.ttf`) so that if the virtual FS is pre-populated, `AddTempDevFont()` can find them. |
| **Logging/diagnostics** | Log font resolution requests and outcomes at `SAL_INFO("vcl.fonts", ...)` level. The existing substitution code already uses this pattern extensively. Essential for debugging "why does my document look wrong." | LOW | Use existing `SAL_INFO("vcl.fonts", ...)` pattern. Log: font name requested, whether JS was called or cache hit, whether JS returned data or null, `AddTempDevFont()` success/failure. |
| **Font family type hint** | Forward `FontFamily` enum (ROMAN/SWISS/MODERN/SCRIPT/DECORATIVE) so the JS host can provide a reasonable generic fallback if the exact font is not installed. E.g., if "Palatino" is not found but type is ROMAN, the host could return "Times New Roman." | LOW | One additional integer parameter to EM_JS. JS host can ignore it if doing exact-match only. |
| **Language/script hint** | Forward `meLanguage` (LanguageType) from `FontSelectPattern` so the JS host can select CJK-appropriate fonts. A request for a CJK font family needs a CJK-capable font in response. | LOW | One additional integer parameter. Important for CJK document rendering accuracy. |
| **Timeout/cancellation** | If the JS side takes too long (e.g., font file is huge, IPC hangs), the C++ side should give up rather than blocking the WASM thread indefinitely. With JSPI, this means the JS Promise should have a timeout. | LOW | Implement on JS side: `Promise.race([fetchFont(), timeout(5000)])`. C++ hook treats timeout same as "not found." |

### Anti-Features (Things to Deliberately NOT Build in the C++ Side)

Features that seem good but create problems, add unnecessary complexity, or belong in the JS/host layer.

| Feature | Why Requested | Why Problematic | Alternative |
|---------|---------------|-----------------|-------------|
| **C++ font matching/scoring** | "The C++ side should try to find the best match from multiple candidates returned by JS." | LibreOffice already has an elaborate font matching engine (`FindFontFamilyByAttributes()`, 400+ lines of scoring logic). Duplicating matching logic in the hook means maintaining two matching systems that can disagree. The hook's job is to make fonts AVAILABLE, not to CHOOSE between them. | Return the single best match from JS. If the host returns a font, register it. Let LibreOffice's existing matching engine evaluate it alongside bundled fonts. |
| **Font metadata parsing on JS side** | "JS should parse the font file to extract family name, weight, style before returning." | Adds complexity to the JS contract. FreeType already parses all this when `analyzeFontFile()` is called via `AddTempDevFont()`. Parsing twice is wasteful and can lead to name mismatches. | JS returns raw bytes + the file path or family name hint. C++ lets FreeType do all metadata extraction. |
| **Font subsetting in the hook** | "Only send the glyphs we need to reduce transfer size." | Font subsetting is complex (requires glyph dependency analysis, table rewriting). The `CreateFontSubset()` method exists in `PhysicalFontFace` but is for PDF embedding, not for font loading. A subsetted font cannot be reused for other characters. | Send full font files. They are already on the local filesystem (Electron), so transfer is fast (IPC, not network). Typical font file is 50-500KB. |
| **Dynamic font enumeration from JS** | "At startup, JS should provide a list of all available system fonts so LibreOffice can populate its font menu." | Requires enumerating potentially thousands of system fonts, parsing metadata for all of them, and transferring data at startup. Massive startup time penalty. The font menu is not needed for headless PDF conversion (the primary use case per PROJECT.md). | Lazy resolution: only fetch fonts when actually needed for document rendering. If font menu is needed later, add as a separate opt-in feature. |
| **Font format conversion in C++** | "Convert WOFF2 to TTF in C++, handle variable fonts, etc." | Adds library dependencies (brotli for WOFF2), increases WASM binary size, and is unnecessary for system fonts which are already TTF/OTF. Variable font support already works in FreeType. | If format conversion is needed, do it on the JS side before returning bytes. Keep the C++ hook simple: it receives TTF/OTF/TTC bytes and registers them. |
| **Cross-origin font loading** | "Support loading fonts from URLs/CDNs." | This is a browser-based concern, not relevant for Electron. Adds CORS complexity, network latency, and security considerations. The hook is for local system fonts. | Out of scope. If needed, the JS host can fetch from any source and pass the bytes through the same callback. The C++ side does not need to know the font's origin. |
| **Async font resolution with callback** | "Make the font hook async -- fire a request and re-layout when the font arrives." | This would require fundamental changes to VCL's synchronous font resolution pipeline. `FindFontFamily()` is called inline during text layout. Making it async means restructuring the entire layout engine to handle pending fonts. | Use JSPI to make the JS call appear synchronous. The WASM thread suspends, JS resolves the Promise, WASM resumes. No changes to VCL's synchronous architecture. |
| **Font license checking** | "Verify the font license allows embedding/use before registering." | Not the C++ hook's responsibility. License compliance is a user/host concern. Adding license checking adds complexity and policy decisions that do not belong in the rendering engine. | Document that the host is responsible for ensuring fonts it provides are licensed appropriately. |

## Feature Dependencies

```
[Synchronous C++<->JS call (JSPI)]
    |
    +--requires--> [Font family name request]
    |                   |
    |                   +--requires--> [Null/failure return]
    |                   |
    |                   +--requires--> [Raw font file data return]
    |                                       |
    |                                       +--requires--> [TTF/OTF format support]
    |                                       |
    |                                       +--requires--> [Write-to-virtual-FS + AddTempDevFont]
    |
    +--requires--> [Negative result caching]
    |
    +--requires--> [Guard against re-registration]

[Font style/weight hints] --enhances--> [Font family name request]

[Language/script hint] --enhances--> [Font family name request]

[Multi-variant batch return] --enhances--> [Raw font file data return]
    (reduces roundtrips but complicates API)

[TTC support] --enhances--> [TTF/OTF format support]
    (already handled by existing FreeType path, no extra work)

[Logging/diagnostics] --enhances--> ALL features
    (independent, can add at any time)

[Timeout/cancellation] --enhances--> [Synchronous C++<->JS call]
    (JS-side implementation, no C++ dependency)
```

### Dependency Notes

- **Synchronous C++<->JS call requires JSPI:** This is the foundation. Without JSPI (or ASYNCIFY), no feature works because the C++ font lookup is synchronous and the JS font resolution is async. JSPI must be validated first.
- **Raw font data return requires Write-to-virtual-FS:** The existing `AddTempDevFont()` API takes a file URL, not a memory buffer. Font bytes must be written to MEMFS first. A future optimization could add a buffer-based `AddTempDevFont()` overload, but the file-based path works and is lower risk.
- **Multi-variant batch return enhances but complicates:** Returning multiple variants at once is a significant performance optimization for real documents, but it complicates the API contract (array vs. single value). Can be added in v1.x without changing the core hook architecture.
- **Negative result caching is effectively required:** Without it, performance degrades catastrophically on documents with many missing fonts. Each text layout pass re-queries JS for every missing font. Treat as table stakes despite being technically separable.
- **Guard against re-registration requires no extra work if cache is implemented:** The positive cache (font already fetched and registered) naturally prevents re-fetching. On retry after `AddTempDevFont()`, `ImplFindFontFamilyBySearchName()` finds the newly registered font.

## MVP Definition

### Launch With (v1)

Minimum viable hook -- what is needed to validate that system fonts can be resolved through JS.

- [x] **Font family name request via EM_JS** -- pass `maTargetName` as UTF-8 string to JS
- [x] **Raw font data return** -- JS returns pointer + length to WASM heap, or null
- [x] **Null/failure return handling** -- treat null as "not found," fall through to existing fallback
- [x] **TTF/OTF support** -- no extra work, FreeType handles it
- [x] **Write-to-virtual-FS + AddTempDevFont** -- write to `/tmp/fonts/`, register, retry lookup
- [x] **Negative result cache** -- `std::unordered_set<OUString>` of names that returned null
- [x] **Positive result tracking** -- same set, prevents re-fetching already-registered fonts
- [x] **JSPI synchronous bridge** -- EM_JS function that returns a Promise, JSPI suspends/resumes WASM
- [x] **Basic SAL_INFO logging** -- log requests, cache hits, JS responses

### Add After Validation (v1.x)

Features to add once the basic hook is working end-to-end.

- [ ] **Font style/weight/width/pitch hints** -- pass additional integers so JS can select the right variant. Trigger: documents rendering with wrong weight/style.
- [ ] **Language/script hint** -- pass `meLanguage` for CJK font selection. Trigger: CJK documents rendering with wrong fonts.
- [ ] **Multi-variant batch return** -- JS returns all variants for a family at once. Trigger: performance profiling shows sequential roundtrips are a bottleneck.
- [ ] **TTC support validation** -- confirm TTC files from the host register correctly. Trigger: CJK font testing (many CJK fonts are TTC).
- [ ] **Font family type hint** -- pass `FontFamily` enum for generic fallback. Trigger: host wants to provide "best serif" when exact font is unavailable.

### Future Consideration (v2+)

Features to defer until the core mechanism is proven in production.

- [ ] **Font enumeration API** -- let JS provide a font list at startup for font menus. Defer: only needed for interactive (non-headless) use.
- [ ] **Persistent font cache** -- cache fetched fonts in IndexedDB/IDBFS. Defer: Electron use case has no cross-session persistence need.
- [ ] **WOFF/WOFF2 support** -- decompress on JS side. Defer: system fonts are not WOFF; only relevant for web-embedded fonts.
- [ ] **Buffer-based AddTempDevFont** -- skip the MEMFS write, register font directly from memory. Defer: optimization that requires new FreeType/PrintFontManager API surface.

## Feature Prioritization Matrix

| Feature | User Value | Implementation Cost | Priority |
|---------|------------|---------------------|----------|
| Font family name request | HIGH | LOW | P1 |
| Raw font data return | HIGH | MEDIUM | P1 |
| Null/failure return | HIGH | LOW | P1 |
| TTF/OTF support | HIGH | LOW (free) | P1 |
| JSPI synchronous bridge | HIGH | HIGH | P1 |
| Write-to-virtual-FS + AddTempDevFont | HIGH | MEDIUM | P1 |
| Negative result cache | HIGH | LOW | P1 |
| Re-registration guard | MEDIUM | LOW | P1 |
| Logging/diagnostics | MEDIUM | LOW | P1 |
| Font style/weight hints | MEDIUM | LOW | P2 |
| Language/script hint | MEDIUM | LOW | P2 |
| Multi-variant batch return | MEDIUM | MEDIUM | P2 |
| TTC support validation | MEDIUM | LOW | P2 |
| Font family type hint | LOW | LOW | P2 |
| Timeout/cancellation | LOW | LOW | P2 |
| Font enumeration API | LOW | HIGH | P3 |
| Persistent font cache | LOW | MEDIUM | P3 |
| WOFF/WOFF2 decompression | LOW | HIGH | P3 |
| Buffer-based AddTempDevFont | LOW | HIGH | P3 |

**Priority key:**
- P1: Must have for launch -- without these, fonts cannot be resolved from the host
- P2: Should have, add when production use reveals the need
- P3: Nice to have, future consideration after core mechanism is proven

## JS Callback API Contract (Recommended)

The API that the C++ EM_JS hook exposes to the JS host. This is what the Electron implementor programs against.

### v1 (MVP) Contract

```
C++ calls JS with:
  - familyName: string (UTF-8, e.g., "Calibri")

JS returns to C++:
  - On success: { ptr: number, length: number }
    (pointer to font file bytes in WASM heap, allocated via Module._malloc)
  - On failure: null
    (C++ frees nothing, falls through to existing fallback)

Memory ownership:
  - JS allocates via Module._malloc(), writes font bytes via Module.HEAPU8.set()
  - C++ reads bytes, writes to MEMFS file, then calls Module._free() on the pointer
```

### v1.x (Enhanced) Contract

```
C++ calls JS with:
  - familyName: string (UTF-8)
  - weight: number (0=DONTKNOW, 1=THIN, 4=NORMAL, 7=BOLD, 10=BLACK)
  - italic: number (0=NONE, 1=OBLIQUE, 2=NORMAL)
  - width: number (0=DONTKNOW, 1=ULTRA_CONDENSED, 5=NORMAL, 9=ULTRA_EXPANDED)
  - pitch: number (0=DONTKNOW, 1=FIXED, 2=VARIABLE)

JS returns to C++:
  - Same as v1, or array of { ptr, length } for batch variant return
```

## Comparable Systems Analysis

| System | How It Resolves Fonts | Our Approach |
|--------|----------------------|--------------|
| **LibreOffice Desktop (Linux)** | fontconfig queries system font directories, returns font file paths. `FcPreMatchSubstitution` caches results. | We replace the fontconfig query with a JS callback. Same caching pattern, same `AddTempDevFont()` registration. |
| **LibreOffice Desktop (macOS)** | CoreText API, `AquaSalGraphics::AddTempDevFont()` variant. | Different backend, but same abstract pattern: ask system for font, register it. |
| **Chromium WASM** | Bundles fonts or uses CSS `@font-face`. No system font access from WASM. | We go further by bridging to the host's system fonts via IPC. |
| **Qt WASM** | Bundles fonts as Qt resources (`Q_INIT_RESOURCE(wasmfonts)`). No dynamic resolution. | We avoid the bundle approach in favor of lazy resolution. |
| **Figma (WASM)** | Uses Local Font Access API in browser, or font upload. | Similar concept (system font access from WASM) but browser-only API. We use Electron IPC instead. |

## Sources

- Direct codebase analysis of LibreOffice VCL font subsystem (all paths verified against source):
  - `/Users/cadenz/Dev/lode/dev/core/vcl/inc/font/fontsubstitution.hxx` -- substitution interfaces
  - `/Users/cadenz/Dev/lode/dev/core/vcl/inc/font/FontSelectPattern.hxx` -- font request data structure
  - `/Users/cadenz/Dev/lode/dev/core/vcl/inc/fontattributes.hxx` -- font attribute fields
  - `/Users/cadenz/Dev/lode/dev/core/vcl/inc/font/PhysicalFontCollection.hxx` -- font collection with hook points
  - `/Users/cadenz/Dev/lode/dev/core/vcl/inc/font/PhysicalFontFace.hxx` -- font face abstraction (FreeType/HarfBuzz)
  - `/Users/cadenz/Dev/lode/dev/core/vcl/source/font/PhysicalFontCollection.cxx` -- full font lookup chain (1200+ lines)
  - `/Users/cadenz/Dev/lode/dev/core/vcl/unx/generic/fontmanager/fontsubst.cxx` -- fontconfig substitution hooks (primary modification target)
  - `/Users/cadenz/Dev/lode/dev/core/vcl/unx/generic/fontmanager/fontconfig.cxx` -- fontconfig integration details
  - `/Users/cadenz/Dev/lode/dev/core/vcl/unx/generic/gdi/freetypetextrender.cxx` -- AddTempDevFont implementation
  - `/Users/cadenz/Dev/lode/dev/core/vcl/headless/svptext.cxx` -- headless backend font delegation
  - `/Users/cadenz/Dev/lode/dev/core/vcl/headless/svpinst.cxx` -- EMSCRIPTEN-specific instance code
  - `/Users/cadenz/Dev/lode/dev/core/bridges/source/cpp_uno/gcc3_wasm/cpp2uno.cxx` -- EM_JS usage pattern
  - `/Users/cadenz/Dev/lode/dev/core/desktop/source/app/initjsunoscripting.cxx` -- EM_JS + emscripten::val patterns
- PROJECT.md validated requirements and architecture decisions

---
*Feature research for: LibreOffice WASM system font resolution*
*Researched: 2026-02-09*
