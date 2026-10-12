/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2024 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QString>

// important: this is used for settings paths on some platforms,
// and must not be a url. qt automatically converts this to reverse domain
// notation (rdn), e.g. io.github.kibra2025
const auto kOrgDomain = QStringLiteral("kibra2025.github.io");

const auto kUrlApp = QStringLiteral("https://github.com/Kibra2025/FlowPlus");
const auto kUrlHelp = QStringLiteral("%1#readme").arg(kUrlApp);
const auto kUrlDownload = QStringLiteral("%1/releases").arg(kUrlApp);

const auto kUrlUpdateCheck = QStringLiteral("https://api.github.com/repos/Kibra2025/FlowPlus/releases/latest");

#if defined(Q_OS_LINUX)
const auto kUrlGnomeTrayFix = QStringLiteral("https://extensions.gnome.org/extension/615/appindicator-support/");
#endif
