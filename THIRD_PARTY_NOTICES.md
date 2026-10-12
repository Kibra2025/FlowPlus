# Third-party notices

The source headers and `REUSE.toml` are the authoritative file-level license map. This table describes the main origins and dependencies; it does not replace their license texts.

| Component | Use | License/reference |
| --- | --- | --- |
| Deskflow and its predecessors | Inherited application source and history | [GPL-2.0-only](LICENSE), [OpenSSL exception](LICENSES/LicenseRef-OpenSSL-Exception.txt); file-specific [MIT](LICENSES/MIT.txt) notices |
| KDE Breeze icons | Shared interface action icons | [LGPL-2.1-only](LICENSES/LGPL-2.1-only.txt), existing resource attribution |
| LocalSend | Protocol and application design reference; no bundled application code | [Preserved Apache 2.0 reference license](docs/third-party/LocalSend-LICENSE.txt), [protocol repository](https://github.com/localsend/protocol) |
| Qt 6 | GUI, networking, JSON, local IPC | Qt's distribution license and notices; supplied by the build environment |
| OpenSSL 3 | Input connection encryption | OpenSSL's distribution license and notices; supplied by the build environment |
| libei / libportal / X11 libraries | Inherited Linux input backends | Respective upstream notices; supplied by the build environment |

FlowPlus artwork (`flowplus*.svg`, platform icon files and installer artwork) is new project artwork under MIT. The remaining Deskflow artwork files and shared icon resource paths retain their upstream notices.

When packaging dependencies, keep the license and notice files required by the actual Qt/OpenSSL/system library distribution used in that build. Preserve the application's corresponding source and original notices with release packages.
