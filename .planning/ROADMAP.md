# Roadmap: LibreOffice WASM System Font Resolution

## Overview

This roadmap delivers dynamic system font resolution for LibreOffice's WASM build in three phases. Phase 1 proves the end-to-end path: a missing font triggers a JSPI-bridged JavaScript callback, font data flows back through the virtual filesystem, and AddTempDevFont registers it for rendering. Phase 2 adds caching, guards, and diagnostics to make the system robust for real document workloads. Phase 3 extends font matching with style/weight hints and TTC collection support so the host can provide the exact font variant a document needs.

## Phases

**Phase Numbering:**
- Integer phases (1, 2, 3): Planned milestone work
- Decimal phases (2.1, 2.2): Urgent insertions (marked with INSERTED)

Decimal phases appear between their surrounding integers in numeric order.

- [ ] **Phase 1: Core Font Resolution** - End-to-end JSPI bridge, font request, VFS write, registration, and fallback
- [ ] **Phase 2: Caching and Diagnostics** - Negative cache, re-registration guard, and structured logging
- [ ] **Phase 3: Font Variant Support** - Style/weight/width/pitch hints and TTC collection files

## Phase Details

### Phase 1: Core Font Resolution
**Goal**: A document requesting a missing font triggers a JavaScript callback that provides font data, and the font renders correctly in the output
**Depends on**: Nothing (first phase)
**Requirements**: REQ-F01, REQ-F02, REQ-F03, REQ-F04, REQ-F05, REQ-F06, REQ-N01, REQ-N02, REQ-N03, REQ-N04
**Success Criteria** (what must be TRUE):
  1. A WASM build of LibreOffice, when rendering a document that requests "Calibri" (not bundled), calls a JavaScript callback with the string "Calibri"
  2. When JavaScript writes a TTF or OTF font file to the Emscripten virtual filesystem and returns the path, the font is registered and subsequent text renders using that font
  3. When JavaScript returns an empty string (font unavailable), LibreOffice falls through to its normal fallback font without crashing or hanging
  4. The WASM thread suspends via JSPI during the JavaScript callback and resumes correctly with the result, without deadlocking
  5. Desktop (non-WASM) builds compile and run with zero behavioral changes -- no new warnings, no font resolution differences, no binary size increase
**Plans**: TBD

Plans:
- [ ] 01-01: TBD
- [ ] 01-02: TBD

### Phase 2: Caching and Diagnostics
**Goal**: Font resolution does not make redundant JavaScript calls, and the complete resolution flow is observable through diagnostic logging
**Depends on**: Phase 1
**Requirements**: REQ-F07, REQ-F08, REQ-F09
**Success Criteria** (what must be TRUE):
  1. A font that JavaScript reported as unavailable triggers only ONE JavaScript call per session, regardless of how many times document layout requests it
  2. A font that was previously fetched and registered triggers zero additional JavaScript calls on subsequent lookups
  3. Setting SAL_LOG=+INFO.vcl.fonts shows the complete font resolution flow: font name requested, cache hit/miss, JavaScript call made, response received, AddTempDevFont result, and retry outcome
**Plans**: TBD

Plans:
- [ ] 02-01: TBD

### Phase 3: Font Variant Support
**Goal**: The host can provide the exact font variant (bold, italic, condensed) a document needs, and TrueType Collection files register all contained faces
**Depends on**: Phase 2
**Requirements**: REQ-F10, REQ-F11
**Success Criteria** (what must be TRUE):
  1. A document using bold Arial triggers a JavaScript callback that includes weight, italic, width, and pitch hints alongside the family name, enabling the host to distinguish "Arial Bold" from "Arial Regular"
  2. A CJK .ttc (TrueType Collection) font file provided by the host registers all contained faces and renders CJK text correctly
**Plans**: TBD

Plans:
- [ ] 03-01: TBD

## Progress

**Execution Order:**
Phases execute in numeric order: 1 -> 2 -> 3

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 1. Core Font Resolution | 0/TBD | Not started | - |
| 2. Caching and Diagnostics | 0/TBD | Not started | - |
| 3. Font Variant Support | 0/TBD | Not started | - |
