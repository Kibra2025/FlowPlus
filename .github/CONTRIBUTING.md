# Contributing to FlowPlus

Open issues and pull requests in the FlowPlus repository. Include the FlowPlus version/commit and both operating systems in cross-device reports; do not report fork-specific behavior to Deskflow or LocalSend as if it were an upstream release.

Read the [architecture](../docs/ARCHITECTURE.md) and [build instructions](../docs/dev/build.md). Use the existing C++20/Qt6 conventions and `.clang-format`. Keep changes focused, preserve SPDX/copyright headers, and describe useful validation in the pull request.

For transfer changes, test malformed input, receive rejection, cancellation, disconnects, bounded buffering, filename collisions and both transfer directions. Run automated tests and the relevant [Windows/macOS manual checklist](../docs/TESTING.md). Mark platform checks you cannot run as untested.

Do not import the LocalSend application source as a nested repository or runtime dependency. Native Qt protocol work belongs in `src/lib/gui/transfer`; the core should coordinate authenticated peers and bounded messages rather than manage download files. Keep protocol references and origins visible in [NOTICE](../NOTICE.md).

GitHub Actions package FlowPlus's own names and application ID. CI must not publish to upstream projects. SonarCloud is optional: enable `SONAR_SCANNER_ENABLED=true` only after configuring this repository's `SONAR_ORGANIZATION`, `SONAR_PROJECT_KEY` and `SONAR_TOKEN`. Releases require the repository's own permissions and signing configuration.