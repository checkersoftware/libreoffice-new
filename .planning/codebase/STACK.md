# Technology Stack

**Analysis Date:** 2026-02-09

## Languages

**Primary:**
- C++ - Core LibreOffice application, UI components, document processing, and graphics
- C - System-level libraries and low-level components
- UNO/IDL - Component interface definitions for the UNO (Universal Network Objects) framework
- Java - Database connectivity drivers, report builder, accessibility, and optional scripting support
- Python - Optional scripting extensions and testing utilities
- JavaScript - Optional web component support and JDialog integration

**Secondary:**
- Perl - Build system scripts (`autogen.sh`)
- Make/Shell - Build configuration and system setup
- Rust - Experimental UNO support (optional)

## Runtime

**Environment:**
- Cross-platform: Windows, macOS, Linux, Android, iOS, WebAssembly (Emscripten)
- Minimum: C++11 compatible compilers (GCC, Clang, MSVC)
- Optional Java Runtime Environment (JRE) for Java features

**Package Manager:**
- Built-in tarball management for external dependencies
- System package manager integration (pkg-config, autotools)
- No traditional Node/npm or pip dependencies at root level

## Frameworks

**Core Application Framework:**
- UNO (Universal Network Objects) - Component model providing plugin architecture and runtime services
- SAL (Segment Abstraction Layer) - Platform abstraction layer for system-level operations
- VCL (Visual Class Library) - Widget toolkit and graphics rendering

**UI/Graphics:**
- Cairo - 2D graphics rendering
- OpenGL - Optional GPU-accelerated rendering
- Skia - Graphics library (bundled in external/)
- GTK3/GTK4 - Native Linux UI rendering (optional)
- Qt5/Qt6 - Native UI toolkit support (optional)
- KDE Frameworks 5/6 - Optional desktop integration

**Document Processing:**
- ODF (Open Document Format) - Native document format support
- Microsoft Office formats (DOCX, XLSX, PPTX) - Via filters
- Legacy ODF XML support - Core to document model

**Testing:**
- CppUnit - C++ unit testing framework
- JUnit - Java testing framework
- Google Test - C++ testing support

**Build System:**
- GNU Autotools (autoconf, automake, libtool) - Main build configuration
- gbuild - LibreOffice-specific build system
- Buck - Build system for modular compilation (optional)

## Key Dependencies

**Critical Infrastructure:**
- Boost - General-purpose C++ libraries (templates, filesystem, threading)
- ICU (International Components for Unicode) - Unicode and internationalization support
- libxml2 - XML parsing and processing
- libxslt - XSLT transformation support
- OpenSSL - Cryptography and TLS/SSL support (or NSS as alternative)
- zlib - Compression library
- bzip2 - Compression support

**Database & Connectivity:**
- libpq (PostgreSQL client) - PostgreSQL SDBC driver support
- MySQL/MariaDB client libraries - Database connectivity driver
- ODBC - Optional database abstraction layer
- Firebird - Optional embedded database support
- HSQLDB - Java-based database component

**Document & Format Support:**
- FreeType - Font rasterization
- HarfBuzz - Text shaping and complex script rendering
- Graphite - Advanced text layout
- LibJPEG - JPEG image support
- LibPNG - PNG image support
- ImageMagick/GraphicsMagick - Image processing
- Hunspell - Spell checking
- Hyphen - Hyphenation support
- LibMythes - Thesaurus support
- MD4C - Markdown parsing

**Cryptography & Security:**
- NSS (Network Security Services) - Alternative cryptography backend
- GSSAPI - Kerberos authentication
- libldap - LDAP directory support
- libassuan - GnuPG cryptography support

**Media & Multimedia:**
- Avmedia module - Media playback support
- FFmpeg - Multimedia codecs (optional)

**Embedded & Extended:**
- Java Websocket - WebSocket support for Java components
- BeanShell - Java scripting engine
- Report Builder - Optional business intelligence component
- Libnumbertext - Number-to-text conversion

**Physics Simulation (optional):**
- Box2D - 2D physics engine

**Code Analysis & QA:**
- clang-format - Code formatting
- clang-cl - MSVC compatibility layer for Clang
- CppCheck - Static analysis (external)
- Clang plugins - Custom compiler plugins for code validation

## Configuration

**Environment:**
- Configuration via `autogen.sh` script with options file
- `autogen.input` or `autogen.lastrun` for persistent configuration
- Distro-specific configs in `distro-configs/` directory
- Environment variables for tool paths (ACLOCAL, AUTOCONF, LODE_HOME)

**Build Configuration:**
- `configure.ac` - Main autoconf input defining all configurable options
- Platform-specific settings via `--host`, `--build` flags
- Optional feature toggles: `--enable-python`, `--enable-database-connectivity`, `--enable-gtk4`, etc.
- Architecture selection: x86, x64, ARM, ARM64, WebAssembly

**Development Configuration:**
- `.clang-format` - Code style formatting rules
- `.editorconfig` - Editor-agnostic formatting preferences
- `.cspell/` - Spell check configuration
- `m4/` directory - Autoconf macro library including platform detection

## Platform Requirements

**Development:**
- GNU Autotools suite (autoconf >= 2.68, aclocal, automake)
- C++ compiler (GCC, Clang, or MSVC)
- pkg-config for dependency detection
- Perl (for `autogen.sh`)
- Make (for building)
- Optional: Java Development Kit (JDK) for Java support
- Optional: Python interpreter for scripting and testing

**Production/Deployment:**
- Windows: Standalone executable with bundled libraries
- macOS: Application bundle with embedded frameworks
- Linux: Distributed via distro packages with system library linking
- Android: APK with bundled native libraries
- iOS: Application bundle
- WebAssembly: Browser with WASM/Emscripten support

**Optional Subsystems:**
- X11 with extensions (Linux desktop)
- Wayland (Linux, modern)
- Cocoa (macOS)
- Win32 API (Windows)
- Android SDK/NDK (Android builds)
- iOS SDK (iOS builds)

---

*Stack analysis: 2026-02-09*
