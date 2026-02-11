# Phase 2: Caching and Diagnostics - Context

**Gathered:** 2026-02-11
**Status:** Ready for planning

<domain>
## Phase Boundary

Eliminate redundant JavaScript font resolution calls through caching (both positive and negative), add re-registration guards, and make the resolution flow observable through SAL_LOG diagnostics. The JS callback contract from Phase 1 does not change. No new font resolution capabilities are added.

</domain>

<decisions>
## Implementation Decisions

### Cache behavior
- Session-only in-memory cache — cleared on page reload, no persistence (IndexedDB, etc.)
- Negative cache: once per session — if JS says "not available", never ask again until reload
- Positive cache: flag-based — track "we already resolved this" to skip JS call, don't track VFS path
- Cache key: family name + style hints (weight, italic, width, pitch) — ready for Phase 3 variant support from day one
- If AddTempDevFont fails (corrupt file, bad format), cache as negative — don't retry

### Logging design
- Key events only, not full trace — ~5 log lines per resolution: font requested, cache hit/miss, JS called, result (path or empty), registration success/fail
- No timing information in logs
- C++ side only (SAL_LOG) — no JS-side console.log for font resolution
- Log category: Claude's discretion (existing vcl.fonts or new subcategory)

### Guard & edge cases
- No deduplication of concurrent requests — let all calls through independently (WASM is effectively single-threaded via JSPI suspend)
- Explicit guard before AddTempDevFont — check cache before calling, never register the same font twice
- VFS file disappearance: ignore — not a realistic scenario, don't add complexity

### Host-side contract
- Contract unchanged from Phase 1: C++ asks for font by name, JS responds with binary data
- One-way communication only — C++ never exposes cache state to JS
- JS callback signature stays the same — no new parameters or return metadata
- JS side caches `queryLocalFonts()` result (font enumeration) — called lazily on first font request, not eagerly on startup
- Font binary data not cached separately — once written to VFS, it persists for the session

### Claude's Discretion
- SAL_LOG category choice (vcl.fonts vs vcl.fonts.wasm or similar)
- Exact log message format and wording
- Internal cache data structure (std::set, std::unordered_map, etc.)
- Cache key format/hashing for family+hints composite key

</decisions>

<specifics>
## Specific Ideas

- The existing `s_aTriedFonts` set from Phase 1 is already a partial negative cache — Phase 2 should formalize and extend this into a proper cache structure that handles both positive and negative results
- Binary font data lives in the Emscripten VFS after first write — no need to cache the data itself anywhere
- JS-side `queryLocalFonts()` cache is a separate concern from C++ caching — it's about avoiding re-enumerating system fonts, not about font resolution caching

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope

</deferred>

---

*Phase: 02-caching-and-diagnostics*
*Context gathered: 2026-02-11*
