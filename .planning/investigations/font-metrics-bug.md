# Font Metrics Bug Investigation

**STATUS: ROOT CAUSE FOUND — FIX IMPLEMENTED**

## Problem Statement
When converting `math_equations - Repaired.pptx` (which uses Calibri font) to PDF:
- **Bundled fonts (no JS hook)**: Carlito substituted, renders correctly with proportional spacing
- **JS Hook resolution**: Calibri loaded from MS Word DFonts, renders with broken spacing — every glyph has uniform ~364 effective advance regardless of actual glyph width

## Root Cause (Confirmed via Debug Logs)

**Two bugs combine to cause the issue:**

### Bug 1: Stale `s_pFontCollection` pointer (PRIMARY)
`fontsubst.cxx` stores a static `s_pFontCollection` pointer set once during `RegisterFontSubstitutors()`. When the PreMatchHook fires from a DIFFERENT `PhysicalFontCollection` instance (e.g., PDF export's VirtualDevice creates a new collection), `registerFontFromVFS` adds fonts to the OLD collection. The caller's `ImplFindFontFamilyBySearchName()` searches the CURRENT collection — fonts aren't there.

**Proof from logs:**
- `ImplFindFontFamilyBySearchName("calibri")` returns nullptr even AFTER `registerFontFromVFS` announced fonts
- The EMSCRIPTEN check (`s_pFontCollection->FindFontFamily("Calibri")`) succeeds (old collection has it)
- But the caller's lookup on the current collection fails

### Bug 2: `maSearchName` stuck on metric-compatible substitute
`FindMetricCompatibleFont` changes `maSearchName` from "calibri" to "carlito" (line 1053). When it fails to find Carlito and the PreMatchHook loads Calibri and returns `false`, the post-hook lookup at line 1085 searches for "carlito" — never finds the newly loaded Calibri.

### Combined Effect
1. "Calibri" requested → `FindMetricCompatibleFont` maps to "carlito" → Carlito not found (not bundled in this config)
2. PreMatchHook loads 6 Calibri files → `registerFontFromVFS` adds to WRONG collection → returns `false`
3. Post-hook lookup for "carlito" → NOT found (Carlito was never loaded)
4. Second for loop → PreMatchHook called again → EMSCRIPTEN block skipped (old collection has Calibri) → fontconfig runs
5. **fontconfig returns OpenSymbol** (broken fallback in WASM — the only available font family)
6. OpenSymbol (1066 glyphs) selected → HarfBuzz shapes with OpenSymbol face
7. All Latin chars → `.notdef` glyph (glyph 0) → advance 748 font units
8. 748 * (150/2048) * 1000 / 150 = **365 PDF units** ← matches the ~364 observed!

### Key Log Evidence

**Bundled (correct):**
```
InitHbFont: family='Calibri' hb_glyph_count=2782  ← Carlito
HB advance[0]: glyph=7 x_advance=1092  ← proportional
HB advance[1]: glyph=1140 x_advance=470
HB advance[2]: glyph=98 x_advance=470
HB advance[3]: glyph=49 x_advance=866  ← all different
```

**JS Hook (broken):**
```
InitHbFont: family='Calibri' hb_glyph_count=1066  ← OpenSymbol!
HB advance[0]: glyph=8 x_advance=1024  ← space?
HB advance[1]: glyph=0 x_advance=748   ← .notdef
HB advance[2]: glyph=0 x_advance=748   ← .notdef
HB advance[3]: glyph=0 x_advance=748   ← all the same!
```

## Fix Applied (2 changes)

## Evidence from PDF Analysis

### TJ Operator Comparison
The PDF TJ operators reveal the core symptom:

| Char | Declared Width (PDF units) | Effective Advance (JS Hook) | Effective Advance (Baseline) |
|------|---------------------------|----------------------------|------------------------------|
| S    | 472                       | 364                        | 472                          |
| l    | 245                       | 363                        | 245                          |
| d    | 536                       | 365                        | 536                          |
| m    | 813                       | 364                        | 812                          |
| space| 226                       | 498                        | 226                          |

- Baseline (Carlito): Glyphs grouped in TJ runs with tiny kerning adjustments (1-14 thousandths)
- JS Hook (Calibri): Every glyph individually positioned with large adjustments to compensate

### Key Observation
The `/Widths` arrays in both PDFs contain **correct** Calibri Bold metrics. The font is **embedded correctly** in the PDF. The problem is in the **layout positions** — they use a uniform ~364 advance, then the PDF writer compensates with TJ adjustments.

### What ~364 Means (NOW EXPLAINED)
- OpenSymbol `.notdef` glyph: 748 font units at upem=2048
- At mnHeight=150: scaled = 748 * (150/2048) = 54.79 pixels
- In PDF units: 54.79 * 1000 / 150 = **365.3** ← matches the ~364 observed!
- The uniformity is because ALL Latin chars map to `.notdef` in OpenSymbol (a symbol font)

## How the Code Works

### PDF TJ Adjustment Computation (`pdfwriter_impl.cxx:5816-5820`)
```cpp
double fAdvance = (thisPos.x - prevPos.x) * 1000.0 / nPixelFontHeight;
double fAdjustment = nativeWidth - fAdvance;
```
- `nativeWidth` = `GetGlyphWidth()` via HarfBuzz → **correct** (matches Calibri Bold)
- `fAdvance` = from layout glyph positions → **wrong** (uniform ~364)

So the positions stored during layout used wrong advances, but the glyph widths queried during PDF export return correct values.

### Font Selection Flow (`PhysicalFontCollection::FindFontFamily`, line 966+)
For a request for "Calibri":
1. **Line 1050**: Direct lookup `ImplFindFontFamilyBySearchName("calibri")` — found if Calibri was loaded earlier
2. **Line 1053**: `FindMetricCompatibleFont(rFSD)` — maps "calibri" → "carlito", sets `rFSD.maSearchName = "carlito"`. If Carlito available, returns it
3. **Line 1076**: PreMatchHook (`FcPreMatchSubstitution::FindFontSubstitute`) — WASM JS resolution
4. **Line 1085**: `ImplFindFontFamilyBySearchName(rFSD.maSearchName)` — searches for whatever maSearchName is

### The maSearchName Bug Hypothesis
There's a potential bug in the interaction between `FindMetricCompatibleFont` and the PreMatchHook:
1. `FindMetricCompatibleFont` at line 1053 changes `maSearchName` from "calibri" to "carlito"
2. If Carlito IS available: returns Carlito immediately (this is the bundled fonts case)
3. If Carlito is NOT available: returns nullptr, BUT `maSearchName` is now "carlito"
4. PreMatchHook at line 1076 loads Calibri from JS (uses `maTargetName` which is still "Calibri")
5. Hook returns `false` (our trick to let caller find the font)
6. Line 1085 searches for `maSearchName` which is **still "carlito"** — Calibri never found through this path!

**However**, this may NOT be the full explanation because Calibri IS in the final PDF, so it's found eventually through some path.

### Alternative Theory: Prior Loading
If the hook fires for a DIFFERENT font first (e.g., "Segoe UI" which is LO's default UI font), and during that resolution Calibri somehow gets loaded into the collection, then when "Calibri" is later requested:
1. Line 1050: Direct lookup "calibri" → **FOUND** (was loaded earlier)
2. Returns Calibri directly, bypassing FindMetricCompatibleFont/Carlito mapping

But then why would the metrics be wrong? The font data should be the same either way.

### HarfBuzz Shaping Pipeline (`CommonSalLayout.cxx`)
```cpp
hb_font_t *pHbFont = GetFont().GetHbFont();  // line 394
// ...
hb_shape_full(pHbFont, pHbBuffer, ...);       // line 620
// ...
nAdvance = pHbPositions[i].x_advance * nXScale; // line 806
```
- HarfBuzz font comes from `LogicalFontInstance::GetHbFont()` → `InitHbFont()` → `GetFontFace()->GetHbFace()`
- Scale: `GetScale()` computes `nXScale = (mnWidth ? mnWidth * avgWidthFactor : mnHeight) / nUPEM`

### HarfBuzz Font Creation (`LogicalFontInstance::InitHbFont`, line 63)
```cpp
hb_face_t* pHbFace = pFace->GetHbFace();       // From FreetypeFontFace
hb_font_t* pHbFont = hb_font_create(pHbFace);
hb_font_set_scale(pHbFont, nUPEM, nUPEM);      // Scale set to upem
```
The HarfBuzz face comes from the PhysicalFontFace. If this face is broken or empty, all advances would be wrong.

### HarfBuzz Blob Creation (`freetype_glyphcache.cxx:CreateHbBlob`)
After our fix, this now uses mmap'd buffer first (same as FreeType). Previously used `hb_blob_create_from_file()` which might fail on Emscripten MEMFS. **Fix didn't help** — so the blob was likely correct already.

## Hypotheses (Ranked)

### H1: HarfBuzz face is broken for dynamically loaded fonts (MOST LIKELY)
Even though the blob data is correct (CreateHbBlob fix verified this), the HarfBuzz face created from the blob might be empty or have the wrong table structure. The `hb_face_get_glyph_count()` debug print will reveal this.

### H2: Font scale mismatch (mnWidth vs mnHeight)
If `mnWidth` is set and different from `mnHeight`, the X scale would differ from the Y scale. The PDF writer uses `mnHeight` for the conversion factor (`nPixelFontHeight`), but layout positions use X scale. This would produce a systematic error — but NOT a uniform advance.

### H3: Wrong font face selected (face index mismatch)
If the font file contains multiple faces (TTC) and the wrong index is used, the wrong font's metrics would be applied. Unlikely since Calibri files from MS Word DFonts appear to be single-face TTF files.

### H4: Font fallback produces initial layout, then font changes
If Calibri is first laid out with a fallback font (wrong metrics), then the font selection changes to Calibri without re-layout, the positions from the fallback persist while PDF embedding uses Calibri.

### H5: `FindMetricCompatibleFont` corrupts `maSearchName` (PLAUSIBLE)
If `maSearchName` is stuck on "carlito" after the hook returns false, Calibri is never found through the name-based path. It eventually gets found through attribute-based fallback with potentially wrong parameters. This would explain both the wrong metrics and the correct font embedding.

## Ruled Out
- **HarfBuzz blob creation failure**: Fixed CreateHbBlob to use mmap buffer. Issue persists. Blob data was already correct.
- **Font file corruption**: The `/Widths` array in the PDF contains correct Calibri Bold widths, and the font renders correctly (just at wrong positions). Font data is fine.

## Debug Instrumentation Added
Added `fprintf(stderr, "FONTDBG ...")` to these files:

1. **`PhysicalFontCollection.cxx`** — traces FindFontFamily path: direct match, metric-compatible, PreMatchHook, post-hook lookup
2. **`fontsubst.cxx`** — traces PreMatchHook entry (targetName, searchName) and successful font loading (shows searchName that caller will use)
3. **`LogicalFontInstance.cxx`** — traces InitHbFont (upem, glyph count, face pointer) and GetScale (mnHeight, mnWidth, upem, avgWidthFactor, scales)
4. **`CommonSalLayout.cxx`** — traces LayoutText (font name, height, width, upem, glyph count) and first 10 HarfBuzz advances for Calibri/Carlito
5. **`pdfwriter_impl.cxx`** — traces PDF glyph collection (font, position, native width, unscaled glyph width)

All filtered to only print for Calibri/Carlito family names (except the general FindFontFamily/PreMatchHook prints which show all fonts to trace ordering).

## Next Steps After Retrieving Logs

### Analysis Plan
1. **Compare FindFontFamily paths**: Does bundled use "metric-compatible → carlito" while JS Hook uses "direct match → calibri"?
2. **Check InitHbFont glyph count**: Is `hb_face_get_glyph_count()` correct (>0, reasonable number) for Calibri?
3. **Check HB advances**: Are they uniform or proportional? If uniform, the issue is in HarfBuzz face/shaping. If proportional, the issue is in scale computation.
4. **Check GetScale values**: Is `mnWidth` set? What's `avgWidthFactor`? Do the scales match expectations?
5. **Check PDF glyph positions**: Confirm the pos values match what we'd expect from the HB advances * scale

### Potential Fixes (Depending on Findings)
- **If maSearchName bug confirmed**: Reset `maSearchName` to target name after PreMatchHook returns false with successful font load
- **If HB face broken**: Investigate `GetHbFace()` for dynamically registered fonts — may need to ensure the face is properly created from the correct blob
- **If scale mismatch**: Track down where `mnWidth` is being set and why it differs between Calibri and Carlito paths
- **If font fallback timing**: Ensure re-layout happens when font changes from fallback to actual font
