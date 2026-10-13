# FlowPlus architecture

FlowPlus keeps the upstream input backends separate from its file transfer service. The GUI owns file access and transfer policy; the core owns screen switching and the established input connection.

## Repository map

| Path | Responsibility |
| --- | --- |
| `src/apps/deskflow-gui` | Qt GUI entry point; outputs `flowplus` / `FlowPlus.app` |
| `src/apps/deskflow-core` | Input sharing process; outputs `flowplus-core` |
| `src/apps/deskflow-daemon` | Windows secure desktop service; outputs `flowplus-daemon` |
| `src/lib/gui/transfer` | Drag capture, peer authorization, transfer coordination, LocalSend HTTP implementation and receive storage |
| `src/lib/gui/core`, `src/lib/gui/ipc` | GUI process control and local IPC clients |
| `src/lib/deskflow/ipc` | Core/daemon IPC server and message dispatch |
| `src/lib/client`, `src/lib/server` | Input protocol peers and transfer message forwarding |
| `src/lib/platform` | Windows, macOS, X11 and Wayland input backends |
| `src/lib/common` | Product constants, settings, version metadata, translation loading and bounded routed transfer payloads |
| `src/unittests` | Automated regression tests |
| `deploy`, `cmake`, `.github` | Platform packages, dependency configuration and CI |
| `translations`, `LICENSES` | Upstream translation catalogs and license texts |

There is one C++/Qt repository. A LocalSend source checkout is not needed to configure, build or run it.

## Transfer lifecycle

`DragDropManager` stages local regular files, identifies the actual connected destination screen, and coordinates a single transfer lifecycle. Metadata carries a transfer identifier and a file manifest. `EdgeDropSensor` observes file-manager drags at screen edges. On macOS, `NativeDragSource` also observes the dedicated drag pasteboard during a held left-button gesture; revision tracking rejects stale drags after release, reconnection or screen return. It only polls while a verified transfer peer is available. This stages Finder files before reaching the edge so the core can allow crossing while the button is held. `DragOverlayWidget` displays pending incoming drops without treating a stationary cursor as a released button.

Screen entry/leave and mouse release events flow from the core to the GUI through local IPC. Drag metadata and file transfer messages travel through the connected client/server protocol (`DDRG` and `DFTR`). Before sending these extensions, the peers negotiate the `FPLS` capability and its acknowledgement through an option that upstream peers can ignore. An upstream Deskflow peer can continue input sharing without receiving unsupported transfer frames.

`LocalSendService` announces local discovery information and implements a subset of LocalSend v2.1 REST endpoints. The coordinator grants one prepared incoming transfer to the connected source IP, transfer UUID and exact filename/size manifest; the grant expires after two minutes. A manual receive requires acceptance; a drag receive follows the observed drop lifecycle. Unknown peers and unsolicited standalone LocalSend offers do not gain receive permission by advertising a hostname or HTTP endpoint. HTTPS LocalSend upload is not implemented and is not silently downgraded to HTTP.

The sender first tries direct preparation on the peer's LocalSend port. Network failure before an upload starts can trigger a transfer over the already established TLS input connection. A policy rejection or failed in-progress HTTP upload is not silently retried through another path. The tunnel uses bounded chunks and acknowledgements so neither local IPC nor a whole-file allocation becomes the data buffer.

The tunnel acknowledges each 32 KiB chunk before sending the next one. This keeps
memory and queued data bounded, but throughput on a high-latency Internet route is
limited by the round-trip time. An address/interface mismatch or a public receiver
address selects TLS immediately. Pending drags expire after two minutes in both
the GUI and core; Escape revokes the staged UUID across participating screens.

The receiver writes private partial data and validates the manifest and byte counts before publication. Completed downloads use collision-safe names in `Downloads/FlowPlus`. Cancellation, timeout and disconnect must terminate the active transfer and remove partial data.

## Networks and trust

The inherited input connection defaults to TCP 24800. Both ends must be reachable, TLS must be enabled, and peer fingerprints must be trusted before file sharing. Internet use means an existing reachable connection, usually via a VPN; FlowPlus contains no relay or NAT traversal service.

LocalSend discovery uses multicast 224.0.0.167 UDP 53317 and normally does not cross routed subnets. HTTP uploads use TCP 53317 and are plaintext in this implementation. Use direct HTTP on a trusted LAN or encrypted VPN. Its discovery identity is not a substitute for the authenticated input connection.

## Operating system delivery

`NativeDropTarget` snapshots a filesystem directory at release, before storage preparation or download. Windows uses Shell COM to find the active Explorer view and Accessibility hit testing to distinguish its background from a resolvable subfolder icon. macOS uses Finder's Accessibility URL/document attributes and desktop window identification; no Automation/AppleScript access is requested. Unknown windows, virtual folders and ambiguous icons produce no destination.

The completed download is initially published in `Downloads/FlowPlus`. Recognized targets receive a copy on a worker thread, using private temporary files, collision-safe names and no overwrite. Failure or cancellation before the entire batch is placed rolls back only copies from that batch and leaves the downloads available. After successful placement, unchanged download copies are removed; cleanup failures retain the extra copies and are reported. Files on the sending computer are never removed. Shutdown interrupts and joins the worker. New incoming offers are declined while placement is in progress. The completion signal reports the actual paths, Received files opens the last delivery folder, and the UI explains any fallback. File receipt leaves the clipboard unchanged. This is directory placement, not arbitrary OLE/AppKit application drop injection or desktop icon positioning.

The macOS GUI's `AccessibilitySetupDialog` requests authorization once, waits without closing the app, and rechecks after returning from System Settings. Ad-hoc bundle signing runs after all installed resources, followed by strict recursive signature verification. Developer ID signing and notarization are not configured.

Display edges, native drag ownership, remote pointer coordinates, permissions and injected mouse events interact differently across platforms. Real Windows ↔ macOS tests are required independently of HTTP/tunnel regression tests.

## Product identity and retained names

The displayed product is FlowPlus. Executables, local IPC endpoints, the Windows service, macOS bundle, package prefix, settings folders and application ID use its own identity (`io.github.kibra2025.FlowPlus`). Installer upgrades use a distinct product GUID to avoid replacing Deskflow.

The `deskflow::` C++ namespace, non-macOS CMake target names, upstream source directory names, `DESKFLOW_*` CI/version variables, shared Breeze resource directories and `deskflow_*.ts`/`.qm` translation catalogs remain internal compatibility details. The Synergy/Barrier handshakes and their protocol labels remain unchanged. Copyright headers retain the original authors. These identifiers are not user-visible product names and should not be mass-renamed.
