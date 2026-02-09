# Architecture

**Analysis Date:** 2026-02-09

## Pattern Overview

**Overall:** LibreOffice is a large, multi-layered monolithic office suite with a plugin-based component architecture built on UNO (Universal Network Objects). The architecture separates platform-specific code from portable logic through abstraction layers, and uses a modular dependency structure where lower-level libraries are depended upon by higher-level applications.

**Key Characteristics:**
- Layered architecture with clear separation: System Abstraction Layer (SAL) at the base, followed by rendering (VCL, Canvas), graphics (basegfx, drawinglayer), document models, and applications (Writer/Calc/Draw)
- Plugin-based component system using UNO for extensibility and inter-component communication
- Historical blend of two frameworks: Legacy SFX2 framework (used by Writer, Calc, Draw) and newer UNO-based framework (used by newer components)
- Platform abstraction: Multiple backend implementations for VCL (Windows, macOS, Linux/X11, Android, iOS, Emscripten) with factory pattern (SalInstance)
- Event-driven dispatch system based on SlotIDs in legacy framework, UNO method dispatch in newer framework

## Layers

**System Abstraction Layer (SAL):**
- Purpose: Platform-independent interface to OS services (threading, processes, IPC, dynamic loading, memory management)
- Location: `sal/`
- Contains: C API for OS abstraction, platform-specific implementations (`sal/osl/` for OS layer, `sal/rtl/` for platform-independent strings)
- Depends on: Native OS libraries (Windows API, POSIX, macOS/iOS/Android SDKs)
- Used by: All higher layers depend on SAL for basic OS functionality

**Graphics Math & Types Layer:**
- Purpose: Provides fundamental data types and algorithms for graphics (polygons, vectors, matrices, colors)
- Location: `tools/`, `basegfx/`
- Contains: Rectangle, Color, Point, Vector, Matrix, Polygon classes; geometric algorithms
- Depends on: SAL
- Used by: VCL, Canvas, DrawingLayer, Rendering backends

**System Abstraction Layer - Rendering (VCL):**
- Purpose: Cross-platform widget toolkit and rendering abstraction for GUI windows, controls, and output devices
- Location: `vcl/`
- Contains: Window system abstractions, device context abstraction (OutputDevice), font handling, metafile recording, platform backends
- Depends on: SAL, basegfx, tools
- Used by: Framework, applications (Writer, Calc, Draw), Canvas, DrawingLayer
- Key files: `vcl/inc/svdata.hxx` (global state), `vcl/inc/` (cross-platform public API)
- Platform backends: `vcl/win/` (Windows), `vcl/osx/` (macOS), `vcl/quartz/` (iOS), `vcl/android/`, `vcl/unx/` (Linux/X11: gtk3, gtk4, kf5, kf6, generic), `vcl/headless/` (bitmap backend)

**UNO Runtime (CPPU/URE):**
- Purpose: Binary UNO runtime - type system, bridge implementations for C++/Java/Python interoperability
- Location: `cppu/`, `cppuhelper/`, `binaryurp/`, `bridges/`
- Contains: Type marshalling, ABI definitions, binary protocol implementations, language bridges
- Depends on: SAL
- Used by: Framework, all UNO components, scripting

**Component Helper Libraries:**
- Purpose: Reusable utilities for implementing UNO components and services
- Location: `comphelper/`, `svl/` (StarView Library - legacy), `editeng/` (text editing engine)
- Contains: Basic component base classes, property handling, smart pointers, configuration helpers
- Depends on: SAL, UNO runtime
- Used by: Document models, filters, UI components

**Canvas & Modern Graphics:**
- Purpose: UNO-based successor to VCL for rendering with modern graphics API access and double-buffering
- Location: `canvas/`, `cppcanvas/`
- Contains: Cairo-based canvas backend (primary backend), UNO interfaces (`css::rendering::XCanvas`)
- Depends on: SAL, basegfx, VCL, CPPU
- Used by: Slideshow, EMF+ rendering, drawinglayer in some contexts

**Drawing Primitives (DrawingLayer):**
- Purpose: Display-list based drawing abstraction with hierarchy of primitives and processors
- Location: `drawinglayer/`
- Contains: Primitive2D class hierarchy (PolyPolygonHatchPrimitive2D, etc.), processor hierarchy (VclPixelProcessor2D, VclMetafileProcessor2D, etc.)
- Depends on: SAL, basegfx, VCL, comphelper
- Used by: SdrObjects, SdrView, rendering pipelines, custom widgets
- Key pattern: Primitives describe what to draw; processors render them to different targets (screen, metafile, PDF, etc.)

**SVX (Drawing Model Helper):**
- Purpose: Shared drawing model code used by both Draw and Impress
- Location: `svx/`
- Contains: SdrObject hierarchy (rectangles, circles, text objects, 3D objects), SdrModel, SdrView, SdrPageView, selection rendering
- Depends on: SAL, basegfx, drawinglayer, VCL, editeng, UNO runtime
- Used by: Draw (`sd/`), Impress (`sd/`), inherited patterns in other modules

**Legacy Framework (SFX2):**
- Purpose: Old document framework with dispatch system, document model base, load/save logic
- Location: `sfx2/`
- Contains: SfxShell (dispatch target), SlotID-based command routing (SDI files), SfxMedium (load/save), document properties, online help
- Depends on: SAL, VCL, editeng, UNO runtime, comphelper
- Used by: Writer, Calc, Draw (all three share this framework)
- Key pattern: SlotIDs identify actions, dispatched to SfxShell instances in a chain of responsibility

**Modern UNO Framework:**
- Purpose: UNO-based framework for menus, toolbars, accelerators, and command dispatch
- Location: `framework/`
- Contains: UNO service implementations for UI control (`css::ui::XUIElement`), menu/toolbar XML parsing, accelerator handling, command dispatch
- Depends on: SAL, VCL, UNO runtime, comphelper, tools
- Used by: UI configuration loading from `uiconfig/` files, modern components

**Text Editing Engine:**
- Purpose: Shared text editing, formatting, and spell-checking logic
- Location: `editeng/`
- Contains: EditorShell, spell checking, text attributes, text formatting, paragraph properties
- Depends on: SAL, VCL, tools, basegfx, UNO runtime
- Used by: Writer, Calc, Draw/Impress (text in shapes), forms

**Filters & File I/O:**
- Purpose: Import/export for various document formats
- Location: `filter/`, `writerfilter/`, `odffilter/`, `xmloff/`, `sw/source/filter/`, etc.
- Contains: Filter implementations for DOC, DOCX, XLS, XLSX, PDF, EMF, WMF, SVG, ODF, HTML, RTF, ASCII
- Depends on: SAL, VCL, UNO runtime, document models, editeng, basegfx
- Used by: Document loading/saving pipelines

**Document Applications:**

**Writer (Text Editor):**
- Location: `sw/` (S=Script, W=Writer, historical naming)
- Contains: Core document model (`SwDoc`, `SwNode`, `SwTextNode`), layout engine, text attributes, undo/redo
- Depends on: SAL, basegfx, drawinglayer, VCL, editeng, sfx2, filter, comphelper, UNO runtime
- Key files: `sw/inc/doc.hxx` (central document class), `sw/source/core/doc/` (core logic), `sw/source/core/layout/` (layout engine), `sw/source/uibase/` (UI layer)
- Architecture: Core + UI + Filters separated; filters in `sw/source/filter/` (HTML, RTF, WW8, ODF)

**Calc (Spreadsheet):**
- Location: `sc/`
- Depends on: SAL, basegfx, drawinglayer, VCL, editeng, sfx2, formula engine, comphelper, UNO runtime
- Architecture: Similar to Writer but with cell model instead of text nodes

**Draw & Impress (Drawing & Presentations):**
- Location: `sd/` (S=Script, D=Draw, later used for Impress)
- Depends on: SAL, basegfx, drawinglayer, VCL, svx, sfx2, editeng, canvas (for slideshow), comphelper, UNO runtime
- Architecture: Reuses SdrObject model from svx; slideshow handled separately in `slideshow/` module

**UI Shell Applications:**
- Location: `desktop/`
- Contains: `soffice` binary entry point, application bootstrap, command-line parsing, splash screen
- Depends on: SAL, VCL, framework, sfx2, UNO runtime
- Key files: `desktop/source/app/sofficemain.cxx` (main entry point), `desktop/source/app/app.cxx` (Desktop class with initialization)

**Configuration System:**
- Purpose: Hierarchical configuration registry for application settings
- Location: `configmgr/`, `officecfg/`
- Contains: Configuration node tree, schema definitions, backend storage
- Depends on: SAL, UNO runtime, comphelper
- Used by: All modules for settings/preferences

## Data Flow

**Document Loading Pipeline:**

1. User opens file or programmatic API call to `SfxMedium::GetInStream()`
2. Filter selection based on file extension or explicit filter name
3. Filter implementation (e.g., `writerfilter::WordprocessingMLFilter` for DOCX) reads from stream
4. Filter creates/populates document model objects (e.g., `SwDoc` for Writer) via UNO interfaces or direct C++ API
5. Document model publishes change notifications to listeners (`XModifyListener`)
6. View(s) listen to changes and trigger layout/rendering

**Rendering & Display Pipeline:**

1. Window invalidation triggers `Window::Paint()` in VCL
2. Application renders to OutputDevice (e.g., screen device)
3. For documents with DrawingLayer objects:
   - SdrObjects generate `Primitive2DSequence` display list
   - `VclPixelProcessor2D` processes primitives and draws to OutputDevice
4. For printing/export to PDF:
   - Setup a GDIMetafile-recording OutputDevice
   - Application renders as normal
   - `VclMetafileProcessor2D` records drawing commands to GDIMetafile
   - GDIMetafile replayed or converted to PDF via `vcl::PDFWriter`

**Command Dispatch (Legacy Framework via SFX2):**

1. User action (menu, toolbar, accelerator, API) triggers dispatch with SlotID
2. SlotID routed through SfxShell chain (application-specific shells down to base shells)
3. `SfxShell::Execute()` handler in appropriate shell processes command
4. Command may modify document model, which sends notifications
5. Notifications trigger view updates

**Command Dispatch (Modern Framework via UNO):**

1. Command dispatch request via `css::frame::XDispatchProvider`
2. Command URL parsed (e.g., `.uno:Save`)
3. Service lookup in framework locates handler
4. Handler executes via UNO method call
5. Same notification/update cycle

**State Management:**

- Document modifications tracked via `SwDoc::GetIDocumentUndoRedo()` (undo stack)
- View state maintained in SfxViewShell subclasses
- Configuration read via `ConfigManager` singleton
- Clipboard/drag-drop via UNO DataTransfer interfaces

## Key Abstractions

**OutputDevice:**
- Purpose: Rendering abstraction for all drawing operations
- Location: `vcl/inc/vcl/outdev.hxx`
- Implementation: `vcl/source/outdev/` with method dispatch to platform backend
- Pattern: Manages graphics state (clipping, font, color), queues drawing commands to backend

**SalInstance / SalFrame / SalDevice:**
- Purpose: Platform-specific implementations of GUI elements
- Location: Backend directories (`vcl/win/`, `vcl/osx/`, etc.)
- Pattern: Factory creates SalInstance → creates SalFrames/SalDevices → delegates OS calls
- Key: VCL's `InitVCL()` calls `CreateSalInstance()` to load platform backend

**SdrObject & SdrView:**
- Purpose: Shape model and shape viewer
- Location: `svx/source/svdraw/`
- Pattern: Model (SdrObject hierarchy) separate from view (SdrView); page concepts via SdrPage/SdrModel

**SwDoc / IDocument* Managers:**
- Purpose: Writer document model with split responsibilities
- Location: `sw/inc/doc.hxx`
- Pattern: Facade over manager interfaces (IDocumentUndoRedo, IDocumentMarkAccess, IDocumentStylePoolAccess, etc.)
- Rationale: Reduces monolithic class size while maintaining single entry point

**Primitive2D & Processor2D:**
- Purpose: Render-target-agnostic drawing abstraction
- Location: `drawinglayer/source/`
- Pattern: Composite pattern for primitives; visitor pattern for processors
- Benefit: Same drawing code can render to screen (pixel), metafile, or PDF without change

**UNO Service & Interface:**
- Purpose: Component contract and implementation
- Location: `offapi/` (interface definitions), module-specific implementations
- Pattern: Interface definition (XML or IDL) → C++ XInterface subclass → Service implementation
- Example: `css::io::XInputStream` implemented by various file readers

**SlotServer / SlotItem (Legacy):**
- Purpose: SlotID → handler mapping in SFX2
- Location: `sfx2/source/control/`
- Pattern: SDI files compiled to C++ mapping tables; SfxShell subclasses override handlers
- Rationale: Compile-time slot ID generation avoids string lookups

## Entry Points

**soffice Binary:**
- Location: `desktop/source/app/main.c` → `sofficemain.cxx`
- Triggers:
  - `sal_detail_initialize()` - SAL initialization
  - `desktop::Desktop` constructor - application bootstrap, UNO service manager setup
  - `SVMain()` - VCL main loop entry, window system initialization, event loop
- Responsibilities: Command-line parsing, VCL/UNO initialization, document/window management, shutdown

**Document Open/Create:**
- Location: `sfx2/source/doc/docfile.cxx` (SfxMedium) → filter dispatch → document factory
- Responsibilities: Stream handling, filter selection, error recovery, recent documents

**View/Window Creation:**
- Location: `sfx2/source/view/viewfrm.cxx` (SfxViewFrame) → application-specific view classes
- Responsibilities: Connecting document model to visual representation, toolbar/menu setup

## Error Handling

**Strategy:** Multi-level error reporting with user-facing recovery

**Patterns:**
- C++ exceptions bubble up to top-level exception handlers in `SVMain()` (VCL main loop)
- UNO exceptions converted to `css::uno::Exception` subtypes for API callers
- File I/O errors caught by `SfxMedium` with user dialog options (retry, skip, cancel)
- Crash reporting via breakpad (if enabled) sends anonymous crash signatures
- Undo/Redo system records pre-image for rollback on operations

## Cross-Cutting Concerns

**Logging:**
- Mechanism: `SAL_LOG` environment variable (set in sal/source/)
- Usage: `SAL_LOG=+INFO.module.area` enables debug logging in specific modules
- Examples: `SAL_LOG=+INFO.cppcanvas.emf` for EMF+ rendering logs
- Implementation: Debug build outputs to stderr; release builds disabled via macro

**Validation:**
- Content validation: Filters validate document structure on load
- API validation: UNO interfaces validate arguments in method implementations
- UI validation: Forms engine validates cell/field input

**Authentication:**
- File passwords: `SfxMedium` handles password prompts for encrypted formats
- Database: `dbaccess/` module implements database connection authentication
- Network: Proxy/auth dialogs in HTTP-related code

**Threading:**
- Main constraint: Most code not thread-safe due to historical C++ design
- Solution: `SolarMutex` (global big kernel lock) - recursive mutex held during UNO method execution
- Usage: `SolarMutexGuard` in UNO method implementations
- Pattern: Acquire before GUI access, release before calling listeners to avoid deadlock

**Serialization:**
- File formats: ODF (XML), Office Open XML (DOCX/XLSX), legacy binary (DOC)
- Mechanisms: Filter factories instantiate reader/writer implementations
- Metadata: `SfxDocumentInfo` / `DocumentProperties` UNO service for doc title/author/etc.

---

*Architecture analysis: 2026-02-09*
