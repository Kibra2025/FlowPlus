/*
 * FlowPlus -- mouse, keyboard and file sharing
 * SPDX-FileCopyrightText: (C) 2026 FlowPlus contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AccessibilitySetupDialog.h"

#include <QLabel>
#include <QPushButton>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QtTest>

using deskflow::gui::AccessibilitySetupDialog;

class AccessibilitySetupDialogTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void deniedPermissionKeepsAppVisibleUntilGranted()
  {
    bool trusted = false;
    int prompts = 0;
    AccessibilitySetupDialog dialog("FlowPlus", [&] { return trusted; }, [&] { ++prompts; }, [] { return true; });
    QSignalSpy finished(&dialog, &QDialog::finished);
    dialog.show();
    QTRY_COMPARE(prompts, 1);
    QVERIFY(dialog.isVisible());
    QVERIFY(finished.isEmpty());
    trusted = true;
    QTRY_COMPARE(finished.size(), 1);
    QCOMPARE(dialog.result(), QDialog::Accepted);
    QCOMPARE(prompts, 1);
  }

  void alreadyGrantedDoesNotPrompt()
  {
    int prompts = 0;
    AccessibilitySetupDialog dialog("FlowPlus", [] { return true; }, [&] { ++prompts; }, [] { return true; });
    QSignalSpy finished(&dialog, &QDialog::finished);
    dialog.show();
    QTRY_COMPARE(finished.size(), 1);
    QCOMPARE(dialog.result(), QDialog::Accepted);
    QCOMPARE(prompts, 0);
  }

  void manualRecheckDoesNotRepeatTheSystemPrompt()
  {
    bool trusted = false;
    int prompts = 0;
    AccessibilitySetupDialog dialog("FlowPlus", [&] { return trusted; }, [&] { ++prompts; }, [] { return true; });
    QSignalSpy finished(&dialog, &QDialog::finished);
    dialog.show();
    QTRY_COMPARE(prompts, 1);
    auto *check = dialog.findChild<QPushButton *>(QStringLiteral("checkPermissionButton"));
    QVERIFY(check);
    check->click();
    check->click();
    QVERIFY(dialog.isVisible());
    QVERIFY(finished.isEmpty());
    QCOMPARE(prompts, 1);
    trusted = true;
    check->click();
    QCOMPARE(finished.size(), 1);
    QCOMPARE(dialog.result(), QDialog::Accepted);
  }

  void settingsFailureShowsManualInstructions()
  {
    int settingsRequests = 0;
    AccessibilitySetupDialog dialog(
        "FlowPlus", [] { return false; }, [] {},
        [&] {
          ++settingsRequests;
          return false;
        }
    );
    dialog.show();
    auto *settings = dialog.findChild<QPushButton *>(QStringLiteral("openSettingsButton"));
    QVERIFY(settings);
    settings->click();
    QCOMPARE(settingsRequests, 1);
    auto *status = dialog.findChild<QLabel *>(QStringLiteral("permissionStatus"));
    QVERIFY(status);
    QVERIFY(status->text().contains(QStringLiteral("Apple menu")));
    QVERIFY(dialog.isVisible());
  }

  void explicitQuitStopsPermissionPolling()
  {
    int checks = 0;
    int prompts = 0;
    AccessibilitySetupDialog dialog(
        "FlowPlus",
        [&] {
          ++checks;
          return false;
        },
        [&] { ++prompts; }, [] { return true; }
    );
    QSignalSpy finished(&dialog, &QDialog::finished);
    dialog.show();
    QTRY_COMPARE(prompts, 1);
    auto *quit = dialog.findChild<QPushButton *>(QStringLiteral("quitButton"));
    QVERIFY(quit);
    quit->click();
    QCOMPARE(finished.size(), 1);
    QCOMPARE(dialog.result(), QDialog::Rejected);
    const auto checksWhenClosed = checks;
    QTest::qWait(1100);
    QCOMPARE(checks, checksWhenClosed);
  }

  void startupKeepsTheApplicationAliveForItsMainWindow()
  {
    const bool originalQuitPolicy = QApplication::quitOnLastWindowClosed();
    const auto restoreQuitPolicy =
        qScopeGuard([originalQuitPolicy] { QApplication::setQuitOnLastWindowClosed(originalQuitPolicy); });
    QApplication::setQuitOnLastWindowClosed(true);
    bool trusted = false;
    bool automaticQuitWasDisabled = false;
    AccessibilitySetupDialog dialog("FlowPlus", [&] { return trusted; }, [] {}, [] { return true; });
    QTimer::singleShot(0, &dialog, [&] {
      automaticQuitWasDisabled = !QApplication::quitOnLastWindowClosed();
      trusted = true;
      dialog.findChild<QPushButton *>(QStringLiteral("checkPermissionButton"))->click();
    });
    QCOMPARE(dialog.execForStartup(), QDialog::Accepted);
    QVERIFY(automaticQuitWasDisabled);
    QVERIFY(QApplication::quitOnLastWindowClosed());

    // Exercise the same sequence as main(): the setup closes before the main
    // window exists, and only then does the application's event loop start.
    QWidget mainWindow;
    mainWindow.show();
    bool mainWindowKeptRunning = false;
    QTimer::singleShot(50, &mainWindow, [&] {
      mainWindowKeptRunning = mainWindow.isVisible();
      QApplication::quit();
    });
    QCOMPARE(QApplication::exec(), 0);
    QVERIFY(mainWindowKeptRunning);
  }

  void cancellingStartupPreservesAnExistingNoQuitPolicy()
  {
    const bool originalQuitPolicy = QApplication::quitOnLastWindowClosed();
    const auto restoreQuitPolicy =
        qScopeGuard([originalQuitPolicy] { QApplication::setQuitOnLastWindowClosed(originalQuitPolicy); });
    QApplication::setQuitOnLastWindowClosed(false);
    AccessibilitySetupDialog dialog("FlowPlus", [] { return false; }, [] {}, [] { return true; });
    QTimer::singleShot(0, &dialog, &QDialog::reject);
    QCOMPARE(dialog.execForStartup(), QDialog::Rejected);
    QVERIFY(!QApplication::quitOnLastWindowClosed());
  }
};

QTEST_MAIN(AccessibilitySetupDialogTests)
#include "AccessibilitySetupDialogTests.moc"
