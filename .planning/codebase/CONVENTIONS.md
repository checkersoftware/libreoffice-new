# Coding Conventions

**Analysis Date:** 2026-02-09

## Naming Patterns

**Files:**
- Header files use `.hxx` extension (e.g., `SparklineAttributes.hxx`, `markdata.hxx`)
- Implementation files use `.cxx` extension (e.g., `SparklineGroup.cxx`)
- Header guards use `INCLUDED_[PATH]_[FILENAME]_HXX` format (e.g., `INCLUDED_TEST_SHEET_XSHEETFILTERABLE_HXX`)
- Deprecated guards replaced with `#pragma once` in modern code (observed in `sc/inc/markdata.hxx`)
- Test files typically placed in `*/qa/unit/` or `*/qa/extras/` directories
- Test file names directly correspond to tested classes (e.g., `ESelectionTest.cxx` tests `ESelection`)

**Functions:**
- PascalCase for public methods and classes (e.g., `testCreateFilterDescriptor()`, `GetMarkArea()`)
- camelCase for member function implementation (e.g., `callbackImpl()`)
- Helper functions often use `lcl_` prefix for local/static functions (e.g., `lcl_isEscapedOrFieldEndQuote`)
- Virtual methods use `override` keyword explicitly (e.g., `virtual void setUp() override;`)

**Variables:**
- PascalCase for class names (e.g., `ScMarkData`, `SparklineAttributes`)
- Snake_case with underscore prefix for member variables (e.g., `m_aAttributes`, `m_bMarked`, `m_nInvalidations`)
- Prefix conventions:
  - `m_` for member variables
  - `a` prefix for aggregate types (e.g., `aSelection`, `aMarkRange`)
  - `b` prefix for boolean members (e.g., `bMarked`, `bMultiMarked`)
  - `n` prefix for numeric types (e.g., `nPara`, `nInvalidations`)
  - `p` prefix for pointers (e.g., `pOther`, `pSparklineGroup`)
  - `r` prefix for references (e.g., `rData`, `rSheetLimits`, `rWeakSparklines`)

**Types:**
- PascalCase for class/struct/enum names (e.g., `ESelection`, `SparklineGroup`)
- Enums in PascalCase without prefix (e.g., `SparklineType::Line`, `AxisType::Individual`)
- Type aliases kept in PascalCase (e.g., `MarkedTabsType`)

## Code Style

**Formatting:**
- Tool: clang-format (LibreOffice custom configuration in `solenv/clang-format/`)
- Tab width: 4 spaces
- Indent-tabs-mode: nil (spaces only, not tabs)
- c-basic-offset: 4
- Modeline header in every file: `/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */`
- Modeline footer in every file: `/* vim:set shiftwidth=4 softtabstop=4 expandtab: */`

**Linting:**
- Not explicitly detected in this codebase (no .eslintrc or clang-tidy config at root)
- clang-format enforced with excludelist mechanism (`solenv/clang-format/excludelist`)

## Import Organization

**Order:**
1. Standard C++ headers (e.g., `<memory>`, `<string_view>`, `<set>`, `<vector>`)
2. LibreOffice core headers (relative includes with angle brackets, e.g., `#include <sal/config.h>`)
3. Module-specific headers (relative includes, e.g., `#include "address.hxx"`)
4. UNO API headers (com.sun.star namespace, e.g., `#include <com/sun/star/beans/XPropertySet.hpp>`)
5. Test/utility headers (e.g., `#include <test/bootstrapfixture.hxx>`)

**Path Aliases:**
- Not using CMake-style path aliases; direct relative paths used
- Module-local includes: `#include "markdata.hxx"` (quotes)
- Public includes: `#include <test/bootstrapfixture.hxx>` (angle brackets)
- UNO: `using namespace css;` (com.sun.star abbreviated as css)

## Error Handling

**Patterns:**
- Assertions with `CPPUNIT_ASSERT()` and `CPPUNIT_ASSERT_EQUAL()` for tests
- SAL logging macros: `SAL_WARN()`, `SAL_INFO()`, `SAL_DEBUG()`, `SAL_WARN_IF()`
- Example: `SAL_WARN("sc.ui", "message")` (module tag in quotes, message as parameter)
- No try/catch observed in examined source; exception-based test setup with `UNO_QUERY_THROW`
- Early return pattern common: `if (condition) return;` instead of early throws

## Logging

**Framework:** SAL (Segmentation Abstraction Layer)

**Patterns:**
- `SAL_WARN(module, "message")` - For warnings (e.g., `SAL_WARN("sc.ui", "data overflow")`)
- `SAL_INFO(module, "message")` - For informational logs
- `SAL_DEBUG(module, "message")` - For debug-only logs
- `SAL_WARN_IF(condition, module, "message")` - Conditional warnings
- Module tags follow pattern: `"domain.component"` (e.g., `"sc.ui"`, `"sc"`)
- No console.log equivalents; all logging through SAL

## Comments

**When to Comment:**
- File headers required: License block + modeline (observed in 100% of files)
- Inline comments for non-obvious logic (e.g., `// cancel if multi selection`)
- TODO comments with format: `//! todo:` (observed: `//! todo: It should be possible to have...`)
- Method documentation minimal; self-documenting code preferred

**JSDoc/TSDoc:**
- Not used in C++ codebase
- Documentation comments sparse; public method signatures considered self-documenting
- Example from test fixtures: Brief comments on parameters (e.g., `"area is being marked -> no MarkToMulti"`)

## Function Design

**Size:**
- Functions typically 5-50 lines; test methods usually 10-30 lines
- Complex logic broken into smaller functions with descriptive names
- Example: `callbackImpl()` delegates from static `callback()` wrapper

**Parameters:**
- Use const references for large objects: `const ScMarkData& rData`
- Use move semantics in constructors: `SparklineAttributes aSparklineAttributes` passed by value then moved
- Weak references common for memory management: `std::weak_ptr<SparklineGroup>`

**Return Values:**
- Return by const reference for accessors: `const ScRange& GetMarkArea() const { return aMarkRange; }`
- Return by value for complex types: `std::vector<std::shared_ptr<Sparkline>> getSparklinesFor(...)`
- Early return for guards: `if (condition) return result;`
- Structured bindings for multiple returns: `auto[iterator, bInserted] = m_aSparklineGroupMap.try_emplace(...)`

## Module Design

**Exports:**
- DLL export macros: `SC_DLLPUBLIC`, `EDITENG_DLLPUBLIC`, `OOO_DLLPUBLIC_TEST`
- Applied to public class methods and functions
- Example: `SC_DLLPUBLIC void ResetMark();`

**Barrel Files:**
- Not explicitly used; includes are direct (no re-export headers observed)
- Test framework provides base classes in `include/test/` (e.g., `test/bootstrapfixture.hxx`)

## Modern C++ Usage

**Standards:**
- C++17 features observed: structured bindings, auto type deduction, std::optional
- Example: `std::optional<double> m_aManualMax;`
- Explicit default member initializers in aggregates
- Scoped enums: `AxisType::Individual` (enum class style)

**Memory Management:**
- Smart pointers: `std::shared_ptr<Sparkline>`, `std::weak_ptr<SparklineGroup>`
- No raw new/delete observed in examined modern code
- RAII principles throughout

## File Copyright & License

All source files include:
- Mozilla Public License v2.0 header (Standard)
- Copyright notice: "This file is part of the LibreOffice project"
- Optional Apache License 2.0 notice for legacy Apache-originated code
- Never modify license headers; they track file provenance

---

*Convention analysis: 2026-02-09*
