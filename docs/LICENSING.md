# FlowPlus licensing

FlowPlus preserves the licenses of the Deskflow code it derives from. The main
application uses **GPL-2.0-only with LicenseRef-OpenSSL-Exception** where stated
in its source headers. The complete [GPL version 2 text](../LICENSE) and
[OpenSSL exception](../LICENSES/LicenseRef-OpenSSL-Exception.txt) are included.

This is a continuation of the inherited license, not a relicensing of Deskflow
or LocalSend. Becoming an independent GitHub repository changes neither the
code's origin nor its license obligations.

## File-level terms

| Material | Terms |
| --- | --- |
| Application source | Source headers; primarily GPL-2.0-only with the OpenSSL exception |
| Build files and documentation | MIT where indicated in headers or `REUSE.toml` |
| FlowPlus artwork and interface preview | MIT, as recorded in `REUSE.toml` |
| Shared KDE Breeze action icons | LGPL-2.1-only and their original notices |
| LocalSend reference license | Apache-2.0; preserved for attribution, not applied to the whole application |

Use [REUSE.toml](../REUSE.toml), individual source headers and [LICENSES](../LICENSES)
to identify a particular file's terms. [NOTICE](../NOTICE.md) records the origins;
[third-party notices](../THIRD_PARTY_NOTICES.md) describe dependencies. The original
Deskflow, Synergy/Barrier and other contributor notices remain in the source.

## Redistributing FlowPlus

When distributing modified builds, preserve the applicable notices and license
texts and satisfy the corresponding source requirements of the GPL. Keep the
source for the exact release available, including modifications and build files.
Packaged Qt, OpenSSL and other dependencies have their own distribution terms.
The root `LICENSE` is intentionally kept as the unmodified upstream GPL text.

Further reading: [GNU GPL v2 FAQ](https://www.gnu.org/licenses/old-licenses/gpl-2.0-faq.html).
