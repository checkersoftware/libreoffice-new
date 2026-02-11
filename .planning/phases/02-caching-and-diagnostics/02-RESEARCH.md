# Phase 2: Caching and Diagnostics - Research

**Researched:** 2026-02-11
**Domain:** C++ in-memory caching for WASM font resolution, SAL_LOG diagnostic integration
**Confidence:** HIGH

## Summary

Phase 2 formalizes the existing `s_aTriedFonts` set into a proper two-state cache (positive/negative), adds a re-registration guard, and instruments the resolution flow with SAL_LOG diagnostics. All changes are confined to a single file (`vcl/unx/generic/fontmanager/fontsubst.cxx`) within the existing `#ifdef EMSCRIPTEN` block. No new files, no new dependencies, no changes to the JS callback contract.

The current implementation already has the core data flow: `FindFontSubstitute()` checks `s_aTriedFonts` before calling `resolveFontFromHost()`, and the result is either a VFS path (positive) or nullptr (negative). Phase 2 replaces the simple `std::set<OUString> s_aTriedFonts` with a `std::unordered_map` keyed by a composite of family name + style hints (weight, italic, width, pitch), mapping to an enum value of `{Positive, Negative}`. The positive path also gains an explicit guard: before calling JS, check if `PhysicalFontCollection::FindFontFamily()` already has the font (which it will after a previous successful registration). Logging uses existing `SAL_INFO("vcl.fonts", ...)` pattern throughout.

The technical complexity is low. The cache data structure, cache key, lookup/insert logic, and SAL_LOG calls are all straightforward C++ with patterns already established in the file. The main design consideration is the cache key: the user decided on family name + style hints (weight, italic, width, pitch), which aligns with the `uselessmatch()` comparator already in this file and prepares for Phase 3 font variant support.

**Primary recommendation:** Replace `static std::set<OUString> s_aTriedFonts` with `static std::unordered_map<WasmFontCacheKey, WasmFontCacheResult>` using a struct key with family name + weight + italic + width + pitch. Add SAL_INFO logging at 5 points in the resolution flow. All changes in `fontsubst.cxx` behind `#ifdef EMSCRIPTEN`.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

#### Cache behavior
- Session-only in-memory cache -- cleared on page reload, no persistence (IndexedDB, etc.)
- Negative cache: once per session -- if JS says "not available", never ask again until reload
- Positive cache: flag-based -- track "we already resolved this" to skip JS call, don't track VFS path
- Cache key: family name + style hints (weight, italic, width, pitch) -- ready for Phase 3 variant support from day one
- If AddTempDevFont fails (corrupt file, bad format), cache as negative -- don't retry

#### Logging design
- Key events only, not full trace -- ~5 log lines per resolution: font requested, cache hit/miss, JS called, result (path or empty), registration success/fail
- No timing information in logs
- C++ side only (SAL_LOG) -- no JS-side console.log for font resolution
- Log category: Claude's discretion (existing vcl.fonts or new subcategory)

#### Guard & edge cases
- No deduplication of concurrent requests -- let all calls through independently (WASM is effectively single-threaded via JSPI suspend)
- Explicit guard before AddTempDevFont -- check cache before calling, never register the same font twice
- VFS file disappearance: ignore -- not a realistic scenario, don't add complexity

#### Host-side contract
- Contract unchanged from Phase 1: C++ asks for font by name, JS responds with binary data
- One-way communication only -- C++ never exposes cache state to JS
- JS callback signature stays the same -- no new parameters or return metadata
- JS side caches `queryLocalFonts()` result (font enumeration) -- called lazily on first font request, not eagerly on startup
- Font binary data not cached separately -- once written to VFS, it persists for the session

### Claude's Discretion
- SAL_LOG category choice (vcl.fonts vs vcl.fonts.wasm or similar)
- Exact log message format and wording
- Internal cache data structure (std::set, std::unordered_map, etc.)
- Cache key format/hashing for family+hints composite key

### Deferred Ideas (OUT OF SCOPE)
None -- discussion stayed within phase scope
</user_constraints>

## Standard Stack

### Core

| Component | Location | Purpose | Why Standard | Confidence |
|-----------|----------|---------|--------------|------------|
| `std::unordered_map` | C++ STL | Cache storage (key -> positive/negative) | O(1) average lookup, already used in fontmanager.cxx and fontconfig.cxx in the same directory | HIGH |
| `SAL_INFO("vcl.fonts", ...)` | `include/sal/log.hxx` | Diagnostic logging | Already used at 11+ call sites in `vcl/unx/generic/fontmanager/` for font operations. Filtered via `SAL_LOG=+INFO.vcl.fonts` | HIGH |
| `SAL_WARN("vcl.fonts", ...)` | `include/sal/log.hxx` | Error-level logging | Already used for registration failures. `SAL_WARN` is always enabled by default. | HIGH |
| `PhysicalFontCollection::FindFontFamily()` | `vcl/source/font/PhysicalFontCollection.cxx` | Re-registration guard check | Already called at line 275 of fontsubst.cxx for the WASM block's entry condition | HIGH |

### Supporting

| Component | Location | Purpose | When to Use | Confidence |
|-----------|----------|---------|-------------|------------|
| `FontWeight` enum | `include/tools/fontenum.hxx` | Cache key component | Values: WEIGHT_DONTKNOW through WEIGHT_BLACK (11 values) | HIGH |
| `FontItalic` enum | `include/tools/fontenum.hxx` | Cache key component | Values: ITALIC_NONE, ITALIC_OBLIQUE, ITALIC_NORMAL, ITALIC_DONTKNOW | HIGH |
| `FontWidth` enum | `include/tools/fontenum.hxx` | Cache key component | Values: WIDTH_DONTKNOW through WIDTH_ULTRA_EXPANDED (10 values) | HIGH |
| `FontPitch` enum | `include/tools/fontenum.hxx` | Cache key component | Values: PITCH_DONTKNOW, PITCH_FIXED, PITCH_VARIABLE | HIGH |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| `std::unordered_map` with custom hash | `std::map` with `operator<` | `std::map` is simpler (no custom hash needed) but O(log n) vs O(1). For a small cache (<100 entries typical), the difference is negligible. `std::unordered_map` recommended for alignment with existing codebase patterns in this directory. |
| Struct key with custom hash | `OUString` key with encoded hints | Simpler key but loses type safety. Composite key is cleaner for Phase 3 extension. |

## Architecture Patterns

### Recommended Changes (Single File)

```
vcl/unx/generic/fontmanager/
  fontsubst.cxx              # MODIFY: Replace s_aTriedFonts with WasmFontCache,
                             #         add SAL_INFO logging at 5 points,
                             #         add re-registration guard
```

No new files. No header changes. All changes within the existing `#ifdef EMSCRIPTEN` block.

### Pattern 1: Cache Key Struct with Custom Hash

**What:** A struct containing `OUString` family name + four enum fields (weight, italic, width, pitch), with `operator==` and a custom `std::hash` specialization. This is the cache lookup key.

**When to use:** Every time `FindFontSubstitute()` enters the WASM block and needs to check whether a font+style combination has already been resolved.

**Design rationale:** The user decided the cache key should include style hints for Phase 3 readiness. The `uselessmatch()` function at line 214 already uses exactly these four attributes (weight, italic, pitch, width) to determine whether a fontconfig substitution is meaningful. Using the same set for the WASM cache key ensures consistency.

**Example:**
```cpp
// Inside #ifdef EMSCRIPTEN block in fontsubst.cxx

enum class WasmFontCacheResult { Positive, Negative };

struct WasmFontCacheKey
{
    OUString maFamilyName;
    FontWeight meWeight;
    FontItalic meItalic;
    FontWidth meWidthType;
    FontPitch mePitch;

    bool operator==(const WasmFontCacheKey& rOther) const
    {
        return maFamilyName == rOther.maFamilyName
            && meWeight == rOther.meWeight
            && meItalic == rOther.meItalic
            && meWidthType == rOther.meWidthType
            && mePitch == rOther.mePitch;
    }
};

struct WasmFontCacheKeyHash
{
    size_t operator()(const WasmFontCacheKey& rKey) const
    {
        size_t nHash = rKey.maFamilyName.hashCode();
        nHash ^= std::hash<int>{}(rKey.meWeight) + 0x9e3779b9
                 + (nHash << 6) + (nHash >> 2);
        nHash ^= std::hash<int>{}(rKey.meItalic) + 0x9e3779b9
                 + (nHash << 6) + (nHash >> 2);
        nHash ^= std::hash<int>{}(rKey.meWidthType) + 0x9e3779b9
                 + (nHash << 6) + (nHash >> 2);
        nHash ^= std::hash<int>{}(rKey.mePitch) + 0x9e3779b9
                 + (nHash << 6) + (nHash >> 2);
        return nHash;
    }
};

static std::unordered_map<WasmFontCacheKey, WasmFontCacheResult,
                          WasmFontCacheKeyHash> s_aWasmFontCache;
```

**Source:** Hash combine pattern from Boost (`boost::hash_combine`), using the golden ratio constant `0x9e3779b9`. This is the standard approach for combining multiple hash values in C++. The `OUString::hashCode()` method is LibreOffice's built-in string hash.

### Pattern 2: Cache Key Construction from FontSelectPattern

**What:** Extract the cache key fields from `rFontSelData` (the `FontSelectPattern` passed to `FindFontSubstitute()`).

**Key insight:** `FontSelectPattern` inherits from `FontAttributes`, which provides `GetWeight()`, `GetItalic()`, `GetPitch()`, `GetWidthType()`. The target name comes from `maTargetName`. These are all available in the existing WASM block at line 275.

**Example:**
```cpp
WasmFontCacheKey aCacheKey{
    rFontSelData.maTargetName,
    rFontSelData.GetWeight(),
    rFontSelData.GetItalic(),
    rFontSelData.GetWidthType(),
    rFontSelData.GetPitch()
};
```

### Pattern 3: Three-Check Guard Sequence

**What:** The WASM resolution block should check three things in order:
1. Is the font already in the PhysicalFontCollection? (existing check)
2. Is it in the WASM cache as positive? (new -- skip JS call)
3. Is it in the WASM cache as negative? (new -- skip JS call)

Only if all three checks fail, call `resolveFontFromHost()`.

**Key insight:** The `FindFontFamily()` check (step 1) handles fonts registered by any path -- not just WASM resolution. The cache check (steps 2-3) specifically tracks WASM JS resolution results. Step 1 subsumes step 2 in most cases (if we registered a font, it is in the collection), but the cache check prevents even the `FindFontFamily()` overhead on subsequent calls. Per the user's decision, the positive cache is flag-based: track "we already resolved this" to skip the JS call, not the VFS path.

**Example flow:**
```cpp
#ifdef EMSCRIPTEN
    if (s_pFontCollection)
    {
        WasmFontCacheKey aCacheKey{ /* ... */ };
        auto itr = s_aWasmFontCache.find(aCacheKey);

        if (itr != s_aWasmFontCache.end())
        {
            if (itr->second == WasmFontCacheResult::Positive)
            {
                SAL_INFO("vcl.fonts", "WASM font cache: positive hit for \""
                         << rFontSelData.maTargetName << "\"");
                return false; // Font already registered, caller will find it
            }
            // Negative hit
            SAL_INFO("vcl.fonts", "WASM font cache: negative hit for \""
                     << rFontSelData.maTargetName << "\", skipping JS call");
            // Fall through to fontconfig substitution
        }
        else if (!s_pFontCollection->FindFontFamily(rFontSelData.maTargetName))
        {
            SAL_INFO("vcl.fonts", "WASM font resolution: requesting \""
                     << rFontSelData.maTargetName << "\" from host");
            // ... call resolveFontFromHost, register, cache result ...
        }
    }
#endif
```

### Pattern 4: SAL_INFO Logging at 5 Key Points

**What:** Log at exactly 5 points per resolution (per user decision):

1. **Font requested:** When entering the WASM block with a new font
2. **Cache hit/miss:** Whether the cache had a positive or negative entry, or miss
3. **JS called:** When `resolveFontFromHost()` is about to be called
4. **Result:** Whether JS returned a path (and what path) or empty
5. **Registration outcome:** Whether `registerFontFromVFS()` succeeded or failed

**Already partially implemented:** Lines 283-320 of the current `fontsubst.cxx` have SAL_INFO calls at points 1, 3 (partial), 4, and 5. Phase 2 adds cache hit/miss logging and refines the existing messages.

### Anti-Patterns to Avoid

- **Do NOT cache the VFS file path:** The user explicitly decided positive cache is flag-based. The VFS path is not needed after registration -- the font is in the PhysicalFontCollection.

- **Do NOT expose cache state to JavaScript:** User decided one-way communication only. C++ never tells JS about its cache.

- **Do NOT add timing/performance metrics:** User explicitly decided no timing information in logs.

- **Do NOT add IndexedDB or any persistent cache:** User decided session-only in-memory cache, cleared on page reload.

- **Do NOT deduplicate concurrent requests:** User decided WASM is single-threaded, no concurrency guard needed.

- **Do NOT use a subcategory like `vcl.fonts.wasm`:** See Discretion Recommendations below -- use existing `vcl.fonts` for consistency. (Recommendation, not constraint.)

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Hash combining | Manual XOR without mixing | Boost-style hash_combine with golden ratio constant | Proper bit mixing prevents collision patterns with correlated inputs |
| Font family presence check | Manual collection iteration | `PhysicalFontCollection::FindFontFamily()` | Already handles search name normalization, case folding |
| Log filtering | Custom log level checks | `SAL_INFO` / `SAL_WARN` macros | Compile-time optimizable, runtime filterable via `SAL_LOG` env var |

**Key insight:** The entire Phase 2 implementation is less than 50 lines of new code (cache struct + hash ~25 lines, cache lookup/insert ~15 lines, logging adjustments ~10 lines). The risk is not complexity but correctness of the cache key design for Phase 3 extensibility.

## Common Pitfalls

### Pitfall 1: Cache Key Must Use maTargetName, Not maSearchName

**What goes wrong:** Using `maSearchName` (the normalized/lowercased name) instead of `maTargetName` (the original font name from the document). The `maSearchName` is set by `GetEnglishSearchFontName()` which strips spaces and lowercases, so "Segoe UI" becomes "segoeui". But the JS resolver expects the original family name.
**Why it happens:** Both names are available on `rFontSelData`. It is easy to pick the wrong one.
**How to avoid:** Use `rFontSelData.maTargetName` for the cache key, consistent with the current code at line 275 (`FindFontFamily(rFontSelData.maTargetName)`) and line 279 (`s_aTriedFonts.insert(rFontSelData.maTargetName)`).
**Warning signs:** Cache misses for fonts that should hit, because the same font arrives with different `maSearchName` values but same `maTargetName`.

### Pitfall 2: Positive Cache Hit Must Still Return false

**What goes wrong:** On a positive cache hit, returning `true` (meaning "I found a substitute") instead of `false` (meaning "no substitute needed, the font is directly available").
**Why it happens:** The return value semantics of `FindFontSubstitute()` are counterintuitive. Returning `false` means "I did NOT substitute" -- which causes the caller (`FindFontFamily()` at line 1085 of `PhysicalFontCollection.cxx`) to call `ImplFindFontFamilyBySearchName()` and find the font directly.
**How to avoid:** Follow the existing pattern at line 305: `return false;` after successful registration. A positive cache hit should behave identically.
**Warning signs:** Positive cache hits cause font selection to use the wrong (substituted) font.

### Pitfall 3: Negative Cache After registerFontFromVFS Failure

**What goes wrong:** Forgetting to cache a negative result when `registerFontFromVFS()` fails (corrupt font file, bad format).
**Why it happens:** The code path for "JS returned data but registration failed" is easy to miss -- it is a separate branch from "JS returned null".
**How to avoid:** The user explicitly decided: "If AddTempDevFont fails, cache as negative -- don't retry." Ensure the negative cache insert happens in BOTH branches: (a) JS returns null, and (b) JS returns data but registration fails.
**Warning signs:** Repeated JS calls for a font that always fails registration, visible as repeated "WASM font resolution: requesting..." in logs.

### Pitfall 4: Style Hints May Be DONTKNOW

**What goes wrong:** Assuming style hints always have meaningful values. Many font requests arrive with `WEIGHT_DONTKNOW`, `ITALIC_DONTKNOW`, `WIDTH_DONTKNOW`, `PITCH_DONTKNOW` -- especially for the initial lookup before fontconfig processes the request.
**Why it happens:** `FontSelectPattern` inherits from `FontAttributes` and the default constructor sets all enums to their DONTKNOW values.
**How to avoid:** This is actually fine for caching -- DONTKNOW values are valid cache key components. Two requests with the same family name and both DONTKNOW for all hints will correctly hash to the same key. The important thing is that the cache key comparison uses exact equality, not "fuzzy" matching.
**Warning signs:** None -- this is a non-issue as long as `operator==` uses exact comparison.

### Pitfall 5: Existing s_aTriedFonts Must Be Fully Removed

**What goes wrong:** Leaving the old `s_aTriedFonts` set alongside the new cache, creating dual bookkeeping that can get out of sync.
**Why it happens:** Incremental development -- adding the new cache without removing the old one.
**How to avoid:** Phase 2 must completely replace `s_aTriedFonts` (line 71) with the new `s_aWasmFontCache`. All references to `s_aTriedFonts` (lines 71, 277, 279, 319) must be updated to use the new cache.
**Warning signs:** Both `s_aTriedFonts` and the new cache appearing in the code.

## Discretion Recommendations

### SAL_LOG Category: Use `vcl.fonts` (existing)

**Recommendation:** Keep the existing `vcl.fonts` category rather than creating a new subcategory like `vcl.fonts.wasm`.

**Rationale:**
1. The existing code already uses `SAL_INFO("vcl.fonts", "WASM font resolution: ...")` with a "WASM" prefix in the message text (lines 283-320 of fontsubst.cxx).
2. SAL_LOG filtering is hierarchical: `SAL_LOG=+INFO.vcl.fonts` enables all `vcl.fonts.*` subcategories. A new subcategory would not provide filtering benefit -- users enabling font diagnostics want ALL font info, including WASM resolution.
3. The success criteria from the phase description explicitly says: "Setting SAL_LOG=+INFO.vcl.fonts shows the complete font resolution flow."
4. 30+ existing call sites in `vcl/unx/generic/fontmanager/` use `vcl.fonts`. Consistency is valuable.

**Message prefix:** Use `"WASM font"` prefix in all log messages to distinguish from native font operations. This provides grep-ability without requiring a separate log category.

### Cache Data Structure: std::unordered_map

**Recommendation:** `std::unordered_map<WasmFontCacheKey, WasmFontCacheResult, WasmFontCacheKeyHash>`

**Rationale:**
1. O(1) average lookup vs O(log n) for `std::map`
2. Already used in `fontconfig.cxx` and `fontmanager.cxx` in the same directory
3. Typical cache size is small (10-50 fonts per document), so either would work, but `unordered_map` is the conventional choice for caches

### Hash Function: Boost-style hash_combine

**Recommendation:** Use the golden ratio constant `0x9e3779b9` with shift-XOR combining, as shown in Pattern 1 above.

**Rationale:** This is the de facto standard for hash combining in C++ (originated in Boost, widely adopted). It provides good distribution for correlated integer inputs like enum values.

### Log Message Format

**Recommendation:** Use consistent structured format:

```
WASM font cache: [event] "[family_name]" (w=[weight] i=[italic] wd=[width] p=[pitch])
```

For the 5 log points:
1. `WASM font resolution: requesting "[name]" from host (w=bold i=none wd=normal p=variable)`
2. `WASM font cache: [positive|negative] hit for "[name]"`
3. `WASM font resolution: calling JS for "[name]"`
4. `WASM font resolution: JS returned [path "/tmp/fonts/X.ttf"|empty] for "[name]"`
5. `WASM font resolution: registration [succeeded|failed] for "[name]"`

Note: Points 1 and 3 can be merged since "requesting from host" and "calling JS" are the same event. This would produce ~4 distinct log lines for a cache miss (request, JS result, registration, cache update) and ~1 for a cache hit.

## Code Examples

### Example 1: Complete Cache Infrastructure

```cpp
// Source: Derived from existing fontsubst.cxx patterns + boost::hash_combine
// Location: vcl/unx/generic/fontmanager/fontsubst.cxx, inside #ifdef EMSCRIPTEN

#include <functional> // for std::hash

enum class WasmFontCacheResult { Positive, Negative };

struct WasmFontCacheKey
{
    OUString maFamilyName;
    FontWeight meWeight;
    FontItalic meItalic;
    FontWidth meWidthType;
    FontPitch mePitch;

    bool operator==(const WasmFontCacheKey& rOther) const = default;
};

struct WasmFontCacheKeyHash
{
    size_t operator()(const WasmFontCacheKey& rKey) const
    {
        size_t nHash = rKey.maFamilyName.hashCode();
        nHash ^= std::hash<int>{}(static_cast<int>(rKey.meWeight))
                 + 0x9e3779b9 + (nHash << 6) + (nHash >> 2);
        nHash ^= std::hash<int>{}(static_cast<int>(rKey.meItalic))
                 + 0x9e3779b9 + (nHash << 6) + (nHash >> 2);
        nHash ^= std::hash<int>{}(static_cast<int>(rKey.meWidthType))
                 + 0x9e3779b9 + (nHash << 6) + (nHash >> 2);
        nHash ^= std::hash<int>{}(static_cast<int>(rKey.mePitch))
                 + 0x9e3779b9 + (nHash << 6) + (nHash >> 2);
        return nHash;
    }
};

// Replaces the old s_aTriedFonts set
static std::unordered_map<WasmFontCacheKey, WasmFontCacheResult,
                          WasmFontCacheKeyHash> s_aWasmFontCache;
```

**Note on `operator== default`:** C++20 defaulted comparison works here because all members support `==` (OUString and enums). If the WASM build does not enable C++20, write it out manually like the existing `FontSelectPattern::operator==` at line 114 of `FontSelectPattern.cxx`.

### Example 2: Revised WASM Block in FindFontSubstitute

```cpp
// Source: Derived from existing code at lines 269-323 of fontsubst.cxx
// Location: Inside FcPreMatchSubstitution::FindFontSubstitute(), replacing current WASM block

#ifdef EMSCRIPTEN
    if (s_pFontCollection)
    {
        WasmFontCacheKey aCacheKey{
            rFontSelData.maTargetName,
            rFontSelData.GetWeight(),
            rFontSelData.GetItalic(),
            rFontSelData.GetWidthType(),
            rFontSelData.GetPitch()
        };

        auto itr = s_aWasmFontCache.find(aCacheKey);
        if (itr != s_aWasmFontCache.end())
        {
            if (itr->second == WasmFontCacheResult::Positive)
            {
                SAL_INFO("vcl.fonts", "WASM font cache: positive hit for \""
                         << rFontSelData.maTargetName << "\"");
                // Font was previously resolved and registered.
                // Return false so caller finds it in PhysicalFontCollection.
                return false;
            }
            SAL_INFO("vcl.fonts", "WASM font cache: negative hit for \""
                     << rFontSelData.maTargetName
                     << "\", skipping JS call");
            // Fall through to fontconfig substitution below.
        }
        else if (!s_pFontCollection->FindFontFamily(rFontSelData.maTargetName))
        {
            // Cache miss AND font not in collection -- resolve from JS
            SAL_INFO("vcl.fonts", "WASM font resolution: requesting \""
                     << rFontSelData.maTargetName << "\" from host"
                     << " (w=" << rFontSelData.GetWeight()
                     << " i=" << rFontSelData.GetItalic()
                     << " wd=" << rFontSelData.GetWidthType()
                     << " p=" << rFontSelData.GetPitch() << ")");

            OString aUtf8 = OUStringToOString(rFontSelData.maTargetName,
                                               RTL_TEXTENCODING_UTF8);
            char* pPath = resolveFontFromHost(aUtf8.getStr());
            if (pPath)
            {
                OUString aPath = OStringToOUString(
                    OString(pPath), RTL_TEXTENCODING_UTF8);
                free(pPath);

                OUString aFileURL = "file://" + aPath;
                SAL_INFO("vcl.fonts", "WASM font resolution: JS returned \""
                         << aPath << "\" for \""
                         << rFontSelData.maTargetName << "\"");

                if (registerFontFromVFS(aFileURL, rFontSelData.maTargetName))
                {
                    SAL_INFO("vcl.fonts", "WASM font resolution: registration "
                             "succeeded for \""
                             << rFontSelData.maTargetName << "\"");
                    s_aWasmFontCache.emplace(aCacheKey,
                                             WasmFontCacheResult::Positive);
                    return false;
                }

                SAL_WARN("vcl.fonts", "WASM font resolution: registration "
                         "failed for \"" << aFileURL << "\"");
                // Registration failed (corrupt file, bad format) -- cache negative
                s_aWasmFontCache.emplace(aCacheKey,
                                         WasmFontCacheResult::Negative);
            }
            else
            {
                SAL_INFO("vcl.fonts", "WASM font resolution: JS returned "
                         "empty for \"" << rFontSelData.maTargetName << "\"");
                s_aWasmFontCache.emplace(aCacheKey,
                                         WasmFontCacheResult::Negative);
            }
        }
        // else: font already in collection (possibly registered by a different code path)
    }
#endif // EMSCRIPTEN
```

### Example 3: SAL_LOG Usage for Testing

```bash
# Enable font resolution logging
SAL_LOG=+INFO.vcl.fonts ./soffice --headless --convert-to pdf test.docx

# Example output for a cache miss (font not available):
# INFO:vcl.fonts:fontsubst.cxx:289: WASM font resolution: requesting "Segoe UI" from host (w=normal i=none wd=normal p=variable)
# INFO:vcl.fonts:fontsubst.cxx:303: WASM font resolution: JS returned empty for "Segoe UI"

# Example output for a cache miss (font resolved successfully):
# INFO:vcl.fonts:fontsubst.cxx:289: WASM font resolution: requesting "Calibri" from host (w=normal i=none wd=normal p=variable)
# INFO:vcl.fonts:fontsubst.cxx:305: WASM font resolution: JS returned "/tmp/fonts/Calibri.ttf" for "Calibri"
# INFO:vcl.fonts:fontsubst.cxx:309: WASM font resolution: registration succeeded for "Calibri"

# Example output for subsequent cache hits:
# INFO:vcl.fonts:fontsubst.cxx:278: WASM font cache: positive hit for "Calibri"
# INFO:vcl.fonts:fontsubst.cxx:282: WASM font cache: negative hit for "Segoe UI", skipping JS call
```

## State of the Art

| Old Approach (Phase 1) | New Approach (Phase 2) | Impact |
|------------------------|----------------------|--------|
| `std::set<OUString> s_aTriedFonts` -- family name only | `std::unordered_map<WasmFontCacheKey, WasmFontCacheResult>` -- family + style hints | Distinguishes positive/negative results, ready for Phase 3 variant support |
| Implicit negative cache (tried = negative) | Explicit positive/negative enum | Clear semantics, positive hits skip even `FindFontFamily()` check |
| No cache on registration failure | Negative cache on registration failure | Prevents repeated JS calls for corrupt fonts |
| SAL_INFO logging at 4 points (partial) | SAL_INFO logging at 5 points (complete) | Full observability of the resolution flow |

## Implementation Notes

### Current State Analysis

The current `fontsubst.cxx` implementation (lines 63-323) uses:
- `emscripten_proxy_sync_with_ctx` for main-thread proxying (NOT `EM_ASYNC_JS`)
- `EM_JS` for the JS function `em_startFontResolve` (synchronous dispatch, async completion via callback)
- `globalThis.__resolveSystemFont` as the JS callback (NOT `Module.resolveSystemFont`)
- `em_fontResolveComplete` as the completion callback from JS to C++
- `s_aTriedFonts` as a `std::set<OUString>` at line 71

Phase 2 changes are **additive** to the resolution mechanism:
- The `resolveFontFromHost()` / `em_startFontResolve` / `em_fontResolveComplete` pipeline remains untouched
- Only the caching logic (what wraps the `resolveFontFromHost` call) changes
- Only the logging statements change

### What Changes, What Stays

**Stays the same:**
- `FontResolveRequest` struct (lines 74-79)
- `em_startFontResolve` EM_JS function (lines 85-110)
- `em_fontResolveComplete` callback (lines 114-120)
- `fontResolveOnMainThread` proxy callback (lines 124-129)
- `resolveFontFromHost` blocking wrapper (lines 134-148)
- `registerFontFromVFS` registration function (lines 154-185)
- `s_pFontCollection` static pointer (line 67)
- `RegisterFontSubstitutors` storing the collection pointer (line 192)

**Changes:**
- `s_aTriedFonts` (line 71) -> replaced by `s_aWasmFontCache` (new unordered_map)
- WASM block in `FindFontSubstitute` (lines 269-323) -> restructured with cache checks + enhanced logging
- New types: `WasmFontCacheKey`, `WasmFontCacheResult`, `WasmFontCacheKeyHash` (added before line 71)

### Scope of Change

Estimated: ~60 lines added, ~20 lines removed, ~10 lines modified. Net: ~50 new lines.

## Open Questions

1. **C++20 `operator== default` availability in WASM build**
   - What we know: LibreOffice targets C++17 as the minimum, but many compilers support C++20 features. The Emscripten build likely supports C++20.
   - What's unclear: Whether the specific WASM build configuration enables C++20 features.
   - Recommendation: Write `operator==` manually (5 lines) to avoid any compatibility risk. Trivial to change later.
   - Confidence: MEDIUM -- likely works but not verified for this build.

2. **console.warn in em_startFontResolve**
   - What we know: Line 104 has `console.warn('em_startFontResolve: wrote font to', path)`. The user decided C++ side only logging. This is JS-side logging.
   - What's unclear: Whether to remove these `console.warn` calls as part of Phase 2 or leave them for debugging.
   - Recommendation: Leave the existing JS `console.warn` calls for now -- they are part of the Phase 1 implementation and removing them is cleanup, not Phase 2 scope. Phase 2's "C++ side only" decision applies to NEW logging added in this phase.
   - Confidence: HIGH -- reasonable interpretation of the constraint.

## Sources

### Primary (HIGH confidence)

- `vcl/unx/generic/fontmanager/fontsubst.cxx` -- Full current source read. Phase 1 implementation verified: `s_aTriedFonts` at line 71, WASM block at lines 269-323, `resolveFontFromHost` at lines 134-148, `registerFontFromVFS` at lines 154-185.
- `vcl/inc/font/FontSelectPattern.hxx` -- `FontSelectPattern` class definition verified: inherits `FontAttributes`, has `maTargetName`, `maSearchName`, `GetWeight()`, `GetItalic()`, `GetPitch()`, `GetWidthType()` at lines 41-74.
- `vcl/inc/fontattributes.hxx` -- `FontAttributes` base class verified: `FontWeight meWeight`, `FontItalic meItalic`, `FontPitch mePitch`, `FontWidth meWidthType` at lines 66-74.
- `vcl/source/font/FontSelectPattern.cxx` -- `hashCode()` at lines 90-112 uses `GetWeight()`, `GetItalic()`, `mnOrientation`, `meLanguage` for hashing. `operator==` at lines 114-153 uses `CompareDeviceIndependentFontAttributes()` + all fields.
- `vcl/source/font/fontattributes.cxx` -- `CompareDeviceIndependentFontAttributes()` at lines 32-52 compares `maFamilyName`, `maStyleName`, `meWeight`, `meItalic`, `meFamily`, `mePitch`, `meWidthType`.
- `include/tools/fontenum.hxx` -- Enum definitions verified: `FontWeight` (11 values), `FontWidth` (10 values), `FontItalic` (4 values), `FontPitch` (3 values).
- `include/sal/log.hxx` -- SAL_INFO/SAL_WARN macro definitions at lines 368-371. Area-based filtering confirmed.
- `vcl/source/font/PhysicalFontCollection.cxx` -- `FindFontFamily()` call sites at lines 1076 and 1109, post-hook lookup at line 1085.
- Existing SAL_INFO("vcl.fonts", ...) usage: 30+ call sites in `vcl/unx/generic/fontmanager/` confirmed the `vcl.fonts` area is standard.

### Secondary (MEDIUM confidence)

- Boost hash_combine pattern: widely documented, used in production C++ codebases. The `0x9e3779b9` constant is the golden ratio's fractional part in 32-bit integer form.

### Tertiary (LOW confidence)

- C++20 `operator== default` in Emscripten: likely supported but not verified for LibreOffice's WASM build configuration.

## Metadata

**Confidence breakdown:**
- Cache design: HIGH -- All data structures and patterns verified from existing codebase. Cache key fields match `uselessmatch()` comparator exactly.
- Logging design: HIGH -- SAL_INFO/SAL_WARN pattern verified from 30+ existing call sites. Success criteria explicitly specifies `vcl.fonts` area.
- Implementation scope: HIGH -- Single file modification, all changes within existing `#ifdef EMSCRIPTEN` block. Estimated ~50 net new lines.
- Pitfalls: HIGH -- All identified from direct code reading (return value semantics, cache key field choice, dual bookkeeping risk).

**Research date:** 2026-02-11
**Valid until:** 2026-03-11 (30 days -- stable domain, no external dependencies)
