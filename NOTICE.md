# FlowPlus origins

FlowPlus is an independent derivative of Deskflow, developed to combine input sharing with an experimental cross-device file drag workflow. It is not affiliated with or endorsed by the upstream projects.

## Deskflow

Upstream: <https://github.com/deskflow/deskflow>

The C++ input sharing application, Qt interface, build system, translations and project history originate in Deskflow and its Synergy/Barrier predecessors. Original notices identify Deskflow Developers, Synergy App Ltd / Symless Ltd, Nick Bolton, Chris Schoeneman and other contributors. Those notices remain in the source files and license documents.

The application is distributed under GPL-2.0-only with the OpenSSL exception where the source headers specify it. MIT build/documentation files and LGPL KDE Breeze icons retain their own terms. FlowPlus-specific changes do not remove upstream attribution or change the license of inherited files.

## LocalSend

Upstream application: <https://github.com/localsend/localsend>

Protocol reference: <https://github.com/localsend/protocol>

LocalSend's protocol and transfer model inspired the native Qt implementation in `src/lib/gui/transfer`. This repository does not compile or bundle the LocalSend Dart/Flutter/Rust application. The former `localsend-main` checkout was a reference snapshot, not a runtime or build dependency.

That reference snapshot's license identifies **Copyright 2022–2026 Tien Do Nam** and **Apache License 2.0**. Its original license text is preserved in [docs/third-party/LocalSend-LICENSE.txt](docs/third-party/LocalSend-LICENSE.txt). Including that reference notice does not make the FlowPlus application Apache-licensed and does not imply that LocalSend implementation code has been linked into the GPL-2.0-only application.

## FlowPlus

Project: <https://github.com/Kibra2025/FlowPlus>

FlowPlus-specific changes include product identity and artwork, native transfer integration, drag coordination, and hardening of the added transfer/IPC paths. New files carry their own SPDX notices where applicable. Historical source headers, protocol signatures and internal `deskflow` identifiers intentionally retain upstream provenance or compatibility.
