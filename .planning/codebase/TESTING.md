# Testing Patterns

**Analysis Date:** 2026-02-09

## Test Framework

**Runner:**
- CppUnit (cppunit)
- Config: Individual `.mk` makefile per test suite (e.g., `editeng/CppunitTest_editeng_core.mk`)
- Makefile-gmake based build system with `$(eval $(call gb_CppunitTest_...))` macros

**Assertion Library:**
- CppUnit assertions: `CPPUNIT_ASSERT()`, `CPPUNIT_ASSERT_EQUAL()`, `CPPUNIT_ASSERT_MESSAGE()`
- Comparison macros: `CPPUNIT_ASSERT(condition)`, `CPPUNIT_ASSERT_EQUAL(expected, actual)`
- No message-less assertions; patterns show both asserts and message variants

**Run Commands:**
```bash
make build            # Build main codebase and tests (gmake system)
```

Note: Specific test runners vary by module; tests registered via `CPPUNIT_TEST_SUITE_REGISTRATION(TestName)` macro.

## Test File Organization

**Location:**
- Co-located with source or in dedicated test directories
- Pattern 1: `module/qa/unit/TestName.cxx` (unit tests)
- Pattern 2: `module/qa/extras/TestName.cxx` (integration/export tests)
- Pattern 3: `test/source/sheet/xsheetfilterable.cxx` (shared test utilities)
- Common subdirectories: `qa/`, `qa/unit/`, `qa/extras/`, `qa/inc/`, `qa/data/`

**Naming:**
- Test files: PascalCase matching tested class (e.g., `ESelectionTest.cxx` for `ESelection`)
- Test classes: `ClassName + "Test"` suffix (e.g., `class ESelectionTest`)
- Test methods: `test` + verb + feature (e.g., `testConstruction()`, `testAssign()`, `testFilter()`)

**Structure:**
```
module/
├── qa/
│   ├── inc/              # Test base classes and shared fixtures
│   ├── unit/             # Unit tests
│   └── extras/           # Integration/roundtrip/export tests
└── source/
    └── [module code]
```

Example from codebase:
- `sw/qa/inc/swmodeltestbase.hxx` - Base class for document tests
- `sw/qa/unit/swtiledrenderingtest.cxx` - Tiled rendering tests
- `editeng/qa/unit/ESelectionTest.cxx` - Unit test for ESelection

## Test Structure

**Suite Organization:**
```cpp
// Pattern 1: CPPUNIT_TEST_FIXTURE macro (modern)
CPPUNIT_TEST_FIXTURE(ESelectionTest, testConstruction)
{
    {
        ESelection aNewSelection;
        CPPUNIT_ASSERT_EQUAL(sal_Int32(0), aNewSelection.start.nPara);
    }
}

// Pattern 2: Test suite registration (classic)
namespace
{
class ESelectionTest : public test::BootstrapFixture
{
};

CPPUNIT_TEST_FIXTURE(ESelectionTest, testConstruction)
{
    // test body
}
}  // end anonymous namespace
```

**Patterns:**

*Setup/Teardown:*
- Base class: `test::BootstrapFixture` or module-specific base (e.g., `SwModelTestBase`)
- `setUp()` override initializes test environment
- `tearDown()` override cleans up resources
- Example from `SwTiledRenderingTest`:
```cpp
void setUp() override
{
    SwModelTestBase::setUp();
    SwGlobals::ensure();
    SwModule::get()->ClearRedlineAuthors();
    comphelper::LibreOfficeKit::setActive(true);
}

void tearDown() override
{
    if (mxComponent.is())
    {
        // cleanup code
        mxComponent->dispose();
        mxComponent.clear();
    }
    test::BootstrapFixture::tearDown();
}
```

*Assertion pattern:*
- Immediate assertions after operations
- Multiple small assertions per test preferred over single large assertion
- Message-driven assertions: `CPPUNIT_ASSERT_MESSAGE("Row 1 should be invisible", !bIsVisible);`

## Mocking

**Framework:** None explicitly detected

**Patterns:**
- Test isolation through setup/teardown
- Real UNO object instantiation preferred over mocks
- Tests use actual LibreOffice engine and file I/O
- Example: Filter tests load real documents, not mocks
```cpp
void testFilter()
{
    uno::Reference< sheet::XSpreadsheet > xSheet(getXSpreadsheet(), UNO_QUERY_THROW);
    uno::Reference< sheet::XSheetFilterable > xFA(xSheet, UNO_QUERY_THROW);
    // Test uses real spreadsheet objects, not mocks
}
```

**What to Mock:**
- Minimal mocking observed; tests prefer integration
- File I/O mocked via temp files: `unotools::TempFile`
- Complex external dependencies may be stubbed (not observed in examined code)

**What NOT to Mock:**
- Core LibreOffice objects (XSpreadsheet, XCell, etc.) - use real instances
- Document models - load actual files or create via API
- UNO components - instantiate real components

## Fixtures and Factories

**Test Data:**

*Inline data initialization:*
```cpp
uno::Sequence< sheet::TableFilterField > xTFF{
    { /* Connection   */ {},
      /* Field        */ 0,
      /* Operator     */ sheet::FilterOperator_GREATER_EQUAL,
      /* IsNumeric    */ true,
      /* NumericValue */ 2,
      /* StringValue  */ {}},
    { /* Connection   */ {},
      /* Field        */ 1,
      /* Operator     */ sheet::FilterOperator_LESS,
      /* IsNumeric    */ false,
      /* NumericValue */ {},
      /* StringValue  */ u"C"_ustr }
};
```

*Base fixture classes:*
- `test::BootstrapFixture` - Core LibreOffice initialization
- `SwModelTestBase` - Document loading and roundtrip testing
- `UnoApiXmlTest` - XML-based API testing

**Location:**
- Shared test base classes in `include/test/` (e.g., `include/test/bootstrapfixture.hxx`)
- Test data/document files in `qa/data/` subdirectories
- Module-specific bases in `module/qa/inc/` (e.g., `sw/qa/inc/swmodeltestbase.hxx`)

## Coverage

**Requirements:** Not explicitly enforced; no coverage reporting tool detected

**View Coverage:**
- No built-in coverage commands found in examined test makefiles
- Coverage would require external tool integration (not configured in current setup)

## Test Types

**Unit Tests:**
- Scope: Single class or function
- Approach: Direct instantiation with test fixtures
- Location: `module/qa/unit/`
- Example: `ESelectionTest` tests only `ESelection` class constructors and methods
- Setup: Minimal; inherit from `test::BootstrapFixture` for LibreOffice engine

**Integration Tests:**
- Scope: Cross-component interaction and document models
- Approach: Load documents via UNO API, verify state through API calls
- Location: `module/qa/extras/` (especially for export/roundtrip tests)
- Example: `SwTiledRenderingTest` tests renderer callbacks and document state
- Setup: Heavy; inherit from `SwModelTestBase`, initialize LibreOfficeKit
- Macros for document roundtrip:
```cpp
#define DECLARE_OOXMLEXPORT_TEST(TestName, filename) \
    DECLARE_SW_ROUNDTRIP_TEST(TestName, filename, TestFilter::DOCX)
```

**E2E Tests:**
- Not formally separated; integration tests serve E2E purpose
- UI test framework available in `uitest/` but not analyzed here
- Document load/save tests act as E2E validators

## Common Patterns

**Async Testing:**
Not observed; all tests are synchronous with blocking I/O.

**Error Testing:**
```cpp
// Pattern: Test expected exceptions
CPPUNIT_ASSERT_NO_THROW_MESSAGE("Unable to create XSheetFilterDescriptor",
                                xSFD->setFilterFields(xTFF));

// Pattern: Direct error checking
if (auto pCurrentSparkline = iterator->lock())
{
    // Handle valid pointer
}
else
{
    // Error case: weak_ptr expired
}
```

**Test Naming Conventions:**

1. **Feature-based naming:**
   - `testConstruction()` - Test object construction
   - `testAssign()` - Test assignment operator
   - `testFilter()` - Test filtering functionality
   - `testEquals()` - Test equality operator

2. **Condition-based naming:**
   - `testLess()` - Test less-than operator
   - `testGreater()` - Test greater-than operator
   - `testAdjust()` - Test data adjustment logic

3. **Roundtrip/Export naming:**
   - `Load_Verify_Reload_Verify()` - Load document, verify, save, reload, verify
   - Used with `DECLARE_SW_ROUNDTRIP_TEST` macros for format preservation tests

## Test Execution Configuration

**Makefile Structure:**

Each test module defines a `.mk` file:

```makefile
# editeng/CppunitTest_editeng_core.mk
$(eval $(call gb_CppunitTest_CppunitTest,editeng_core))

$(eval $(call gb_CppunitTest_add_exception_objects,editeng_core, \
    editeng/qa/unit/core-test \
    editeng/qa/unit/ESelectionTest \
))

$(eval $(call gb_CppunitTest_use_libraries,editeng_core, \
    basegfx \
    comphelper \
    cppu \
    vcl \
))

$(eval $(call gb_CppunitTest_use_components,editeng_core,\
    configmgr/source/configmgr \
    i18npool/util/i18npool \
))
```

**Test Dependencies:**
- External dependencies declared via `gb_CppunitTest_use_externals()`
- Library dependencies via `gb_CppunitTest_use_libraries()`
- Component dependencies via `gb_CppunitTest_use_components()`
- SDK/UNO via `gb_CppunitTest_use_sdk_api()` and `gb_CppunitTest_use_ure()`
- VCL initialization via `gb_CppunitTest_use_vcl()`

## Key Test Utilities

**BootstrapFixture (`include/test/bootstrapfixture.hxx`):**
- Bootstraps full LibreOffice environment for unit tests
- Initializes UNO, component framework, VCL
- Can be excluded conditionally: `static bool IsDefaultDPI()`
- Provides device info: `static sal_uInt16 getDefaultDeviceBitCount()`

**SwModelTestBase (`sw/qa/inc/swmodeltestbase.hxx`):**
- For Writer document tests
- Supports roundtrip testing with reload/verify cycle
- Provides document loading and XML parsing utilities
- Helper methods: `getBodyText()`, `getStyles()`, `getAutoStyles()`

**UnoApiXmlTest:**
- For UNO API testing with XML assertion
- Supports loading/saving documents and validating XML

---

*Testing analysis: 2026-02-09*
