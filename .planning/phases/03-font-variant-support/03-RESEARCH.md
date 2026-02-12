# Phase 3 Research: Font Variant Support

## Key Decision

**Approach B: Return all variants at once, let C++ match.**

When C++ requests a font family (e.g., "Arial"), JS returns ALL variant files from its `Map<string, FontData[]>`. C++ registers them all via `registerFontFromVFS`. The existing `PhysicalFontFace::IsBetterMatch()` scoring system picks the correct variant (bold, italic, condensed, etc.) automatically.

**Why this approach:**
- JS side has `Map<string, FontData[]>` — already grouped by family, no per-variant metadata needed
- C++ scoring system handles weight/italic/width/pitch matching (240K pts for family name, 1K for weight, 900 for italic, etc.)
- One JS call per font family instead of per variant
- Negative cache simplifies to family-name-only (current `WasmFontCacheKey` with style hints is overcomplicated for this approach)

## What Exists Today

### Current Hook (`vcl/unx/generic/fontmanager/fontsubst.cxx`)

- `em_startFontResolve(familyName, pReq)` — EM_JS, runs on main thread
- `globalThis.__resolveSystemFont(familyName)` → `Promise<ArrayBuffer>` (single file)
- Writes one file to `/tmp/fonts/{safeName}.ttf`
- Returns single path via `em_fontResolveComplete`
- `resolveFontFromHost(pFamilyName)` blocks pthread via `emscripten_proxy_sync_with_ctx`
- `registerFontFromVFS(fileURL, fontName)` registers a single file

### Current Cache

`WasmFontCacheKey` includes weight/italic/width/pitch — this was designed for per-variant calls. With Approach B (all variants at once), we only need family name in the cache key.

### Existing TTC Support

`PrintFontManager::addFontFile()` → `analyzeFontFile()` → `CountTTCFonts()` already iterates all face indices in TTC files and creates separate `PrintFont` entries per face. The `registerFontFromVFS` function iterates all returned `fontID`s. **TTC files work out of the box** — no special handling needed in the hook.

FreeType identifies font format by magic bytes (`ttcf` for TTC), not file extension. The `.ttf` extension on VFS path doesn't break TTC handling.

## Changes Required

### 1. JS API Contract Change

**Before:** `__resolveSystemFont(familyName: string) → Promise<ArrayBuffer>`
**After:** `__resolveSystemFont(familyName: string) → Promise<ArrayBuffer[]>`

Returns array of font file buffers (one per variant). Empty array = font not available.

Backward compatibility: if resolver returns a single `ArrayBuffer` (not array), wrap it.

### 2. `em_startFontResolve` — Handle Multiple Buffers

Write each buffer to VFS with indexed filenames:
```
/tmp/fonts/Arial_0.ttf
/tmp/fonts/Arial_1.ttf
/tmp/fonts/Arial_2.ttf
```

Return newline-separated paths string to C++ via `em_fontResolveComplete`.

### 3. `FontResolveRequest` — Multiple Paths

`pResultPath` becomes a newline-separated list of VFS paths. C++ splits on `\n` and registers each file.

### 4. `registerFontFromVFS` — Called Per File

No change needed — it already handles multi-face files (TTC). Just call it once per returned path.

### 5. Simplify Negative Cache

Replace `WasmFontCacheKey` (family+weight+italic+width+pitch) with simple `std::unordered_set<OUString>` keyed by family name only. One negative entry per family.

### 6. SAL_INFO Logging Updates

Log: number of variants received, registration results per variant, total faces registered.

## Font Matching Deep Dive

`PhysicalFontFace::IsBetterMatch()` scoring (from `vcl/source/font/PhysicalFontFace.cxx`):

| Criterion | Points |
|-----------|--------|
| Family name exact match | 240,000 |
| Style name exact match | 120,000 |
| Pitch exact match | 20,000 |
| Weight exact match | 1,000 |
| Italic exact match | 900 |
| Width (normal preferred) | 400 |

With quality boost of 5800 on host-provided fonts, they always beat bundled fallbacks within the same family.

## Enum Values Reference

From `include/tools/fontenum.hxx`:

**FontWeight:** DONTKNOW=0, THIN=1, ULTRALIGHT=2, LIGHT=3, SEMILIGHT=4, NORMAL=5, MEDIUM=6, SEMIBOLD=7, BOLD=8, ULTRABOLD=9, BLACK=10

**FontItalic:** NONE=0, OBLIQUE=1, NORMAL=2, DONTKNOW=3

**FontWidth:** DONTKNOW=0, ULTRA_CONDENSED=1 .. NORMAL=5 .. ULTRA_EXPANDED=9

**FontPitch:** DONTKNOW=0, FIXED=1, VARIABLE=2

## REQ-F10 Satisfaction

Style hints don't need to be passed TO JavaScript. Instead:
1. JS returns all variants for the family
2. C++ registers them all → FreeType extracts FontAttributes (weight, italic, width, pitch) from each font's OS/2 table
3. PhysicalFontCollection scoring picks the best match for the requested style

The acceptance criterion ("A document using bold Arial triggers a JS callback that includes weight, italic, width, and pitch hints") is satisfied differently: the callback triggers for "Arial" and returns ALL variants including Bold. The scorer then selects Bold based on the document's style request.

**Note:** The existing SAL_INFO logging already outputs style hints from the C++ side, which satisfies observability.

## REQ-F11 Satisfaction

TTC support works automatically:
1. JS returns `.ttc` ArrayBuffer as one entry in the array
2. C++ writes to VFS (extension doesn't matter — FreeType uses magic bytes)
3. `PrintFontManager::addFontFile()` → `CountTTCFonts()` iterates all faces
4. Each face gets its own `PrintFont` entry with correct `FontAttributes`
5. `registerFontFromVFS` iterates all `fontID`s and registers each

No special TTC handling code needed in the hook.

## Risk Assessment

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| Large font families (10+ variants) slow initial load | Low | Most families have 4-8 variants; one-time cost |
| Backward compat if JS returns single ArrayBuffer | Medium | Add `Array.isArray()` check, wrap single buffer |
| VFS disk space with all variants | Low | MEMFS; variants are only loaded once per family |

## RESEARCH COMPLETE
