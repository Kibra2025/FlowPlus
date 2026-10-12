/*
 * SPDX-FileCopyrightText: (C) 2026 FlowPlus Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/VersionChecker.h"

#include <QTest>

class VersionCheckerTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void compareVersions_data();
  void compareVersions();
};

void VersionCheckerTests::compareVersions_data()
{
  QTest::addColumn<QString>("current");
  QTest::addColumn<QString>("release");
  QTest::addColumn<int>("expected");

  QTest::newRow("GitHub release tag") << "1.26.0.0" << "v1.27.0" << 1;
  QTest::newRow("same version") << "1.26.0" << "1.26.0" << 0;
  QTest::newRow("implicit zero build") << "1.26.0.0" << "v1.26.0" << 0;
  QTest::newRow("lower major higher minor") << "2.0.0" << "1.99.99" << -1;
  QTest::newRow("development build newer") << "1.26.0.9999" << "v1.26.0" << -1;
  QTest::newRow("release candidate number") << "1.26.0-rc1" << "1.26.0-rc2" << 1;
  QTest::newRow("stable after candidate") << "1.26.0-rc2" << "1.26.0" << 1;
  QTest::newRow("beta before stable") << "1.26.0" << "1.26.0-beta" << -1;
  QTest::newRow("malformed JSON response") << "1.26.0" << "{}" << 0;
  QTest::newRow("incomplete version") << "1.26.0" << "1" << 0;
  QTest::newRow("empty response") << "1.26.0" << "" << 0;
  QTest::newRow("candidate integer overflow") << "1.26.0" << "1.26.0-rc2147483647" << -1;
}

void VersionCheckerTests::compareVersions()
{
  QFETCH(QString, current);
  QFETCH(QString, release);
  QFETCH(int, expected);
  QCOMPARE(VersionChecker::compareVersions(current, release), expected);
}

QTEST_GUILESS_MAIN(VersionCheckerTests)
#include "VersionCheckerTests.moc"
