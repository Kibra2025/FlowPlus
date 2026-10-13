/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2024 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2026 FlowPlus contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QApplication>
#include <QDir>
#include <QFileInfoList>
#include <QFontDatabase>
#include <QIcon>
#include <QPalette>
#include <QStyleHints>

#include "common/Constants.h"

namespace deskflow::gui {

// A shared palette keeps dialogs, menus and the main workspace consistent.
// Use Qt's Fusion controls so the explicit dark palette also works on a light OS.
inline void applyFlowPlusTheme()
{
  QApplication::setStyle(QStringLiteral("fusion"));
  QPalette palette;
  palette.setColor(QPalette::Window, QColor("#111214"));
  palette.setColor(QPalette::WindowText, QColor("#f5f5f1"));
  palette.setColor(QPalette::Base, QColor("#17191c"));
  palette.setColor(QPalette::AlternateBase, QColor("#202226"));
  palette.setColor(QPalette::Text, QColor("#f5f5f1"));
  palette.setColor(QPalette::PlaceholderText, QColor("#a6a9b1"));
  palette.setColor(QPalette::Button, QColor("#25282d"));
  palette.setColor(QPalette::ButtonText, QColor("#f5f5f1"));
  palette.setColor(QPalette::Highlight, QColor("#b8a0ff"));
  palette.setColor(QPalette::HighlightedText, QColor("#141218"));
  palette.setColor(QPalette::Link, QColor("#c8b7ff"));
  palette.setColor(QPalette::ToolTipBase, QColor("#25282d"));
  palette.setColor(QPalette::ToolTipText, QColor("#f5f5f1"));
  palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#858991"));
  palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#858991"));
  palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#858991"));
  QApplication::setPalette(palette);
  qApp->setStyleSheet(QStringLiteral(R"(
    QToolTip { color: #f5f5f1; background: #25282d; border: 1px solid #555961; padding: 6px; }
    QWidget#topLevelWidget { background: #111214; }
    QFrame#connectionPanel, QFrame#transferPanel { background: #191b1f; border: 1px solid #34373e; border-radius: 12px; }
    QLabel#brandName { color: #f5f5f1; font-size: 34px; font-weight: 800; }
    QLabel#brandAccent { color: #c4a7ff; font-size: 38px; font-weight: 500; }
    QLabel#workspaceSubtitle { color: #a6a9b1; font-size: 14px; }
    QLabel#connectionSection, QLabel#transferSection { color: #b8a0ff; font-size: 11px; font-weight: 700; letter-spacing: 2px; }
    QLabel#connectionTitle, QLabel#transferTitle { font-size: 20px; font-weight: 600; }
    QLabel#connectionHint, QLabel#transferHint, QLabel#labelComputerName, QLabel#lblIpAddresses { color: #a6a9b1; }
    QLabel#lblComputerName { font-weight: 600; }
    QLabel#connectionBadge { color: #c6c9d0; background: #25282d; border: 1px solid #3a3e46; border-radius: 12px; padding: 6px 12px; }
    QLabel#connectionBadge[connected="true"] { color: #d4f5a0; border-color: #55643c; background: #27301f; }
    QRadioButton#rbModeServer, QRadioButton#rbModeClient { background: #22252a; border: 1px solid #3b3f47; border-radius: 8px; padding: 16px; spacing: 10px; min-height: 42px; }
    QRadioButton#rbModeServer:checked, QRadioButton#rbModeClient:checked { background: #30293f; border-color: #b8a0ff; }
    QRadioButton#rbModeServer:hover, QRadioButton#rbModeClient:hover { border-color: #b8a0ff; }
    QRadioButton#rbModeServer:focus, QRadioButton#rbModeClient:focus { border: 2px solid #d3c2ff; padding: 15px; }
    QMainWindow QPushButton { background: #282b30; color: #f5f5f1; border: 1px solid #434750; border-radius: 6px; padding: 8px 12px; }
    QMainWindow QPushButton:hover { background: #343740; border-color: #87818f; }
    QMainWindow QPushButton:pressed { background: #3f3b4a; }
    QMainWindow QPushButton:focus { border-color: #c4a7ff; }
    QMainWindow QPushButton:disabled { color: #858991; background: #202226; border-color: #30343a; }
    QPushButton#btnToggleCore, QPushButton#sendFilesButton { background: #edece7; color: #151619; border-color: #edece7; font-weight: 600; padding: 9px 20px; }
    QPushButton#btnToggleCore:hover, QPushButton#sendFilesButton:hover { background: #d7c8ff; border-color: #d7c8ff; }
    QPushButton#btnToggleCore:disabled, QPushButton#sendFilesButton:disabled { background: #303238; color: #93969d; border-color: #434750; }
    QMainWindow QLineEdit { background: #111316; border: 1px solid #4d515b; border-radius: 6px; padding: 8px; selection-background-color: #b8a0ff; selection-color: #141218; }
    QMainWindow QLineEdit:focus { border-color: #c4a7ff; }
    QStatusBar { background: #17191c; border-top: 1px solid #34373e; color: #afb3bc; }
    QStatusBar::item { border: none; }
    QStatusBar QPushButton, QDockWidget QPushButton, QPushButton#btnEditName { padding: 2px; border: none; background: transparent; }
    QMenuBar { background: #111214; color: #c6c9d0; }
    QMenuBar::item:selected { background: #30293f; }
    QMenu { background: #202226; border: 1px solid #434750; }
    QMenu::item:selected { background: #b8a0ff; color: #141218; }
  )"));
}

/**
 * @brief Detects dark mode in a universal manner (all Qt versions).
 * Until better platform support is added, it's more reliable to use the old way (compare text and window lightness),
 * because the newer versions in Qt 6.5+ are not always correct and some return `UnknownScheme`.
 * https://www.qt.io/blog/dark-mode-on-windows-11-with-qt-6.5
 */
inline bool isDarkMode()
{
  const QPalette defaultPalette;
  const auto text = defaultPalette.color(QPalette::WindowText);
  const auto window = defaultPalette.color(QPalette::Window);
  return text.lightness() > window.lightness();
}
/**
 * @brief get a string for the iconMode
 * @returns "dark" or "light"
 */
inline QString iconMode()
{
  return isDarkMode() ? QStringLiteral("dark") : QStringLiteral("light");
}

inline void updateIconTheme()
{
  // Sets the fallback icon path and fallback theme
  const auto themeName = QStringLiteral("%1-%2").arg(kIconThemeId, iconMode());
  if (QIcon::themeName().isEmpty() || QIcon::themeName().startsWith(kIconThemeId))
    QIcon::setThemeName(themeName);
  else
    QIcon::setFallbackThemeName(themeName);
  QIcon::setFallbackSearchPaths({QStringLiteral(":/icons/%1").arg(themeName)});
}
} // namespace deskflow::gui

inline QFont fixedFont()
{
#if defined(Q_OS_WIN)
  QFont f({"Hack", "Liberation Mono", "Monospace", "Andale Mono"});
  f.setStyleHint(QFont::Monospace);
#else
  QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
#endif

#if defined(Q_OS_MAC)
  f.setPointSize(12);
#endif
  return f;
}
