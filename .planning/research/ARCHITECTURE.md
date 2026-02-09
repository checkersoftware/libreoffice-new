# Architecture Research: LibreOffice WASM Runtime Font Resolution

**Domain:** LibreOffice VCL font subsystem -- WASM dynamic font loading
**Researched:** 2026-02-09
**Confidence:** HIGH (all findings based on direct source code inspection of the LibreOffice codebase)

## System Overview

```
                       Document requests font "Calibri"
                                    |
                                    v
  +------------------------------------------------------------------+
  |                  PhysicalFontCollection                          |
  |                  (vcl/source/font/PhysicalFontCollection.cxx)    |
  |                                                                  |
  |  FindFontFamily(FontSelectPattern&)                              |
  |    1. Direct lookup by normalized name                           |
  |    2. Metric-compatible substitution (Calibri -> Carlito)        |
  |    3. PreMatchHook -> FcPreMatchSubstitution::FindFontSubstitute |
  |    4. Attribute-based matching                                   |
  |    5. Default font fallback                                      |
  +---------------------+------------------------------------------+
                        |
                        v
  +------------------------------------------------------------------+
  |            FcPreMatchSubstitution (fontsubst.cxx)                |
  |                                                                  |
  |  FindFontSubstitute(FontSelectPattern&)                          |
  |    - Check MRU cache (list of 256 entries)                       |
  |    - Call GetFcSubstitute() -> PrintFontManager::Substitute()    |
  |    - Substitute() queries fontconfig via FcFontMatch()           |
  |    - Returns substituted font name or empty on failure           |
  |                                                                  |
  |  ** THIS IS THE HOOK POINT FOR WASM FONT RESOLUTION **          |
  +---------------------+------------------------------------------+
                        |
           (font not found locally)
                        |
                        v
  +------------------------------------------------------------------+
  |            WASM Font Resolution (NEW - to be built)              |
  |                                                                  |
  |  1. Call out to JavaScript via emscripten                        |
  |  2. JS fetches font data (ArrayBuffer)                           |
  |  3. Write font file to Emscripten virtual FS                    |
  |  4. Register with fontconfig (addFontconfigFile)                 |
  |  5. Register with PrintFontManager (addFontFile)                 |
  |  6. Register with FreetypeManager (AddFontFile + AnnounceFonts)  |
  |  7. Invalidate FcPreMatchSubstitution cache                      |
  |  8. Retry substitution                                           |
  +------------------------------------------------------------------+
```

## Component Boundaries

| Component | Responsibility | Source File(s) | Communicates With |
|-----------|---------------|----------------|-------------------|
| **PhysicalFontCollection** | Master font lookup orchestrator. Holds the map of all known font families. Invokes PreMatchHook and FallbackHook during font resolution. | `vcl/source/font/PhysicalFontCollection.cxx`, `vcl/inc/font/PhysicalFontCollection.hxx` | FcPreMatchSubstitution, FcGlyphFallbackSubstitution, PhysicalFontFamily |
| **FcPreMatchSubstitution** | Fontconfig-based pre-match substitution hook. Maintains an MRU cache mapping FontSelectPattern -> substituted FontSelectPattern. Delegates to `PrintFontManager::Substitute()`. | `vcl/unx/generic/fontmanager/fontsubst.cxx` | PrintFontManager, PhysicalFontCollection |
| **FcGlyphFallbackSubstitution** | Fontconfig-based glyph-level fallback. Called when a specific glyph is missing from the selected font. Also delegates to `PrintFontManager::Substitute()`. | `vcl/unx/generic/fontmanager/fontsubst.cxx` | PrintFontManager, PhysicalFontCollection |
| **PrintFontManager** | Singleton managing all known fonts. Wraps fontconfig operations (init, query, add/remove fonts). Maps font IDs to file paths and font metadata. | `vcl/unx/generic/fontmanager/fontmanager.cxx`, `vcl/unx/generic/fontmanager/fontconfig.cxx`, `vcl/inc/unx/fontmanager.hxx` | fontconfig library, FreetypeManager |
| **FreetypeManager** | Singleton managing FreeType font face data. Maps font IDs to FreetypeFontInfo objects, which hold file references and font attributes. `AnnounceFonts()` pushes fonts into PhysicalFontCollection. | `vcl/unx/generic/glyphs/freetype_glyphcache.cxx`, `vcl/inc/unx/freetype_glyphcache.hxx` | PhysicalFontCollection, FreetypeFontInfo, FreetypeFontFile |
| **FreeTypeTextRenderImpl** | Bridges font management to graphics output. `GetDevFontList()` populates PhysicalFontCollection from PrintFontManager via FreetypeManager. `AddTempDevFont()` is the runtime font registration entry point. | `vcl/unx/generic/gdi/freetypetextrender.cxx` | PrintFontManager, FreetypeManager, PhysicalFontCollection |
| **SvpSalGraphics** | Headless (SVP) graphics backend. Delegates all font operations to `FreeTypeTextRenderImpl` via `m_aTextRenderImpl`. | `vcl/headless/svptext.cxx` | FreeTypeTextRenderImpl |
| **SvpSalInstance** | Headless SAL instance, used for WASM (Emscripten). Inherits from SalGenericInstance. Contains Emscripten-specific main loop (`emscripten_set_main_loop_arg`). | `vcl/headless/svpinst.cxx`, `vcl/inc/headless/svpinst.hxx` | SvpSalGraphics, SalGenericInstance |
| **SalGenericInstance** | Base class for Unix-like SAL instances. Owns `RegisterFontSubstitutors()` which installs both FcPreMatchSubstitution and FcGlyphFallbackSubstitution into PhysicalFontCollection. | `vcl/inc/unx/geninst.h`, `vcl/unx/generic/fontmanager/fontsubst.cxx` | PhysicalFontCollection, FcPreMatchSubstitution, FcGlyphFallbackSubstitution |
| **WASM Font Bridge (NEW)** | C++ side of JS interop. Called from the substitution hook when fontconfig cannot resolve a font. Uses Emscripten APIs to call JS, receive font data, write to virtual FS. | To be created in `vcl/` (likely near `fontsubst.cxx` or as a separate file) | JavaScript runtime, PrintFontManager, FreetypeManager, Emscripten virtual FS |
| **JS Font Provider (NEW)** | JavaScript module that receives font name requests, fetches font data (e.g., from IndexedDB, CDN, or local storage), and returns it to C++. | To be created as a JS/TS module | C++ WASM bridge, font CDN or storage |

## Data Flow

### Current Font Resolution Flow (no WASM hook)

```
Application requests font "Calibri"
    |
    v
PhysicalFontCollection::FindFontFamily(FontSelectPattern&)
    |
    +-- 1. Normalize name: "calibri"
    |
    +-- 2. Direct lookup in maPhysicalFontFamilies map
    |       -> NOT FOUND (Calibri not bundled)
    |
    +-- 3. FindMetricCompatibleFont: "calibri" -> "carlito"
    |       -> Lookup "carlito" in map
    |       -> FOUND if Carlito is installed, return
    |       -> NOT FOUND, continue
    |
    +-- 4. mpPreMatchHook->FindFontSubstitute(rFSD)
    |       |
    |       v
    |   FcPreMatchSubstitution::FindFontSubstitute()
    |       |
    |       +-- Check MRU cache (list<pair<pattern,pattern>>)
    |       |   -> cache miss on first request
    |       |
    |       +-- GetFcSubstitute()
    |       |       |
    |       |       v
    |       |   PrintFontManager::Substitute()
    |       |       |
    |       |       v
    |       |   fontconfig FcFontMatch()
    |       |       -> returns best available match OR empty
    |       |
    |       +-- If useless match (same name, same attrs): return false
    |       +-- If good substitute found: cache it, update rFSD, return true
    |       +-- If empty result: return false
    |
    +-- 5. Font attribute-based matching (serif/sans/weight/width scoring)
    |
    +-- 6. ImplFindFontFamilyOfDefaultFont() (last resort)
```

### Proposed WASM Font Resolution Flow

```
FcPreMatchSubstitution::FindFontSubstitute(rFSD)
    |
    +-- Check MRU cache -> miss
    |
    +-- GetFcSubstitute() -> PrintFontManager::Substitute()
    |       -> fontconfig returns empty OR useless substitute
    |
    +-- ** NEW: WASM intercept point **
    |   if (result empty/useless AND running on Emscripten):
    |       |
    |       v
    |   WasmFontResolver::RequestFont(fontName)
    |       |
    |       +-- Call JS: EM_ASM / emscripten_async_call / EM_JS
    |       |       -> Module.fontProvider.requestFont("Calibri")
    |       |
    |       +-- JS fetches font data (async)
    |       |       -> Returns ArrayBuffer of .ttf/.otf data
    |       |
    |       +-- Write to Emscripten virtual FS
    |       |       -> /tmp/fonts/Calibri.ttf
    |       |
    |       +-- Register with fontconfig
    |       |       -> PrintFontManager::addFontconfigFile(path)
    |       |       -> FcConfigAppFontAddFile()
    |       |
    |       +-- Register with PrintFontManager
    |       |       -> PrintFontManager::addFontFile(fileURL)
    |       |       -> Analyzes font, assigns fontID, stores metadata
    |       |
    |       +-- Register with FreetypeManager
    |       |       -> FreetypeManager::AddFontFile()
    |       |       -> FreetypeManager::AnnounceFonts(pFontCollection)
    |       |
    |       +-- Invalidate FcPreMatchSubstitution cache
    |       |       -> Clear maCachedFontMap (or targeted eviction)
    |       |
    |       +-- Retry: GetFcSubstitute() again
    |               -> fontconfig now finds the newly registered font
    |               -> Return substituted pattern
    |
    +-- Cache result, update rFSD, return true
```

### Key Observation: AddTempDevFont Already Does Most of the Registration

`FreeTypeTextRenderImpl::AddTempDevFont()` at line 96-129 of `freetypetextrender.cxx` already implements the full font registration pipeline:

```
AddTempDevFont(pFontCollection, rFileURL, rFontName)
    1. PrintFontManager::addFontFile(rFileURL)
       - addFontconfigFile() -> FcConfigAppFontAddFile()
       - analyzeFontFile() -> parses TrueType, extracts metadata
       - Stores in m_aFonts map with new fontID
    2. For each fontID:
       - FreetypeManager::AddFontFile(fileName, faceNum, variantNum, fontId, attrs)
    3. FreetypeManager::AnnounceFonts(pFontCollection)
       - Pushes all fonts into PhysicalFontCollection::Add()
```

This means the registration side of the problem is already solved. The new work is:
1. Detecting when font resolution fails (hook point)
2. Calling out to JS to get font data (bridge)
3. Writing font data to virtual FS (bridge)
4. Calling the existing registration path (already exists)
5. Retrying the substitution (hook point)

## Recommended Project Structure

```
vcl/
├── unx/generic/fontmanager/
│   ├── fontsubst.cxx              # MODIFY: Add WASM intercept in FindFontSubstitute
│   ├── fontmanager.cxx            # EXISTING: PrintFontManager (no changes expected)
│   └── fontconfig.cxx             # EXISTING: fontconfig wrappers (no changes expected)
├── unx/generic/gdi/
│   └── freetypetextrender.cxx     # EXISTING: AddTempDevFont (use as reference/utility)
├── headless/
│   ├── svpinst.cxx                # EXISTING: SvpSalInstance, Emscripten-specific code
│   └── svptext.cxx                # EXISTING: delegates to FreeTypeTextRenderImpl
├── source/font/
│   └── PhysicalFontCollection.cxx # EXISTING: orchestrator (no changes needed)
└── wasm/                          # NEW: WASM-specific font resolution
    ├── WasmFontResolver.cxx       # C++ side: detect failure, call JS, register font
    ├── WasmFontResolver.hxx       # Interface for the resolver
    └── fontprovider.js            # JS side: fetch font data, return to C++
```

### Structure Rationale

- **`vcl/wasm/`:** Isolates WASM-specific code from the generic Unix font path, keeping `#ifdef EMSCRIPTEN` blocks minimal in shared code.
- **Modification to `fontsubst.cxx`:** The smallest possible change -- a conditional call to the WasmFontResolver when fontconfig substitution fails. This is the only file in the existing font subsystem that needs modification.
- **No changes to `PhysicalFontCollection.cxx`:** The font collection is the consumer, not the provider. It already handles the hooks correctly.
- **No changes to `fontmanager.cxx`/`fontconfig.cxx`:** The existing `addFontFile()` and `addFontconfigFile()` APIs are sufficient for runtime registration.

## Architectural Patterns

### Pattern 1: Intercept-Register-Retry in the Substitution Hook

**What:** Modify `FcPreMatchSubstitution::FindFontSubstitute()` to detect substitution failure and trigger font fetching before returning false.

**When to use:** When fontconfig cannot find a match for the requested font AND the WASM font provider has a font available.

**Trade-offs:**
- Pro: Minimal changes to existing code (one `#ifdef EMSCRIPTEN` block in one function)
- Pro: Transparent to all callers -- PhysicalFontCollection never knows about WASM
- Con: Synchronous JS call required in the substitution path (see Pattern 2 for async alternative)

**Key code location:**

In `vcl/unx/generic/fontmanager/fontsubst.cxx`, lines 100-172, `FcPreMatchSubstitution::FindFontSubstitute()`:

```cpp
// After line 135: const bool bHaveSubstitute = !uselessmatch( rFontSelData, aOut );
// INSERT WASM intercept:
#ifdef EMSCRIPTEN
    if (!bHaveSubstitute || aOut.maSearchName.isEmpty())
    {
        // Attempt WASM font resolution
        if (WasmFontResolver::get().tryResolveFont(rFontSelData.maTargetName))
        {
            // Font was fetched and registered -- retry fontconfig query
            const vcl::font::FontSelectPattern aRetry = GetFcSubstitute(rFontSelData, aDummy);
            if (!aRetry.maSearchName.isEmpty() && !uselessmatch(rFontSelData, aRetry))
            {
                rCachedFontMap.push_front(value_type(rFontSelData, aRetry));
                if (rCachedFontMap.size() > 256)
                    rCachedFontMap.pop_back();
                rFontSelData = aRetry;
                return true;
            }
        }
    }
#endif
```

### Pattern 2: Synchronous JS Call via Emscripten

**What:** Use `EM_ASM_INT` / `EM_JS` for synchronous C++ -> JS -> C++ font data transfer. Emscripten's virtual FS (`FS.writeFile`) is used to persist font data before returning to C++.

**When to use:** During the font substitution hook, which is called synchronously from the rendering pipeline and cannot be deferred.

**Trade-offs:**
- Pro: No need to restructure the synchronous VCL font resolution pipeline
- Pro: Font is immediately available for the current rendering pass
- Con: Blocks the main thread during font fetch (acceptable for WASM single-threaded model)
- Con: Requires font data to be pre-loaded or cached in JS (network fetch would block too long)

**Critical interface:**

```cpp
// C++ side (WasmFontResolver.cxx)
bool WasmFontResolver::tryResolveFont(const OUString& rFontName)
{
    OString aUtf8Name = OUStringToOString(rFontName, RTL_TEXTENCODING_UTF8);

    // Call JS to check if font is available and write to virtual FS
    int result = EM_ASM_INT({
        var fontName = UTF8ToString($0);
        var fontData = Module.fontProvider.getFont(fontName);  // sync
        if (!fontData) return 0;
        var path = '/tmp/fonts/' + fontName.replace(/\s/g, '') + '.ttf';
        FS.writeFile(path, new Uint8Array(fontData));
        return stringToNewUTF8(path);
    }, aUtf8Name.getStr());

    if (result == 0) return false;

    // Register the font file with the LO font subsystem
    OUString aFileURL = /* convert /tmp/fonts/... path to file:// URL */;
    return registerFontFile(aFileURL, rFontName);
}
```

```javascript
// JS side (fontprovider.js)
Module.fontProvider = {
    fontCache: new Map(),  // fontName -> ArrayBuffer

    // Called synchronously from C++ via EM_ASM
    getFont: function(fontName) {
        return this.fontCache.get(fontName) || null;
    },

    // Called from JS application code to pre-load fonts
    preloadFont: async function(fontName, url) {
        const response = await fetch(url);
        const buffer = await response.arrayBuffer();
        this.fontCache.set(fontName, buffer);
    }
};
```

### Pattern 3: Registration via Existing AddTempDevFont Path

**What:** Reuse the existing `FreeTypeTextRenderImpl::AddTempDevFont()` pipeline for the actual font registration, rather than reimplementing the three-step registration (fontconfig + PrintFontManager + FreetypeManager).

**When to use:** After font data has been written to the virtual FS.

**Trade-offs:**
- Pro: Zero duplication of registration logic
- Pro: Handles TTC files, font variations, quality scoring automatically
- Con: Requires access to a `PhysicalFontCollection*` pointer at the hook call site
- Alternative: Call the three registration steps directly (PrintFontManager::addFontFile, FreetypeManager::AddFontFile, FreetypeManager::AnnounceFonts) if the collection pointer is not readily available.

**Implementation note:** The `FcPreMatchSubstitution::FindFontSubstitute()` does not have direct access to a `PhysicalFontCollection*`. However, `SalGenericInstance::RegisterFontSubstitutors()` passes the collection pointer when setting up the hooks. The resolver can either:
1. Store a reference to the collection during hook setup, OR
2. Call `PrintFontManager::addFontFile()` + `FreetypeManager` operations directly (these are singletons accessible via `PrintFontManager::get()` and `FreetypeManager::get()`)

Option 2 is simpler and sufficient.

## Data Flow: Font Registration Pipeline (Existing)

Understanding this existing pipeline is critical for the WASM integration:

```
Font file on disk (e.g., /tmp/fonts/Calibri.ttf)
    |
    v
PrintFontManager::addFontFile(fileURL)                    [fontmanager.cxx:141]
    |
    +-- Parse URL to get dir + filename
    +-- getDirectoryAtom(dir) -> assigns/retrieves dir ID
    +-- findFontFileIDs(dirID, name) -> check if already known
    +-- addFontconfigFile(fullPath)                        [fontconfig.cxx:785]
    |       -> FcConfigAppFontAddFile() -- tells fontconfig about the font
    |       -> FontCfgWrapper::addFontSet(FcSetApplication)
    +-- analyzeFontFile(dirID, name)                       [fontmanager.cxx:186]
    |       -> OpenTTFontFile() -- parse TrueType tables
    |       -> Extract family name, weight, slant, width, pitch
    |       -> Return vector<PrintFont>
    +-- For each PrintFont:
    |       -> Assign fontID (m_nNextFontID++)
    |       -> Store in m_aFonts[fontID]
    |       -> Store in m_aFontFileToFontID[fileName]
    +-- Return vector<fontID>
    |
    v
FreetypeManager::AddFontFile(filePath, faceNum, variantNum, fontId, attrs)
                                                           [freetype_glyphcache.cxx:281]
    |
    +-- Create FreetypeFontInfo with font attributes and file reference
    +-- Store in m_aFontInfoList[fontId]
    |
    v
FreetypeManager::AnnounceFonts(pFontCollection)           [freetype_glyphcache.cxx:302]
    |
    +-- For each FreetypeFontInfo:
    |       -> Create FreetypeFontFace
    |       -> pFontCollection->Add(pFontFace)
    |               -> Normalizes name, creates/finds PhysicalFontFamily
    |               -> PhysicalFontFamily::AddFontFace()
    |
    v
Font is now in PhysicalFontCollection::maPhysicalFontFamilies
    -> Future FindFontFamily() calls will find it
    -> Future fontconfig queries will also find it (via FcConfigAppFontAddFile)
```

## Critical Interfaces Between C++ and JS

### Interface 1: Font Request (C++ -> JS)

| Aspect | Detail |
|--------|--------|
| **Direction** | C++ calls JS |
| **Mechanism** | `EM_ASM_INT` or `EM_JS` macro |
| **Input** | Font family name (UTF-8 string) |
| **Output** | Integer: 0 = not available, nonzero = pointer to path string |
| **Timing** | Synchronous (blocking) |
| **Thread** | Main thread only (Emscripten single-threaded or main thread for WASM) |

### Interface 2: Font Data Delivery (JS -> Virtual FS)

| Aspect | Detail |
|--------|--------|
| **Direction** | JS writes to Emscripten virtual FS |
| **Mechanism** | `FS.writeFile(path, new Uint8Array(buffer))` |
| **Input** | Font data as ArrayBuffer (from JS fetch, IndexedDB, or preload cache) |
| **Output** | File at a known path (e.g., `/tmp/fonts/<name>.ttf`) |
| **Timing** | Synchronous (FS.writeFile is sync in Emscripten) |
| **Format** | TrueType (.ttf), OpenType (.otf), or TrueType Collection (.ttc) |

### Interface 3: Font Preloading (JS application -> JS font provider)

| Aspect | Detail |
|--------|--------|
| **Direction** | Application JS calls font provider |
| **Mechanism** | `Module.fontProvider.preloadFont(name, url)` |
| **Input** | Font name + URL |
| **Output** | Promise (async fetch + cache) |
| **Timing** | Asynchronous -- called before document load |
| **Purpose** | Pre-populate the font cache so synchronous C++ requests can be served instantly |

### Interface 4: Cache Invalidation (Internal C++)

| Aspect | Detail |
|--------|--------|
| **What** | FcPreMatchSubstitution's maCachedFontMap must be invalidated after new font registration |
| **Why** | Cached "no substitute found" results become stale when new fonts are added |
| **How** | Clear the cache list, or add a targeted eviction for the font name |
| **Where** | In `fontsubst.cxx`, the cache is `mutable CachedFontMapType maCachedFontMap` |

## Anti-Patterns

### Anti-Pattern 1: Modifying PhysicalFontCollection::FindFontFamily

**What people might do:** Add the WASM font resolution hook inside `PhysicalFontCollection::FindFontFamily()` at the top level.

**Why it's wrong:** `FindFontFamily()` is a 300+ line method with complex multi-stage fallback logic. It calls `mpPreMatchHook->FindFontSubstitute()` at specific points in its chain. Adding WASM logic here would entangle platform-specific code with the generic font resolution algorithm, making it hard to maintain and test.

**Do this instead:** Hook into `FcPreMatchSubstitution::FindFontSubstitute()` which is already platform-specific (Unix/fontconfig) and is the natural place for "I need to find this font" logic.

### Anti-Pattern 2: Async Font Loading with Deferred Rendering

**What people might do:** Make font loading asynchronous and defer document rendering until fonts arrive.

**Why it's wrong:** The VCL rendering pipeline is deeply synchronous. `FindFontFamily()` is called during layout, which is called during paint, which must complete before the frame is displayed. Deferring would require restructuring the entire rendering pipeline.

**Do this instead:** Pre-load fonts in JS before document open, and serve them synchronously from an in-memory cache during C++ font resolution. The synchronous `EM_ASM` call is fast when data is already in the JS cache.

### Anti-Pattern 3: Bypassing fontconfig Registration

**What people might do:** Register fonts only with FreetypeManager and PhysicalFontCollection, skipping fontconfig.

**Why it's wrong:** The substitution hook calls `PrintFontManager::Substitute()` which calls `FcFontMatch()`. If the font is not registered with fontconfig, the retry after registration will still fail. Additionally, `FcGlyphFallbackSubstitution` also queries fontconfig for missing glyphs.

**Do this instead:** Always call `PrintFontManager::addFontconfigFile()` (which calls `FcConfigAppFontAddFile()`) before calling `addFontFile()`. This ensures fontconfig, PrintFontManager, and FreetypeManager all know about the font.

### Anti-Pattern 4: Creating a New Substitution Hook Class

**What people might do:** Create a `WasmPreMatchSubstitution` class that replaces `FcPreMatchSubstitution`.

**Why it's wrong:** The existing `FcPreMatchSubstitution` is instantiated as a static object in `RegisterFontSubstitutors()` and works correctly for all fonts that ARE available. Replacing it would mean reimplementing all the fontconfig query logic.

**Do this instead:** Extend `FcPreMatchSubstitution::FindFontSubstitute()` with a WASM-specific fallback path that only activates when the fontconfig query returns no result. This is an additive change, not a replacement.

## Build Order (Suggested Implementation Phases)

### Phase 1: JS Font Provider Infrastructure

**Build:** The JavaScript module that manages font data.

**Dependencies:** None (pure JS, no C++ changes).

**Deliverables:**
- `Module.fontProvider` object with `getFont(name)` and `preloadFont(name, url)` methods
- In-memory Map cache for font ArrayBuffers
- Test: preload a font, verify `getFont()` returns data

**Why first:** This can be developed and tested entirely in the browser without touching C++ code.

### Phase 2: C++ -> JS Bridge (WasmFontResolver)

**Build:** The C++ class that calls into JS and writes to the virtual FS.

**Dependencies:** Phase 1 (JS provider must exist to test against).

**Deliverables:**
- `WasmFontResolver` singleton class
- `tryResolveFont(fontName)` method using `EM_ASM_INT`
- Write font data to Emscripten virtual FS
- Test: call from C++, verify file appears in virtual FS

**Why second:** Establishes the cross-language bridge without yet modifying the font subsystem.

### Phase 3: Font Registration Integration

**Build:** Connect `WasmFontResolver` to the existing font registration pipeline.

**Dependencies:** Phase 2 (resolver must produce files in virtual FS). Existing `PrintFontManager::addFontFile()` and `FreetypeManager::AddFontFile()` APIs.

**Deliverables:**
- `WasmFontResolver::registerFontFile(fileURL, fontName)` method
- Calls `PrintFontManager::addFontFile()` -> `FreetypeManager::AddFontFile()` -> `FreetypeManager::AnnounceFonts()`
- Test: register a font file, verify it appears in `PhysicalFontCollection`

**Why third:** This phase only uses existing APIs; the risk is in getting the right sequence of calls, not in modifying existing code.

### Phase 4: Hook Integration in fontsubst.cxx

**Build:** Modify `FcPreMatchSubstitution::FindFontSubstitute()` to call `WasmFontResolver` on failure.

**Dependencies:** Phase 3 (full registration pipeline must work).

**Deliverables:**
- `#ifdef EMSCRIPTEN` block in `FindFontSubstitute()` after the fontconfig query returns empty/useless
- Cache invalidation after successful font registration
- Retry logic (re-call `GetFcSubstitute()`)
- Test: open document requesting "Calibri", verify it resolves via WASM fetch + registration

**Why last:** This is the riskiest change because it modifies the hot path of font resolution. All other pieces must be working and tested before integrating here.

### Optional Phase 5: FcGlyphFallbackSubstitution Hook

**Build:** Same pattern as Phase 4 but for `FcGlyphFallbackSubstitution::FindFontSubstitute()`.

**Dependencies:** Phase 4.

**Deliverables:**
- Glyph-level fallback support: when a font is installed but missing specific glyphs, fetch a supplementary font
- This is a stretch goal; the pre-match hook (Phase 4) handles the majority of use cases

## Key Design Decisions

### Decision 1: Where to Hook

**Choice:** `FcPreMatchSubstitution::FindFontSubstitute()` in `fontsubst.cxx`

**Rationale:** This is the last C++ code called before fontconfig gives up on finding a font. It has access to the font name being requested, it already has a caching layer (which we need to invalidate), and it is already platform-specific code (Unix/fontconfig path) so adding an `#ifdef EMSCRIPTEN` is architecturally consistent.

**Alternative considered:** Hooking in `PhysicalFontCollection::FindFontFamily()` -- rejected because it would put WASM-specific code into platform-generic code.

### Decision 2: Sync vs Async Font Loading

**Choice:** Synchronous with pre-loaded cache

**Rationale:** The VCL font resolution pipeline is synchronous. The font substitution hook is called during layout/paint. Making this async would require restructuring the entire rendering pipeline. Instead, fonts should be pre-loaded into a JS Map cache before documents are opened, and the synchronous `EM_ASM` call simply reads from this cache.

**Implication:** The JS application must know which fonts a document needs before opening it (or must preload a standard set of common fonts). A "font loading" progress indicator in the JS UI would handle this.

### Decision 3: PhysicalFontCollection Access

**Choice:** Use singletons (`PrintFontManager::get()`, `FreetypeManager::get()`) rather than threading a `PhysicalFontCollection*` through the hook.

**Rationale:** The substitution hook does not have a direct pointer to the `PhysicalFontCollection`. However, `PrintFontManager` and `FreetypeManager` are singletons. The newly registered font will be picked up by fontconfig on the retry query via `PrintFontManager::Substitute()`, so we do not strictly need to call `AnnounceFonts()` on the collection during the hook. The font will be announced to the collection on the next `GetDevFontList()` call. For immediate availability, we can store the collection pointer during `RegisterFontSubstitutors()`.

## Sources

All findings derived from direct inspection of the LibreOffice source tree at:

- `vcl/unx/generic/fontmanager/fontsubst.cxx` -- FcPreMatchSubstitution, FcGlyphFallbackSubstitution, RegisterFontSubstitutors
- `vcl/unx/generic/gdi/freetypetextrender.cxx` -- FreeTypeTextRenderImpl::AddTempDevFont, GetDevFontList
- `vcl/unx/generic/fontmanager/fontmanager.cxx` -- PrintFontManager::addFontFile, initialize, analyzeFontFile
- `vcl/unx/generic/fontmanager/fontconfig.cxx` -- PrintFontManager::Substitute, addFontconfigFile, countFontconfigFonts
- `vcl/source/font/PhysicalFontCollection.cxx` -- FindFontFamily (both overloads), GetGlyphFallbackFont, SetPreMatchHook
- `vcl/headless/svptext.cxx` -- SvpSalGraphics delegation to FreeTypeTextRenderImpl
- `vcl/headless/svpinst.cxx` -- SvpSalInstance Emscripten-specific code
- `vcl/inc/font/fontsubstitution.hxx` -- PreMatchFontSubstitution, GlyphFallbackFontSubstitution interfaces
- `vcl/inc/unx/fontmanager.hxx` -- PrintFontManager class definition
- `vcl/inc/font/PhysicalFontCollection.hxx` -- PhysicalFontCollection class definition
- `vcl/inc/unx/geninst.h` -- SalGenericInstance, RegisterFontSubstitutors
- `vcl/inc/unx/freetype_glyphcache.hxx` -- FreetypeManager, FreetypeFontInfo, FreetypeFontFile
- `vcl/unx/generic/glyphs/freetype_glyphcache.cxx` -- FreetypeManager::AddFontFile, AnnounceFonts, RemoveFontFile

---
*Architecture research for: LibreOffice WASM Runtime Font Resolution*
*Researched: 2026-02-09*
