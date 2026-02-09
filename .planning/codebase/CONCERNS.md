# Codebase Concerns

**Analysis Date:** 2026-02-09

## Tech Debt

**MD2 and MD5 Digest Implementations:**
- Issue: Multiple cryptographic digest algorithms (MD2, MD5, and all HMAC variants using these) explicitly marked with `@deprecated The implementation is buggy and generates incorrect results` in their function declarations
- Files: `include/rtl/digest.h` - Lines 294, 303, 315, 327, 342, 373, 382, 393, 405, 420, 522, 531, 542, 560, 572, 587, 615
- Impact: Any code using `rtl_digest_createMD2()`, `rtl_digest_updateMD2()`, `rtl_digest_getMD2()`, `rtl_digest_createMD5()`, `rtl_digest_updateMD5()`, `rtl_digest_getMD5()`, and HMAC variants will produce cryptographically incorrect results. This is a critical correctness issue for any security-related digest operations.
- Fix approach: Replace with SHA1 or SHA256 implementations for all digest operations, or migrate to modern cryptographic libraries. Update all call sites that depend on MD2/MD5 digest functions.

**Unsafe Legacy String Functions in Build Tools:**
- Issue: Build system tools (soltools/mkdepend, soltools/cpp) use unsafe C string functions: `strcpy()`, `sprintf()` without bounds checking
- Files: `soltools/mkdepend/main.c`, `soltools/mkdepend/cppsetup.c`, `soltools/mkdepend/include.c`, `soltools/mkdepend/pr.c`, `soltools/cpp/_macro.c`, `soltools/cpp/_include.c`
- Impact: Buffer overflow vulnerabilities in build tools. While these are not runtime issues for end users, they create security risks during the build process itself and could be exploited if processing untrusted input.
- Fix approach: Replace `strcpy()` with `strncpy()` or modern alternatives. Replace `sprintf()` with `snprintf()`. Consider refactoring to use modern C++ string classes if codebase permits.

**Obsolete System Compatibility Code:**
- Issue: `soltools/mkdepend/imakemdep.h` contains `/* FIXME: strange list of obsolete systems */` comment indicating dead code for systems no longer in use
- Files: `soltools/mkdepend/imakemdep.h` (lines marked as FIXME)
- Impact: Maintains support for obsolete platforms that no longer exist, increasing maintenance burden and code complexity without benefit
- Fix approach: Remove obsolete system configuration blocks and consolidate to actively supported platforms only.

**Missing Backtrace Implementation for Some Platforms:**
- Issue: Backtrace functions in `sal/osl/unx/` return `NULL` with `/*TODO*/` comments for BSD, Solaris, and other Unix variants
- Files: `sal/osl/unx/backtrace_other.c`, `sal/osl/unx/backtrace_solaris.c`, `sal/osl/unx/backtrace_bsd.c`
- Impact: Debugging and error reporting functionality is non-functional on these platforms, making troubleshooting production issues extremely difficult
- Fix approach: Implement proper backtrace handling using platform-specific APIs for each Unix variant, or use libunwind as a portable solution

## Known Bugs

**UBSan Invalid Bool Load (Recent Fix):**
- Symptoms: Runtime error loading invalid boolean values - "load of value 190, which is not a valid value for type 'bool'"
- Files: `sw/source/core/doc/tblafmt.cxx` line 272 (previously)
- Trigger: Writer table auto format operations, especially undo/redo operations in table style make
- Status: Fixed in commit 9a110a047aaa (Feb 5, 2026) but indicates broader memory layout issues
- Workaround: Not needed - fixed upstream
- Related: Look for similar bool assignment issues in assignment operators copying packed structs

**Print Context Not Available for Infoprinter:**
- Issue: Real printer context is not retrievable for infoprinter operations on macOS
- Files: `vcl/inc/osx/salprn.h` - marked with `/// FIXME: get real printer context for infoprinter if possible`
- Impact: macOS print preview/info operations may display incorrect printer information
- Trigger: Requesting printer information on macOS before print operation
- Workaround: Use system printer dialogs instead of relying on infoprinter context

**Spinbox Position Misalignment on macOS:**
- Issue: Spinboxes positioned one pixel to the right with size misalignment in VCL on macOS
- Files: `vcl/inc/osx/salnativewidgets.h`
- Impact: Minor visual misalignment of spinbox controls on macOS - cosmetic issue
- Trigger: Rendering spinbox controls in VCL on macOS
- Workaround: None - minor visual issue

## Security Considerations

**Deprecated Hash Algorithms in Cryptography:**
- Risk: MD2, MD5 digest implementations explicitly marked as buggy and broken. No safeguards prevent their use for security-critical operations
- Files: `include/rtl/digest.h` (all MD2/MD5 function declarations)
- Current mitigation: Deprecation notices in documentation only - functions remain available and callable
- Recommendations:
  - Enforce compile-time warnings or errors when deprecated digest functions are used
  - Audit all callers to ensure migration away from MD2/MD5
  - Consider completely removing MD2/MD5 implementations if they're only used for legacy compatibility

**Unsafe String Handling in Build Tools:**
- Risk: Buffer overflows in preprocessing and dependency generation tools could allow code injection during build
- Files: `soltools/mkdepend/*`, `soltools/cpp/*`
- Current mitigation: Tools are used on trusted build inputs, but no input validation exists
- Recommendations:
  - Add bounds checking or use safe string functions
  - Add input validation and size limits
  - Consider fuzzing build tools with malformed inputs

**Legacy C API Unsafe Patterns:**
- Risk: Public C APIs use void pointers and manual memory management without strong type safety
- Files: `include/rtl/digest.h`, `include/osl/file.h`, and other SAL public APIs
- Current mitigation: Documentation requirements for callers
- Recommendations:
  - Consider C++ wrapper classes with RAII for critical APIs
  - Add helper functions to prevent common misuse patterns

## Performance Bottlenecks

**Unoptimized Print Dialog Context Building on macOS:**
- Problem: Real printer information context cannot be retrieved for print info dialogs
- Files: `vcl/inc/osx/salprn.h`
- Cause: Missing implementation for retrieving actual printer context
- Improvement path: Implement proper system printer enumeration and context retrieval using macOS APIs

**Minimal Screen Refresh Optimization Not Implemented in Quartz:**
- Problem: Full screen refresh instead of minimal changed rectangle for Quartz graphics operations
- Files: `vcl/inc/quartz/salgdi.h` - marked with `// TODO: refresh minimal changerect`
- Cause: Complete area refresh instead of dirty rect optimization
- Improvement path: Implement proper dirty rectangle tracking and minimal refresh area optimization for Quartz backend

**UNO Any-to-JSON Conversion Not Implemented:**
- Problem: UNO callback for command results cannot convert complex UNO Any types to JSON
- Files: `include/LibreOfficeKit/LibreOfficeKitEnums.h` - marked with `// TODO "result": "..."  // UNO Any converted to JSON (not implemented yet)`
- Cause: JSON serialization of complex UNO type system values is complex
- Impact: LibreOffice Kit clients cannot receive structured result data from UNO commands
- Improvement path: Implement JSON serializer for UNO Any values, handling complex types recursively

## Fragile Areas

**Table Auto Format Assignment Operator:**
- Files: `sw/source/core/doc/tblafmt.cxx` (assignment operator, particularly around line 272)
- Why fragile: Boolean fields in packed structures with non-zero invalid values causing UBSan errors. Assignment operators must correctly handle all struct members.
- Safe modification: Use proper sanitizer builds when modifying. Ensure all bool members are properly initialized and assigned.
- Test coverage: CppunitTest_sw_uiwriter4 covers some undo/redo scenarios but needs expansion for table format copy operations

**OSX Native Widget Positioning:**
- Files: `vcl/inc/osx/salnativewidgets.h`
- Why fragile: Multiple pixel-level positioning workarounds and special cases for macOS widget rendering
- Safe modification: Test all widget types after changes to native widget handling. Visual regression testing needed.
- Test coverage: Limited - visual tests not automated

**Virtual Method Dispatch in UNO Interface Hierarchy:**
- Files: Multiple across `include/com/sun/star/uno/`
- Why fragile: Complex C++ to UNO bridge with virtual method dispatch and reference counting. Binary compatibility constraints.
- Safe modification: Changes to virtual method order or signatures break binary compatibility with external extensions
- Test coverage: Bridge tests exist but may not cover all extension compatibility scenarios

**macOS-Specific Initialization in SAL:**
- Files: `sal/android/lo-bootstrap.c`, `include/osl/detail/android-bootstrap.h`
- Why fragile: Platform-specific bootstrap code with `TODO` comments indicating incomplete implementations
- Safe modification: Platform-specific changes must be tested on actual target hardware/OS version
- Test coverage: May only be tested on primary development platform

## Scaling Limits

**String Length Limitations:**
- Current capacity: Build tools use buffer allocation with `sprintf()` and `strcpy()` without dynamic sizing
- Limit: File paths longer than fixed buffer sizes (typically 256-1024 bytes) will cause buffer overflows
- Scaling path: Migrate to dynamic string allocation or use std::string throughout build tools

**Memory Management in Digest Operations:**
- Current capacity: Single fixed-size digest objects
- Limit: Streaming large files requires keeping all data in memory before finalizing digest
- Scaling path: Support incremental updates which already exist in API but are poorly documented

## Dependencies at Risk

**MD2 and MD5 Implementations:**
- Risk: Explicitly documented as buggy in current codebase; cryptographic standards have deprecated these algorithms
- Impact: Any security-critical code using these digest functions will fail validation by security audits
- Migration plan: Replace with SHA256/SHA512 or OpenSSL crypto functions. Create compatibility wrapper if needed.

**macOS-Specific Bootstrap and Platform Code:**
- Risk: Platform-specific code for older macOS versions may become incompatible with newer SDKs
- Impact: Build failures on newer macOS or Xcode versions
- Migration plan: Update platform-specific code to use modern macOS APIs, test with latest SDKs

**C89 Build Tools in Modern Environment:**
- Risk: Legacy C build tools (mkdepend, cpp) use C89 conventions incompatible with modern compiler warnings
- Impact: Build failures with stricter compiler flags (e.g., -Werror)
- Migration plan: Modernize C code to C99+, add bounds checking, or replace with Rust/C++ equivalents

## Missing Critical Features

**Printer Context for Info Printing:**
- Problem: Cannot retrieve real printer context for macOS print info operations
- Blocks: Accurate print preview and printer capability detection on macOS

**Backtrace Support on Non-Linux Unix:**
- Problem: Backtrace functions return NULL on BSD, Solaris, and other Unix variants with TODO markers
- Blocks: Meaningful debugging and crash reporting on these platforms

**JSON Serialization for UNO Complex Types:**
- Problem: LibreOfficeKit cannot return structured results from UNO commands
- Blocks: Remote clients from using complex command results

## Test Coverage Gaps

**MD2/MD5 Implementation Testing:**
- What's not tested: Whether buggy implementations are actually used anywhere; impact of deprecation on external code
- Files: `include/rtl/digest.h`, digest implementation files (location not visible in current structure)
- Risk: Migration away from MD2/MD5 could miss hidden dependencies
- Priority: High - security-critical code

**Build Tools Security:**
- What's not tested: Buffer overflow scenarios with long file paths or specially crafted input
- Files: `soltools/mkdepend/*`, `soltools/cpp/*`
- Risk: Build process could be compromised with malicious input
- Priority: Medium - affects build infrastructure

**macOS Platform Code:**
- What's not tested: Widget positioning and rendering on various macOS versions
- Files: `vcl/inc/osx/*`, related implementation files
- Risk: Regression in platform-specific behavior not caught until user reports
- Priority: Medium - affects macOS users

**UNO Bridge Compatibility:**
- What's not tested: Binary compatibility with extensions built against older LibreOffice versions
- Files: `include/com/sun/star/uno/*`, bridge implementation
- Risk: Extension incompatibility after updates
- Priority: Medium - affects extension ecosystem

**Backtrace Implementation for Non-Linux:**
- What's not tested: Debug symbol resolution on BSD/Solaris platforms
- Files: `sal/osl/unx/backtrace_*.c`
- Risk: Silent failures in error reporting with no fallback
- Priority: Low - affects minority of platforms

---

*Concerns audit: 2026-02-09*
