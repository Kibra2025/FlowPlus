# Testing FlowPlus

Automated tests cover program logic and regression cases. They do not establish physical cross-device drag behavior, macOS permissions, Windows service installation or package signing.

## Automated checks

The transport regressions can be configured independently with CMake 3.24+, a
C++20 compiler and Qt 6.7+ (Core, Network, Widgets and Test):

```sh
cmake -S src/unittests/transfer -B build/transfer -DCMAKE_BUILD_TYPE=Release
cmake --build build/transfer --config Release --parallel 4
ctest --test-dir build/transfer -C Release --output-on-failure
```

Set `CMAKE_PREFIX_PATH` to your Qt installation when it is not discoverable.
The isolated suite covers routed binary payloads, malformed IPC, embedded network
length limits, filename/manifest rules, overlay behavior and actual LocalSend HTTP
requests, including authorization, interruption, collisions and fallback policy.
It does not run the full coordinator against two input backends. The
`transfer-tests.yml` workflow runs this suite on Windows and macOS. It also runs
the permission setup tests independently:

```sh
cmake -S src/unittests/startup -B build/startup -DCMAKE_BUILD_TYPE=Release
cmake --build build/startup --config Release --parallel 4
ctest --test-dir build/startup -C Release --output-on-failure
```

The transfer suite includes safe destination placement (collisions, unsupported
paths, empty files, rollback and cancellation) and native drag revision tracking.
macOS CI compiles the native Finder/pasteboard helpers; that does not prove a real
Finder drag or Accessibility hit test succeeds.

See [the local review record](VALIDATION.md) for checks performed on the current changes.

Configure with `BUILD_TESTS=ON` (the default), build and run:

```sh
cmake --build build --config Release
ctest --test-dir build/src/unittests -C Release --output-on-failure
```

On headless Linux, set `QT_QPA_PLATFORM=minimal`. `SKIP_BUILD_TESTS=ON` skips the post-build test run, not compilation of test targets. Compile with warnings as errors when supported by the selected toolchain. Run the repository clang-format and REUSE checks before publishing changes.

After changing UI text, build `app_translations` in the full application build
and commit the updated `translations/*.ts` catalogs:

```sh
cmake --build build --target app_translations --config Release
git diff -- translations
```

This target runs Qt Linguist's `lupdate` and `lrelease`. New untranslated entries
remain unfinished and use the English source text at runtime. Keep existing
translations and review the catalog diff. CI rejects source changes produced by
the build, including missing catalog updates.

## Windows ↔ macOS checklist

Use a real Windows computer and a real macOS computer with the same FlowPlus build. Repeat transfers in both directions. Keep the source file available for hash comparison.

1. Verify FlowPlus executable/bundle identity, About attribution, icons, settings folders and help/download links. Confirm an installed Deskflow copy keeps its own settings and service.
2. Enable TLS and peer fingerprint checking, verify fingerprints, configure screen names/positions and connect input sharing. On macOS, grant accessibility/input permissions to FlowPlus and its core as requested.
3. Send a small file manually. Confirm the receiver acceptance prompt. Reject it and verify no fallback bypasses the rejection and no completed file appears.
4. Use the shared pointer controlled by the primary computer to drag a regular file from Explorer/Finder across each configured edge. A secondary computer's independent hardware pointer does not drive FlowPlus screen traversal. Keep the button held: the target overlay must remain pending without starting a transfer. Release it: exactly one transfer should begin.
5. Compare received file size and SHA-256 hash. On Windows use `Get-FileHash -Algorithm SHA256`; on macOS use `shasum -a 256`.
6. Repeat with multiple files, a zero-byte file, Unicode names, a file larger than 32 MiB, and an existing identical filename at the destination. Existing data must remain intact.
7. Press Escape before release, including after waiting more than 30 seconds; then release the mouse and verify no transfer starts. Leave/re-enter the target screen, pass through a third screen, disconnect mid-transfer, and cancel an accepted transfer. Verify overlays disappear, no duplicate session starts, and partial data is not visible as a completed download.
8. Exercise different display scaling, multiple monitors, negative monitor coordinates, fullscreen apps and macOS Spaces. Record the exact monitor layout when reporting a drag bug.
9. Release over an open Finder/Explorer folder background, a real subfolder icon, the desktop, a virtual location and an unrelated app. Recognized folders should receive the files; unsupported/ambiguous targets must fall back to Downloads/FlowPlus with an explanatory status. Change the foreground window or navigate elsewhere during a large transfer: the captured destination must stay unchanged. Verify existing files, sender originals and the clipboard are preserved. After successful placement, unchanged temporary download copies should be removed; cleanup failures retain extra copies and are reported. Received files must open the last delivery folder. Exact icon coordinates and arbitrary application drops are not supported.
10. On macOS, remove FlowPlus's Accessibility authorization and launch. The setup window must remain visible; decline the OS prompt, open settings from the dialog, enable access, and verify automatic continuation. Check explicit Quit and relaunch after replacing the app. Run `codesign --verify --deep --strict --verbose=2 /Applications/FlowPlus.app` on the newly packaged artifact; distinguish signature integrity from Developer ID/notarization.
11. With the Mac as the controlling server, begin a fresh Finder drag, cross into Windows and release. Repeat after a canceled drag, an ordinary click, reconnection, and returning from the remote screen. Stale drag pasteboard contents must never resend old files. Repeat near the Dock, menu bar and in fullscreen/Spaces.

## VPN and tunnel checklist

1. Connect the two computers through a VPN/reachable FlowPlus TLS connection. Disable multicast on the route. Input sharing and peer-directed transfers must still work.
2. Allow the advertised LocalSend TCP port on the VPN and verify the direct path. Normally it is 53317; if another application occupies that port, FlowPlus may advertise an available ephemeral port. Capture transport/progress logs without exposing filenames or tokens publicly.
3. Block the advertised HTTP port while keeping TCP 24800 reachable. Repeat a drag/manual accepted transfer and confirm the TLS tunnel, acknowledgements, progress and identical hash. Repeat with LAN and VPN adapters active together to check route selection.
4. Reject a receive request and fail an HTTP upload after it has started. Neither case may trigger another transfer automatically.
5. Disable TLS and try file sharing. It must be unavailable. An unauthenticated HTTP sender or a multicast hostname collision must not gain receive authorization.
6. Interrupt the tunnel, retry after reconnecting, and transfer in the reverse direction. A stale transfer ID must not accept new chunks.

## Report evidence

Record both operating systems, FlowPlus version/commit, TLS state, source and target screen names, screen layout/scaling, VPN type, transport path, requested delivery target, file count/sizes, hashes and the precise step that failed. Share logs with certificate keys, tokens and private file paths removed. Mark untested checklist items as untested.
