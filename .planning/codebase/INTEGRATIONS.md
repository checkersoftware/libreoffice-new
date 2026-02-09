# External Integrations

**Analysis Date:** 2026-02-09

## APIs & External Services

**Cloud Storage & Sync:**
- Google Drive - OAuth2 integration for document access
  - SDK/Client: libcurl (HTTP client)
  - Auth: OAuth2 credentials via environment variables (`WITH_GDRIVE_CLIENT_ID`, `WITH_GDRIVE_CLIENT_SECRET`)

- Microsoft OneDrive - OAuth2 integration for document access
  - SDK/Client: libcurl
  - Auth: OAuth2 credentials via environment variables (`WITH_ONEDRIVE_CLIENT_ID`, `WITH_ONEDRIVE_CLIENT_SECRET`)

- Nextcloud - WebDAV protocol support via HTTP
  - SDK/Client: libcurl for HTTP/WebDAV requests
  - Auth: User credentials passed through WebDAV URLs

**Document Processing:**
- Microsoft Office Open XML (OOXML) - Built-in filter support via ooxmlexport/ooxmlimport modules
  - Location: `sc/source/filter/ooxml/`, `sd/source/filter/eppt/`, `sw/source/filter/ww8/`
  - No external SDK; parsing via internal XML processing

**Cryptography & Authentication:**
- OpenSSL or NSS - TLS/SSL certificates and cryptographic operations
  - Default: NSS (Network Security Services)
  - Optional: OpenSSL for cipher implementation
  - Configuration: Selected via `--with-crypto-backend` during configure

- GSSAPI/Kerberos - Network authentication for PostgreSQL and other services
  - SDK/Client: libgssapi, Kerberos libraries
  - Auth: System GSSAPI credentials
  - Usage: PostgreSQL SDBC driver authentication

**Collaborative Features:**
- WebSocket support via `java_websocket` library (optional)
  - Location: `external/java_websocket/`
  - Used for real-time collaboration features in Impress (presentations)

## Data Storage

**Databases:**

- **PostgreSQL** (Optional)
  - Connection: libpq library for native PostgreSQL access
  - Client: SDBC driver in `connectivity/source/drivers/postgresql/`
  - Configuration: `WITH_POSTGRESQL`, `WITH_POSTGRESQL_SDBC`
  - Environment: GSSAPI, Kerberos support for authentication

- **MySQL/MariaDB** (Optional)
  - Connection: MySQL client libraries (libmysql or MariaDB Connector)
  - Client: SDBC driver in `connectivity/source/drivers/mysql/`
  - Configuration: `--with-mysql`, `--with-mariadb-sdk`
  - Environment: Optional bundling of client library with connector

- **Firebird** (Optional)
  - Connection: fbclient or fbembed
  - Client: SDBC driver in `connectivity/source/drivers/firebird/`
  - Configuration: `WITH_FIREBIRD`, `WITH_FIREBIRD_SDBC`
  - Environment: Native Firebird client library

- **HSQLDB** (Java-based)
  - Connection: Pure Java database
  - Client: Java SDBC driver in `connectivity/source/drivers/hsqldb/`
  - Usage: Bundled database for application testing and examples

- **ODBC** (Windows/Optional)
  - Connection: Windows ODBC API for generic database access
  - Client: ODBC SDBC bridge driver
  - Configuration: `WITH_SYSTEM_ODBC_HEADERS`
  - Environment: System ODBC drivers for various databases

**File Storage:**
- Local filesystem only - No cloud storage SDKs bundled
- Supported formats: ODF, OOXML, legacy formats via filters
- Document import/export via filter modules

**Caching:**
- In-memory caching via Boost libraries
- Optional distributed caching through database connections
- No dedicated Redis/Memcached integration

## Authentication & Identity

**Auth Providers:**
- **OAuth2** - Cloud service integration (Google Drive, OneDrive)
  - Implementation: Custom HTTP client using libcurl
  - Credentials: Environment variables set at build/configuration time
  - Location: `sw/source/uibase/web/` and related modules

- **GSSAPI/Kerberos** - Network authentication
  - Implementation: Native GSSAPI integration via gssapi_krb5 library
  - Usage: PostgreSQL and LDAP connections
  - Location: `connectivity/source/drivers/postgresql/`

- **LDAP** - Directory services authentication
  - Implementation: libldap for LDAP protocol
  - Configuration: OpenLDAP libraries
  - Location: `dbaccess/source/` directory

- **Custom** - Application-level user authentication
  - No built-in user management system
  - Applications built on LibreOffice implement authentication separately

## Monitoring & Observability

**Error Tracking:**
- Breakpad - Crash reporting framework (optional)
  - Location: `external/breakpad/`
  - Configuration: `--enable-breakpad`
  - Integration: Desktop crash dump generation and minimal reporting

**Logs:**
- Console logging via SAL framework (`sal/inc/sal/log.hxx`)
- File-based logging to user directory
- Application-specific logging in individual modules
- No centralized metrics or telemetry infrastructure

**Debugging:**
- GDB support via debug information in binaries
- LLDB support for macOS debugging
- clang-format integration for code quality

## CI/CD & Deployment

**Hosting/Platform:**
- Standalone desktop application deployable to:
  - Windows (EXE installer via MSI)
  - macOS (DMG package)
  - Linux (via distribution package managers)
  - Android (APK)
  - iOS (IPA)
  - WebAssembly (browser)

**CI Pipeline:**
- Git-based (GitHub integration via `.github/` configuration)
- No bundled CI/CD service; relies on external systems
- Build automation via `autogen.sh` and Makefiles
- Testing via CppUnit and JUnit frameworks

**Build Artifacts:**
- Compiled libraries in `./lib/` after build
- Executable in `./program/` directory
- Packaged distributions via distro-specific tools

## Environment Configuration

**Required env vars for cloud integrations:**
- `WITH_GDRIVE_CLIENT_ID` - Google Drive OAuth2 client identifier
- `WITH_GDRIVE_CLIENT_SECRET` - Google Drive OAuth2 secret
- `WITH_ONEDRIVE_CLIENT_ID` - OneDrive OAuth2 client identifier
- `WITH_ONEDRIVE_CLIENT_SECRET` - OneDrive OAuth2 secret

**Database configuration:**
- `WITH_POSTGRESQL` - Enable PostgreSQL SDBC driver
- `WITH_MYSQL` - Enable MySQL/MariaDB SDBC driver
- `WITH_FIREBIRD` - Enable Firebird database driver
- `WITH_SYSTEM_ODBC_HEADERS` - Use system ODBC headers (Windows)

**Build environment:**
- `LODE_HOME` - LibreOffice Development Environment path
- `ACLOCAL` - Path to aclocal executable
- `AUTOCONF` - Path to autoconf executable

**Secrets location:**
- Configuration-time compilation: OAuth2 secrets embedded in binary during build
- Runtime: No active secret management system
- Security: Sensitive credentials must be handled at build/deployment time, not runtime

## Webhooks & Callbacks

**Incoming:**
- No webhook endpoints built-in to the application
- WebSocket support for collaboration features (Java implementation via java_websocket)
- Custom implementations possible via UNO extension interface

**Outgoing:**
- OAuth2 callback handling for cloud service redirects
- HTTP/HTTPS requests via libcurl for:
  - Google Drive/OneDrive authentication
  - WebDAV connections to Nextcloud
  - Optional certificate validation and revocation checking

**Real-time Collaboration:**
- WebSocket connections for multi-user editing (optional, experimental)
- Integration point: `avmedia/source/` for media components
- Protocol: Custom UNO messaging over WebSocket via java_websocket

## Data Exchange Formats

**Supported Export/Import:**
- Open Document Format (ODF/ODS/ODT/ODP) - Native format
- Microsoft Office Open XML (OOXML/DOCX/XLSX/PPTX)
- Legacy Microsoft Office (DOC/XLS/PPT)
- Rich Text Format (RTF)
- Plain text (TXT, CSV)
- Portable Document Format (PDF) - Export and limited import
- Portable Network Graphics (PNG, JPEG, BMP, GIF, TIFF)
- Markdown (via md4c library)

---

*Integration audit: 2026-02-09*
