/*
 * FlowPlus -- mouse, keyboard and file sharing
 * SPDX-FileCopyrightText: (C) 2026 FlowPlus contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AccessibilitySetupDialog.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QScopeGuard>
#include <QShowEvent>
#include <QVBoxLayout>

#include <utility>

namespace deskflow::gui {

AccessibilitySetupDialog::AccessibilitySetupDialog(
    const QString &appName, std::function<bool()> isTrusted, std::function<void()> requestPermission,
    std::function<bool()> openSettings, QWidget *parent
)
    : QDialog(parent),
      m_isTrusted(std::move(isTrusted)),
      m_requestPermission(std::move(requestPermission)),
      m_openSettings(std::move(openSettings))
{
  setWindowTitle(tr("Set up %1").arg(appName));
  setWindowFlag(Qt::WindowContextHelpButtonHint, false);
  setMinimumWidth(520);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(24, 24, 24, 24);
  layout->setSpacing(16);

  auto *heading = new QLabel(tr("Allow mouse and keyboard sharing"), this);
  auto font = heading->font();
  font.setPointSize(font.pointSize() + 4);
  font.setBold(true);
  heading->setFont(font);
  heading->setWordWrap(true);
  layout->addWidget(heading);

  auto *instructions = new QLabel(
      tr("%1 needs Accessibility access to share your mouse and keyboard.\n\n"
         "Open System Settings, then Privacy & Security > Accessibility, and enable %1. "
         "If it is missing, use + to add it from Applications.\n\n"
         "This window will continue automatically when access is available.")
          .arg(appName),
      this
  );
  instructions->setWordWrap(true);
  layout->addWidget(instructions);

  m_status = new QLabel(tr("Waiting for Accessibility access…"), this);
  m_status->setObjectName(QStringLiteral("permissionStatus"));
  m_status->setWordWrap(true);
  layout->addWidget(m_status);

  auto *help = new QLabel(
      tr("Already enabled? After replacing or re-signing %1, remove its old entry and add the app again. "
         "If macOS asks you to quit and reopen it, do so.")
          .arg(appName),
      this
  );
  help->setWordWrap(true);
  layout->addWidget(help);

  auto *buttons = new QDialogButtonBox(this);
  auto *quitButton = buttons->addButton(tr("Quit"), QDialogButtonBox::RejectRole);
  quitButton->setObjectName(QStringLiteral("quitButton"));
  auto *checkButton = buttons->addButton(tr("Check again"), QDialogButtonBox::ActionRole);
  checkButton->setObjectName(QStringLiteral("checkPermissionButton"));
  auto *settingsButton = buttons->addButton(tr("Open System Settings"), QDialogButtonBox::ActionRole);
  settingsButton->setObjectName(QStringLiteral("openSettingsButton"));
  settingsButton->setDefault(true);
  layout->addWidget(buttons);

  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(checkButton, &QPushButton::clicked, this, &AccessibilitySetupDialog::checkPermission);
  connect(settingsButton, &QPushButton::clicked, this, [this] {
    if (!m_openSettings()) {
      m_status->setText(
          tr("Could not open System Settings. Open it from the Apple menu, then select "
             "Privacy & Security > Accessibility.")
      );
    }
  });
  connect(&m_poll, &QTimer::timeout, this, &AccessibilitySetupDialog::checkPermission);
  connect(this, &QDialog::finished, &m_poll, &QTimer::stop);
  connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
    if (state == Qt::ApplicationActive && isVisible()) {
      checkPermission();
    }
  });
  m_poll.setInterval(1000);
}

int AccessibilitySetupDialog::execForStartup()
{
  const bool quitOnLastWindowClosed = QApplication::quitOnLastWindowClosed();
  const auto restoreQuitPolicy =
      qScopeGuard([quitOnLastWindowClosed] { QApplication::setQuitOnLastWindowClosed(quitOnLastWindowClosed); });
  QApplication::setQuitOnLastWindowClosed(false);
  return exec();
}

void AccessibilitySetupDialog::showEvent(QShowEvent *event)
{
  QDialog::showEvent(event);
  // The OS prompt is asynchronous. Keep our explanation visible while the user
  // changes settings instead of interpreting its immediate false return as an error.
  QTimer::singleShot(0, this, [this] {
    if (!isVisible()) {
      return;
    }
    if (m_isTrusted()) {
      accept();
      return;
    }
    if (!m_requested) {
      m_requested = true;
      m_requestPermission();
    }
    m_poll.start();
    checkPermission();
  });
}

void AccessibilitySetupDialog::checkPermission()
{
  if (m_isTrusted()) {
    m_poll.stop();
    accept();
  }
}

} // namespace deskflow::gui
