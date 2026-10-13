/*
 * FlowPlus -- mouse, keyboard and file sharing
 * SPDX-FileCopyrightText: (C) 2026 FlowPlus contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QDialog>
#include <QTimer>

#include <functional>

class QLabel;

namespace deskflow::gui {

// Keep the permission UI independent of platform APIs so granting access while
// the dialog is open can be exercised without changing a machine's permissions.
class AccessibilitySetupDialog final : public QDialog
{
  Q_OBJECT

public:
  AccessibilitySetupDialog(
      const QString &appName, std::function<bool()> isTrusted, std::function<void()> requestPermission,
      std::function<bool()> openSettings, QWidget *parent = nullptr
  );

  // Closing the first window must not terminate the application before its
  // main window is created. Restore the normal policy when setup completes.
  int execForStartup();

protected:
  void showEvent(QShowEvent *event) override;

private:
  void checkPermission();

  std::function<bool()> m_isTrusted;
  std::function<void()> m_requestPermission;
  std::function<bool()> m_openSettings;
  QTimer m_poll;
  QLabel *m_status = nullptr;
  bool m_requested = false;
};

} // namespace deskflow::gui
