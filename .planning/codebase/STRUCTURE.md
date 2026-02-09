# Codebase Structure

**Analysis Date:** 2026-02-09

## Directory Layout

```
core/
├── sal/                       # System Abstraction Layer - OS services, threading, IPC
├── tools/                     # Deprecated utilities - being phased out (see tools/README.md)
├── basegfx/                   # Graphics math - vectors, matrices, polygons, algorithms
├── vcl/                       # Visual Class Library - widget toolkit, rendering abstraction
│   ├── inc/                   # Public API headers (cross-platform)
│   ├── source/                # Core cross-platform implementation
│   ├── headless/              # Bitmap-only backend (no native windowing)
│   ├── android/               # Android platform backend
│   ├── osx/                   # macOS platform backend
│   ├── quartz/                # Shared iOS/macOS code
│   ├── ios/                   # iOS platform backend
│   ├── win/                   # Windows platform backend
│   └── unx/                   # Linux/X11 backends (gtk3, gtk4, kf5, kf6, generic)
├── canvas/                    # UNO-based graphics backend (Cairo, etc.)
├── cppcanvas/                 # C++ helper classes for canvas
├── drawinglayer/              # Display-list drawing API with primitives & processors
├── svx/                       # Drawing model helpers (SdrObject, SdrView, etc.)
├── editeng/                   # Text editing engine - formatting, spell-check, attributes
├── cppu/                      # C++ UNO runtime - type system, marshalling
├── cppuhelper/                # C++ helpers for UNO component implementation
├── bridges/                   # Language bridges (C++/Java, C++/Python)
├── binaryurp/                 # Binary UNO protocol implementation
├── comphelper/                # Generic UNO component helpers
├── framework/                 # Modern UNO-based framework - menus, toolbars, dispatch
├── sfx2/                      # Legacy framework - SFX2, document shell, dispatch (SlotID)
├── filter/                    # Generic filter infrastructure
├── writerfilter/              # Word/DOCX filter implementation
├── odffilter/                 # ODF filter implementation
├── xmloff/                    # XML import/export for ODF
├── sw/                        # Writer application
│   ├── inc/                   # Public headers (doc.hxx, etc.)
│   ├── source/core/           # Document model, layout, text handling
│   ├── source/filter/         # Writer-specific filters (HTML, RTF, DOC, DOCX, ODF)
│   ├── source/uibase/         # UI layer (always loaded)
│   ├── source/ui/             # Optional UI parts (loaded on demand)
│   ├── uiconfig/              # UI configuration files (menus, toolbars)
│   ├── qa/                    # Unit and integration tests
│   └── sdi/                   # SlotID definition files
├── sc/                        # Calc spreadsheet application
├── sd/                        # Draw and Impress (presentations)
├── slideshow/                 # Presentation engine
├── dbaccess/                  # Database access and UI
├── forms/                     # Form controls and data binding
├── configmgr/                 # Configuration system core
├── officecfg/                 # Configuration schemas and data
│   └── registry/              # Schema definitions and default settings
├── desktop/                   # Application startup and shell binaries
│   ├── source/app/            # soffice binary, desktop class, bootstrap
│   ├── source/pkgchk/         # Extension manager
│   └── test/deployment/       # Test extensions
├── connectivity/              # Database connectivity (ODBC, JDBC bridges)
├── i18npool/                  # Internationalization - locale data, collation
├── scripting/                 # Scripting framework - StarBasic, Python, JavaScript
├── javaunohelper/             # Java-UNO bridge helper classes
├── cli_ure/                   # .NET/CLI bridge to UNO
├── embedserv/                 # OLE embedding server (Windows)
├── offapi/                    # LibreOffice UNO API definitions (IDL/XML)
├── odk/                       # SDK for extension developers
├── include/                   # Global public API headers (third-party accessible)
├── unodevtools/               # Tools for UNO development
├── idl/                       # IDL compiler and infrastructure
├── compilerplugins/           # Clang plugins for code checking
├── soltools/                  # Standalone tools
├── setup_native/              # Native installers (Windows, macOS, Linux)
├── android/                   # Android app shell and integration
├── ios/                       # iOS app shell and integration
├── pch/                       # Precompiled headers
├── config_host/               # Generated build configuration
├── bin/                       # Build output and temporary files
├── workdir/                   # Build output directory
├── instdir/                   # Installation staging directory
├── .git/                      # Git repository
├── .planning/                 # GSD planning documents
├── .config/                   # IDE/tool configuration
├── autom4te.cache/            # Autotools cache
└── aclocal.m4, configure      # Autotools build configuration
```

## Directory Purposes

**sal/**
- Purpose: Lowest-level platform abstraction - OS threading, dynamic library loading, file I/O, memory management
- Contains: `rtl/` (runtime library - strings), `osl/` (OS layer - platform-specific)
- Key files: `sal/inc/sal/main.h` (entry macro), `sal/osl/` (platform backends)
- Committed: Yes | Generated: No

**vcl/**
- Purpose: Widget toolkit and rendering abstraction layer
- Contains: Platform-independent public API in `inc/`, cross-platform implementation in `source/`, platform backends in subdirectories
- Key files: `vcl/inc/vcl/outdev.hxx` (rendering device), `vcl/source/outdev/` (implementation)
- Committed: Yes | Generated: No
- Backend selection: Compile-time via `--with-system-*` configure flags; runtime factory via `CreateSalInstance()`

**drawinglayer/**
- Purpose: Display-list drawing primitives and processors
- Contains: Primitive2D class hierarchy, processor implementations for different render targets
- Key files: `drawinglayer/source/primitive2d/` (primitives), `drawinglayer/source/processor2d/` (processors)
- Committed: Yes | Generated: No
- Usage pattern: Create Primitive2DSequence → pass to processor → processor renders to target (screen/metafile/PDF)

**editeng/**
- Purpose: Shared text editing, formatting, and spell-checking engine
- Contains: Text attribute system, spell-check integration, paragraph formatting
- Key files: `editeng/source/editeng/` (main engine)
- Committed: Yes | Generated: No
- Used by: Writer, Calc, Draw/Impress (for text in shapes), Forms

**sw/source/core/**
- Purpose: Writer document model and layout engine
- Contains:
  - `doc/` - Core document (SwDoc class, document-level operations)
  - `layout/` - Layout engine (page/frame rendering)
  - `text/` - Text node implementation
  - `crsr/` - Cursor and selection logic
  - `access/` - Accessibility API (UNO bridge)
  - `draw/` - Drawing integration
  - `fields/` - Field handling
  - `undo/` - Undo/Redo system
- Key files: `sw/inc/doc.hxx` (SwDoc central class), `sw/source/core/doc/doc.cxx` (implementation)
- Committed: Yes | Generated: No

**sw/source/filter/**
- Purpose: Writer-specific format filters
- Contains:
  - `ww8/` - DOC format import/export (legacy Word 97-2000)
  - `docx/` - DOCX wrapper (for autotext)
  - `html/` - HTML import/export
  - `rtf/` - RTF filter wrapper
  - `writer/` - Native Writer format
  - `xml/` - ODF import/export (delegates to xmloff/)
- Key files: Each format has reader and writer classes
- Committed: Yes | Generated: No

**sw/source/uibase/** and **sw/source/ui/**
- Purpose: Writer user interface and toolbar/menu handling
- Contains:
  - `uibase/` - Always-loaded UI (main UI layer)
  - `ui/` - Optional UI parts loaded on demand (swui library)
- Key files: Menu/toolbar definitions, dialog implementations
- Committed: Yes | Generated: No

**framework/**
- Purpose: Modern UNO-based framework for UI configuration and command dispatch
- Contains: Menu/toolbar XML parsing, UNO UI control services, command dispatch
- Key files: `framework/source/` (UNO service implementations)
- Committed: Yes | Generated: No
- Used for: Loading UI from `uiconfig/` XML files, modern command routing

**sfx2/source/**
- Purpose: Legacy SFX2 framework and document infrastructure
- Contains:
  - `appl/` - Application-level shell infrastructure
  - `doc/` - Document handling (SfxMedium load/save, document shell base)
  - `view/` - View infrastructure (SfxViewFrame, SfxViewShell)
  - `dialog/` - Dialog infrastructure, Startcenter
  - `control/` - SlotID dispatch system, toolbar/menu mechanics
- Key files: `sfx2/source/doc/docfile.cxx` (SfxMedium - load/save logic)
- Committed: Yes | Generated: No

**configmgr/** and **officecfg/**
- Purpose: Configuration system for application settings
- Contains:
  - `configmgr/` - Registry core and backend
  - `officecfg/registry/schema/` - Configuration node definitions
  - `officecfg/registry/data/` - Default configuration values
- Key files: `configmgr/source/` (service implementation)
- Committed: Yes | Generated: No

**desktop/source/app/**
- Purpose: Application startup and shell infrastructure
- Contains: `soffice` binary entry point, bootstrap, Desktop class
- Key files: `main.c` (entry point), `sofficemain.cxx` (soffice_main() function), `app.cxx` (Desktop class)
- Committed: Yes | Generated: No

**offapi/**
- Purpose: LibreOffice public UNO API definitions
- Contains: IDL/XML interface and service definitions under `com/sun/star/` hierarchy
- Key files: Service definitions (e.g., `offapi/com/sun/star/text/TextDocument.idl`)
- Committed: Yes | Generated: No
- Importance: Third-party developers extend via SDK

**uiconfig/**
- Purpose: XML configuration for menus, toolbars, sidebars, and keybindings
- Contains: Module-specific UI definitions (one per application)
- Key files: `uiconfig/modules/*/ui/` (menu/toolbar XML), `uiconfig/modules/*/menubar.xml`
- Committed: Yes | Generated: No
- Loaded by: framework module at runtime via XML parsing

**pch/**
- Purpose: Precompiled headers to speed compilation
- Contains: `inc/pch/precompiled_*.hxx` for each major module
- Key files: `pch/inc/pch/` (shared precompiled headers)
- Committed: Yes | Generated: No

**writerfilter/** and **odffilter/**
- Purpose: Word/DOCX and ODF format handling (shared/reusable across applications)
- Contains: OOXML/ODF grammar-based filter implementations
- Key files: `source/dmapper/` (OOXML semantic converter)
- Committed: Yes | Generated: No
- Used by: Writer (`sw/source/filter/docx/` wraps this), Calc, Draw

**connectivitiy/** and **dbaccess/**
- Purpose: Database connectivity and UI
- Contains: ODBC bridges, JDBC bridges, SQL parser, database UI dialogs
- Committed: Yes | Generated: No

**i18npool/**
- Purpose: Internationalization and localization support
- Contains: Locale data, collation algorithms, text analysis
- Committed: Yes | Generated: No

## Key File Locations

**Entry Points:**
- `desktop/source/app/main.c` - C entry point that calls SAL_IMPLEMENT_MAIN macro
- `desktop/source/app/sofficemain.cxx:soffice_main()` - Main application setup and event loop entry
- `include/vcl/svmain.hxx:SVMain()` - VCL main loop function called by soffice_main()

**Configuration:**
- `config_host/` - Generated build configuration (created by configure script)
- `.config/` - IDE/tool configurations (.clangd, .vscode settings)
- `.clang-format` - Code formatting rules for clang-format
- `.editorconfig` - Editor configuration for universal editors
- `aclocal.m4` - Autotools macro definitions

**Core Logic - Writer:**
- `sw/inc/doc.hxx` - SwDoc class definition (central document model)
- `sw/source/core/doc/` - SwDoc implementation
- `sw/source/core/layout/` - Layout engine (page/frame rendering)
- `sw/source/core/text/` - Text node and text formatting
- `sw/source/core/crsr/` - Cursor and selection

**Core Logic - Calc:**
- `sc/inc/document.hxx` - ScDocument class definition
- `sc/source/core/` - Spreadsheet core model

**Core Logic - Drawing:**
- `svx/source/svdraw/` - SdrObject and SdrView implementation
- `sd/source/core/` - Impress-specific logic

**Testing:**
- `sw/qa/` - Writer tests (cppunit)
- `sc/qa/` - Calc tests
- `sd/qa/` - Draw/Impress tests
- Pattern: `qa/cppunit/` for C++ unit tests; `qa/complex/` for integration tests; `qa/unoapi/` for UNO API tests

## Naming Conventions

**Files:**
- Headers: `.hxx` extension (C++ headers); `.h` for C headers or legacy
- Implementation: `.cxx` extension (C++); `.c` for C
- IDL: `.idl` extension (UNO interface definitions)
- Tests: `*_test.cxx` or `*Test.cxx` for individual tests; `*_test.cxx` preferred
- Configuration: `.xml` for UI config, `.conf` for settings

**Directories:**
- Module folders: lowercase, no underscores (e.g., `sw`, `sc`, `drawinglayer`)
- Functional subdirs: `source/` (implementation), `inc/` (headers), `qa/` (tests), `util/` (build scripts), `uiconfig/` (UI)
- Class/domain subdirs (large modules): named by domain (e.g., `sw/source/core/text/`, `sw/source/core/layout/`)
- Platform backends: lowercase with platform name (e.g., `vcl/win/`, `vcl/osx/`, `vcl/unx/`)

**Classes & Types:**
- C++ classes: PascalCase (e.g., `SwDoc`, `SdrObject`, `OutputDevice`)
- Interfaces: Start with `X` (UNO convention, e.g., `XInputStream`, `XComponent`)
- Enums: All caps with underscore (e.g., `SwTextAttrMode`, `SdrTextAniKind`)
- Legacy "Sw" prefix convention: Writer classes prefixed with `Sw` (e.g., `SwNode`, `SwTextNode`)

**Functions & Methods:**
- PascalCase (e.g., `GetLength()`, `SetValue()`)
- Getters: `Get*()` convention; setters: `Set*()` convention
- Private methods: Prefix with `Impl` (e.g., `ImplLayout()`)

**Variables & Members:**
- Local variables: camelCase (e.g., `nCount`, `bIsValid`)
- Member variables: Prefix with `m_` (modern) or `m` (legacy, e.g., `m_nSize` or `mnSize`)
- Boolean members: Prefix with `b` (e.g., `bIsValid`, `bModified`)
- Count/index members: Prefix with `n` (e.g., `nCount`, `nIndex`)
- Pointer members: Prefix with `p` (legacy, e.g., `pNext`, `pDocument`)

**Macros:**
- All caps with underscores (e.g., `SAL_LOG`, `OSL_ENSURE`)
- Module-specific: Prefixed with module name (e.g., `SW_TEXTNODE_`, `VCL_DLLPUBLIC`)

## Where to Add New Code

**New Writer Feature:**
- Document model changes: `sw/source/core/doc/` or `sw/source/core/{domain}/` (e.g., `sw/source/core/text/` for text changes)
- UI changes: `sw/source/uibase/` for always-loaded UI, `sw/source/ui/` for optional UI
- Tests: `sw/qa/cppunit/{domain}/` (mirror source structure)
- UI config: Add to `sw/uiconfig/modules/swriter/ui/` (menus/toolbars)
- Filter support: `sw/source/filter/{format}/` if Writer-specific, otherwise update `writerfilter/` or `xmloff/`

**New Calc Feature:**
- Core model: `sc/source/core/` (follows similar structure to Writer)
- UI: `sc/source/ui/`
- Tests: `sc/qa/`
- Shared logic: Consider `sc/source/` subdirectories by domain

**New Calc Component / Library:**
- Helper library: `libcomp/` or similar (not present currently)
- UNO service: Implement in module-specific directory, register in `offapi/` or module's util/ dir
- Config: `officecfg/registry/` (if it's a user setting)

**Utilities / Shared Code:**
- Cross-module helpers: `comphelper/` (if UNO-related) or `tools/` (if basic utilities - but prefer comphelper)
- Graphics/rendering: `basegfx/` (math), `drawinglayer/` (primitives)
- Text handling: `editeng/` (already reused by Writer, Calc, Draw)

**Filters for New Format:**
- ODF/XML: Add to `xmloff/` (shared) or `odffilter/` (ODF-specific)
- Microsoft Office: Add to `writerfilter/` (OOXML parser)
- Other formats: Create format-specific module if widely used, otherwise add to app-specific filter directory

**UI Configuration:**
- Menus: `{app}/uiconfig/modules/{modulename}/menubar.xml`
- Toolbars: `{app}/uiconfig/modules/{modulename}/toolbar/*.xml`
- Dialogs: `.ui` files in `{app}/uiconfig/modules/{modulename}/ui/`
- Images: `{app}/uiconfig/modules/{modulename}/res/` or use icon theme system

**Tests:**
- Unit tests: Co-locate with source code in `qa/cppunit/`
- Integration tests: `qa/complex/`
- UNO API tests: `qa/unoapi/`
- Naming: `{FeatureName}Test.cxx` or `{feature}_test.cxx`

## Special Directories

**bin/**
- Purpose: Build output - compiled tools and generated files
- Generated: Yes | Committed: No
- Contents: `soffice` executable (after build), intermediate object files, generated code

**workdir/**
- Purpose: Build working directory
- Generated: Yes | Committed: No
- Contents: Object files, libraries (.a, .so, .lib), generated source files

**instdir/**
- Purpose: Installation staging directory (what gets packaged)
- Generated: Yes | Committed: No
- Contents: Copied binary, libraries, resources, configuration files ready for distribution

**config_host/**
- Purpose: Generated build configuration from autotools
- Generated: Yes | Committed: No
- Contents: `config.h` with platform/feature defines

**.planning/codebase/**
- Purpose: GSD planning documents generated during analysis
- Generated: Yes (by GSD commands) | Committed: Yes
- Contents: ARCHITECTURE.md, STRUCTURE.md, CONVENTIONS.md, TESTING.md, CONCERNS.md, STACK.md, INTEGRATIONS.md

**.config/**
- Purpose: IDE and development tool configuration
- Generated: No | Committed: Yes
- Contents: `.clangd` (clangd LSP config), IDE workspaces

**.git-hooks/**
- Purpose: Custom git hooks for development workflow
- Generated: No | Committed: Yes
- Contents: Hook scripts for pre-commit checks

**autom4te.cache/**
- Purpose: Autotools cache for faster reconfiguration
- Generated: Yes | Committed: No
- Delete safely: `rm -rf autom4te.cache/`

---

*Structure analysis: 2026-02-09*
