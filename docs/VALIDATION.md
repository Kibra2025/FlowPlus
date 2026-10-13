# Local review record

Reviewed on 2026-10-12, limited to this FlowPlus repository. The target deployment
is Windows ↔ macOS over a VPN or already reachable addresses.

## Changes reviewed

- Independent FlowPlus identity for executables, settings, IPC endpoints, service,
  bundle, icons, installers and project documentation; upstream source notices,
  translation catalog filenames and input protocol signatures retained.
- FlowPlus file capability negotiation on an actually verified TLS connection,
  canonical/alias screen routing, a fixed destination for each file stream,
  bounded IPC parsing and ownership of queued transfer events.
- Explicit release/acceptance, UUID/manifest authorization, cancellation and
  consistent drag lifetimes; no transfer triggered by a stationary cursor.
- Bounded HTTP parsing and streaming, collision-safe batch publication, temporary
  file cleanup, expiring receive grants and restricted fallback policy.
- An asynchronous 32 KiB acknowledged tunnel; LAN/VPN route mismatches select
  the tunnel before attempting an unauthorized direct upload.

The original completion report was stronger than the evidence supported. In
particular, native Finder/modern Explorer drop delivery and unrestricted
interoperability with the standalone LocalSend application are not implemented.

## Executed checks

Local tools: Windows, Qt 6.7.3, GCC/MinGW 13.1, CMake/Ninja, clang-format 20.1.0
and REUSE. All tools, build trees and downloaded dependencies are ignored by Git.

| Check | Result | Scope |
| --- | --- | --- |
| `TransferProtocolTests` | 17 passed | Binary routing, size limits and malformed numbered IPC |
| `ProtocolUtilLimitsTests` | 5 passed | Actual protocol parser, embedded lengths and binary strings |
| `TransferInteractionTests` | 25 passed | Shared filename/manifest/storage rules and overlay behavior |
| `LocalSendServiceTests` | 23 passed | Actual HTTP service/sender, grants, replay, interruption, cancellation, collisions, invalid destinations and fallback policy |
| `VersionCheckerTests` | 14 passed | FlowPlus version response comparison |
| Changed core/IPC/GUI translation units | Compiled | Includes routing, TLS socket, transfer coordinator/service/overlay and MainWindow; object compilation rather than a complete application link |
| clang-format and `git diff --check` | Passed | Changed/new C++ files and working diff |
| REUSE lint | Passed | SPDX/license coverage; preserved upstream and LocalSend notices |
| Translation regeneration | Idempotent | Exact CMake lupdate options; repeated generation produces the same catalogs |
| Workflow/configuration/resource checks | Passed | YAML/JSON/XML parsing and branding artwork/container checks |

The four isolated transport suites passed together through CTest. Qt Test totals
include fixture setup/cleanup cases. Commands to reproduce the committed suites
are in [TESTING.md](TESTING.md).

## Remaining verification

The complete Windows application has not been built/linked locally with its supported
MSVC toolchain here. No macOS application build, installer installation/signing,
real two-device transfer, physical Explorer/Finder drag, mixed-DPI display test or
native destination acceptance test was executed.

## CI regression follow-up

GitHub runs for commit `8af819a` were inspected on 2026-10-12. The isolated
Windows transfer suites passed; macOS failed five authorized reception cases.
Their original output omitted the HTTP response. The shared destination check
now probes actual directory writability and obtains Unix free space with
`statvfs` on that directory, avoiding the reported volume-root read-only flag
and treating unavailable capacity as unknown. This addresses a suspected macOS
APFS storage-query issue, not a confirmed diagnosis. Failed prepare responses
and sender failures now appear in test output; failed isolated CI jobs retain
CTest diagnostics as artifacts.

The complete CI also exposed an uninitialized logging singleton in the parser
test, a deprecated Qt exception macro rejected by the macOS compiler, and Linux
tests assuming incomplete temporary files always have directory entries. The
parser fixture now initializes logging when built with the application, the
exception assertion uses the supported macro, and interruption/cancellation
tests wait for received-byte progress before verifying cleanup. Qt documents
Linux unnamed temporary files in [QTemporaryFile](https://doc.qt.io/qt-6/qtemporaryfile.html).

After these changes, all four isolated suites passed locally on Windows
(7.03 seconds); the service suite reports 23 passing cases. REUSE and diff
checks passed. The updated macOS/Linux/full application builds require a new
commit and CI run; rerunning the old commit does not test these changes.

The CodeQL screenshots also show the same parser crash and Linux temporary-file
test failures interrupting its automatic build, plus missing API permissions.
Its workflow now uses manual CMake/Ninja compilation of the application with
`BUILD_TESTS=OFF` and `BUILD_INSTALLER=OFF`, leaving regression execution to the
test/CI workflows. It requests `contents: read`, `actions: read` and
`security-events: write` for the CodeQL job, as required for analysis upload.
The workflow was checked locally; its extraction/upload still requires a new
GitHub run. See [CodeQL Action documentation](https://github.com/github/codeql-action/blob/main/README.md).

The FreeBSD job for the same commit stops at the deprecated Qt exception macro
already replaced in the parser test. Both Flatpak architectures stop before
building with AppStream `releases-info-missing`. The metadata now describes the
current `1.26.0.9999` development snapshot dated 2026-10-12; it does not claim a
stable FlowPlus release. XML structure/version checks passed locally. The full
AppStream/Flatpak validator and FreeBSD build remain pending the new CI run.

## CI confirmation and packaging follow-up

The subsequent `34bb140` run confirmed passing isolated transfer suites on
Windows/macOS, CodeQL, FreeBSD and 16 of 17 full application build jobs,
including Windows x64/ARM64, macOS ARM64 and the Linux distributions.
The remaining macOS Intel job built the application but failed packaging when
`hdiutil` could not detach the temporary DMG. Its intended retry loop exited on
the first failure because Bash runs with `-e`. The package command is now an
`if` condition, with five bounded attempts and an explicit failure exit if all
attempts fail. This enables the intended recovery from a transient detach
failure; a persistent packaging failure will still fail CI.

Both Flatpak architectures built and bundled successfully, then failed final
Flathub lint for missing screenshots and their OSTree mirror. The existing CI
exception file now lists only those two presentation checks for the FlowPlus
app ID. It does not exempt runtime, dependency, permission or security checks.
These are development packages from this repository, not a Flathub submission;
capture and host actual FlowPlus screenshots, enable screenshot mirroring and
remove these exceptions before a Flathub submission. The runtime-update notice
for KDE 6.11 is a warning and did not cause these failures.

The updated packaging configuration still requires a new CI run. Passing build
jobs establish compilation and automated checks, not physical cross-device drag
behavior. The previous local-only limitations above are superseded only where
this section explicitly records GitHub CI confirmation.

On 2026-10-13, the `fe79af9` run confirmed passing macOS Intel packaging and both
Flatpak jobs. Windows ARM64 failed after linking `TransferProtocolTests.exe`:
vcpkg's dependency deployment could not read `build/bin/Qt6Core.dll` while the
GUI's post-build deployment was updating DLLs in the same directory. The new
transfer tests had inherited the application's global runtime output directory.
In the full Windows build, each of the three transfer tests now has a separate
runtime directory under `build/src/unittests/transfer`, avoiding writes shared
with the application and the other test targets. CTest resolves their executable
locations from the targets; the isolated suite keeps its existing layout.
Local CMake generation using the actual full-build test directory confirmed
distinct executable paths and CTest registration with Ninja and Ninja
Multi-Config. All four isolated suites passed again on Windows (8.15 seconds);
REUSE and diff checks passed.
Windows ARM64 dependency deployment still requires confirmation in the next CI
run. No application code or test assertions were changed for this correction.

## First continuous release

The `0fdb4c9` run on 2026-10-13 passed all 17 application build jobs, both
Flatpak builds, FreeBSD and the CI gate. The only failed job was release
publication: `gh release delete continuous --cleanup-tag` failed because this
repository has no release yet. The release job now explicitly requests
`contents: write`, creates or moves only `refs/tags/continuous` to the tested
commit using the GitHub API, and lets the release action create/update the
prerelease and overwrite its assets in place. There is no release deletion.
Release notes also handle a missing initial tag, and unmatched package assets
now cause publication to fail instead of silently producing incomplete output.

The actual workflow JavaScript was exercised with API doubles for first
publication, an existing tag, a similarly named tag and API read/write denials;
all five checks passed and API errors propagate. Both release-note Git history
commands were checked locally. YAML, REUSE and diff checks passed. No live tag
or release was changed locally; GitHub publication requires a new commit/run.
See the [Git reference API](https://docs.github.com/en/rest/git/refs) and
[release action](https://github.com/softprops/action-gh-release/tree/v3).

Received files reliably target the download folder at the implementation level.
Gesture receipts also publish clipboard URLs. Windows desktop copies and optional
legacy `WM_DROPFILES` delivery require manual validation; macOS Finder and modern
Explorer folder OLE drops remain outside the implemented native delivery path.
Use the manual checklist before claiming production-ready cross-device dragging.

## LocalSend reference cleanup

`localsend-main` is absent from the repository root and is not a build/runtime
dependency. Its license and attribution are retained in the tracked notices.
Recursive deletion was rejected by the automatic approval review with
`blocked by policy`, so the reference copy was moved reversibly into
`build/reference-archive/localsend-main`, which Git ignores. It is not part of a
GitHub source checkout or release source archive. No commit, push or release was
created during this review.
