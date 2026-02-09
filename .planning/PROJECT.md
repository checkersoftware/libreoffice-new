# LibreOffice WASM System Font Resolution

## What This Is

A modification to LibreOffice's VCL font subsystem that enables the WASM build to resolve fonts dynamically from the host environment (Electron/Node.js) instead of bundling all fonts into `soffice.data`. This dramatically reduces the WASM data file size (~53MB of fonts out of ~96MB) while maintaining document rendering fidelity.

## Core Value

When LibreOffice WASM can't find a requested font, it calls out to JavaScript so the host can provide font data from the system — eliminating the need to bundle most fonts.

## Requirements

### Validated

- VCL font substitution system works via `FcPreMatchSubstitution::FindFontSubstitute()` in `vcl/unx/generic/fontmanager/fontsubst.cxx`
- `AddTempDevFont()` API exists for dynamically registering fonts at runtime
- Emscripten builds already use headless SVP backend (`vcl/headless/`)
- `EM_JS`/`EM_ASM` interop exists in the codebase for C++ → JS calls
- JSPI support is conditionally available via `ENABLE_EMSCRIPTEN_JSPI`

### Active

- [ ] C++ font resolution hook that calls out to JS when a font is not found (WASM only)
- [ ] Dynamic font registration via `AddTempDevFont()` after receiving font data from JS
- [ ] Minimal fallback font set (OpenSymbol + Liberation family) bundled in `soffice.data`
- [ ] Build with `--without-fonts` to exclude bundled font collection
- [ ] Sync/async bridge (JSPI or ASYNCIFY) for the C++ → JS → C++ roundtrip
- [ ] Documents render correctly with system-provided fonts

### Out of Scope

- Electron IPC handler and JS-side font resolution — user handles this separately
- Browser Local Font Access API support — Electron only
- Font caching/persistence across sessions — host responsibility
- Changes to non-WASM platforms — all modifications behind `#ifdef EMSCRIPTEN`

## Context

This is a brownfield modification to the LibreOffice core repository. The change is scoped to the VCL font subsystem, specifically the Emscripten/headless backend.

**The problem:** LibreOffice WASM bundles ~136 font files (~53MB) into `soffice.data` to ensure documents render with correct fonts. For a headless PDF conversion use case in Electron (Diffchecker), this is wasteful — the host has system fonts available but LibreOffice can't access them.

**The approach:** Hook into the font substitution path. When fontconfig can't find a requested font, an `#ifdef EMSCRIPTEN` block calls out to JS via `EM_JS`. The JS side (implemented by the user in Electron) resolves the font from the system and returns font data. The C++ side writes it to the virtual FS and registers it with `AddTempDevFont()`, then retries the font lookup.

**Build path validation:** Despite living under `vcl/unx/`, the font substitution code IS compiled for WASM. `vcl/Library_vcl.mk` defines `vcl_headless_freetype_code` (line 660) which includes `vcl/unx/generic/fontmanager/fontsubst` and related files. This is compiled whenever `USE_HEADLESS_CODE=TRUE`, which is set for Emscripten because `configure.ac` sets `using_freetype_fontconfig=yes` for the `emscripten` target. The full WASM font chain is: `SvpSalInstance` → `SalGenericInstance::RegisterFontSubstitutors()` → `FcPreMatchSubstitution` (fontconfig-based).

**Key source files:**
- `vcl/unx/generic/fontmanager/fontsubst.cxx` — font substitution hooks (primary modification point, confirmed compiled for WASM via `vcl_headless_freetype_code` in `vcl/Library_vcl.mk`)
- `vcl/headless/svptext.cxx` — headless backend's `AddTempDevFont()` implementation
- `vcl/inc/font/fontsubstitution.hxx` — substitution interfaces
- `vcl/source/font/PhysicalFontCollection.cxx` — font collection management
- `vcl/unx/generic/fontmanager/fontmanager.cxx` — font manager (fontconfig integration)
- `vcl/unx/generic/gdi/freetypetextrender.cxx` — where `RegisterFontSubstitutors()` is called during font list initialization
- `vcl/Library_vcl.mk` — build configuration confirming which files are compiled for headless/WASM

**Sync/async challenge:** The C++ font resolution is synchronous. The JS font resolution (Electron IPC to main process, read font file, return data) is async. This requires either JSPI (WebAssembly JavaScript Promise Integration) or ASYNCIFY to bridge the gap. JSPI is preferred (no binary size overhead) but needs investigation for compatibility.

## Constraints

- **Platform**: All changes must be behind `#ifdef EMSCRIPTEN` — no impact on desktop builds
- **API contract**: The JS callback interface must be simple — receive font family name, return font data (or null)
- **Binary compat**: Changes must not break the existing font substitution pipeline for non-WASM builds
- **Minimal bundled fonts**: OpenSymbol (always needed for symbols) + Liberation Sans/Serif/Mono + Carlito + Caladea as baseline fallbacks

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| Hook at `FcPreMatchSubstitution::FindFontSubstitute()` | Called before fallback, has font family name, existing cache mechanism. Confirmed compiled for WASM via `vcl_headless_freetype_code` in `vcl/Library_vcl.mk` | ✓ Good |
| Use `EM_JS` for C++ → JS interop | Standard Emscripten mechanism, already used in codebase | — Pending |
| JSPI vs ASYNCIFY for sync/async bridge | JSPI preferred (no binary overhead) but needs compatibility check | — Pending |
| Keep minimal fallback font set | Safety net for when host can't provide fonts | — Pending |
| `--without-fonts` build flag | Excludes bundled font collection, saves ~53MB | — Pending |

---
*Last updated: 2026-02-09 after initialization*
