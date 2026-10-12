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
| `LocalSendServiceTests` | 22 passed | Actual HTTP service/sender, grants, replay, interruption, cancellation, collisions and fallback policy |
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

The complete Windows application has not been built/linked with its supported
MSVC toolchain here. No macOS application build, installer installation/signing,
real two-device transfer, physical Explorer/Finder drag, mixed-DPI display test or
native destination acceptance test was executed. The added Windows/macOS CI
workflow has been prepared but not run on GitHub during this review.

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
