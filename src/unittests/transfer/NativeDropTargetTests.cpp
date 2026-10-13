/*
 * FlowPlus -- mouse, keyboard, and file sharing utility
 * SPDX-FileCopyrightText: (C) 2026 FlowPlus Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/transfer/NativeDropTarget.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

namespace deskflow::gui::transfer {
namespace {

bool writeFile(const QString &path, const QByteArray &contents)
{
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

QByteArray readFile(const QString &path)
{
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

} // namespace

class NativeDropTargetTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void unsupportedTargetKeepsDownload();
  void missingTargetIsNotRecreated();
  void preserveExistingFilesAndRemoveDeliveredDownloads();
  void failedBatchRollsBackOnlyItsOwnCopies();
  void sameDirectoryDoesNotDuplicateFiles();
  void emptyFileIsPublished();
  void cancellationRollsBackPublishedFiles();
  void cancellationDuringCopyRemovesPartialFile();
  void changedDownloadIsPreservedAfterCommit();
  void cancellationAfterCommitPreservesDestinationAndDownload();
};

void NativeDropTargetTests::unsupportedTargetKeepsDownload()
{
  QTemporaryDir download;
  const auto path = QDir(download.path()).filePath(QStringLiteral("test.txt"));
  QVERIFY(writeFile(path, "original"));
  const auto result = placeReceivedFiles({path}, {});
  QVERIFY(!result.usedDropTarget);
  QCOMPARE(result.paths, QStringList{path});
  QCOMPARE(result.remainingDownloadPaths, QStringList{path});
  QCOMPARE(readFile(path), QByteArray("original"));
}

void NativeDropTargetTests::missingTargetIsNotRecreated()
{
  QTemporaryDir download;
  QTemporaryDir destination;
  const auto path = QDir(download.path()).filePath(QStringLiteral("test.txt"));
  QVERIFY(writeFile(path, "original"));
  const NativeDropTarget target{QFileInfo(destination.path()).canonicalFilePath()};
  QVERIFY(QDir().rmdir(destination.path()));
  const auto result = placeReceivedFiles({path}, target);
  QVERIFY(!result.usedDropTarget);
  QCOMPARE(result.paths, QStringList{path});
  QVERIFY(!QFileInfo::exists(target.directory));
}

void NativeDropTargetTests::preserveExistingFilesAndRemoveDeliveredDownloads()
{
  QTemporaryDir download;
  QTemporaryDir destination;
  const auto path = QDir(download.path()).filePath(QStringLiteral("photo.bin"));
  const auto existing = QDir(destination.path()).filePath(QStringLiteral("photo.bin"));
  const QByteArray binary("before\0after", 12);
  QVERIFY(writeFile(path, binary));
  QVERIFY(writeFile(existing, "keep"));
  const auto result = placeReceivedFiles({path}, {QFileInfo(destination.path()).canonicalFilePath()});
  QVERIFY(result.usedDropTarget);
  QCOMPARE(result.paths.size(), 1);
  QCOMPARE(QFileInfo(result.paths.first()).fileName(), QStringLiteral("photo (1).bin"));
  QCOMPARE(readFile(result.paths.first()), binary);
  QCOMPARE(readFile(existing), QByteArray("keep"));
  QVERIFY(!QFileInfo::exists(path));
  QVERIFY(result.remainingDownloadPaths.isEmpty());
  QCOMPARE(QDir(destination.path()).entryList(QDir::Files | QDir::Hidden).size(), 2);
}

void NativeDropTargetTests::failedBatchRollsBackOnlyItsOwnCopies()
{
  QTemporaryDir download;
  QTemporaryDir destination;
  const auto first = QDir(download.path()).filePath(QStringLiteral("first.txt"));
  const auto missing = QDir(download.path()).filePath(QStringLiteral("missing.txt"));
  const auto existing = QDir(destination.path()).filePath(QStringLiteral("first.txt"));
  QVERIFY(writeFile(first, "incoming"));
  QVERIFY(writeFile(existing, "keep"));
  const auto result = placeReceivedFiles({first, missing}, {QFileInfo(destination.path()).canonicalFilePath()});
  QVERIFY(!result.usedDropTarget);
  QCOMPARE(result.paths, (QStringList{first, missing}));
  QCOMPARE(readFile(first), QByteArray("incoming"));
  QCOMPARE(readFile(existing), QByteArray("keep"));
  QCOMPARE(QDir(destination.path()).entryList(QDir::Files | QDir::Hidden), QStringList{QStringLiteral("first.txt")});
}

void NativeDropTargetTests::sameDirectoryDoesNotDuplicateFiles()
{
  QTemporaryDir download;
  const auto path = QDir(download.path()).filePath(QStringLiteral("test.txt"));
  QVERIFY(writeFile(path, "original"));
  const auto result = placeReceivedFiles({path}, {QFileInfo(download.path()).canonicalFilePath()});
  QVERIFY(result.usedDropTarget);
  QCOMPARE(result.paths, QStringList{QFileInfo(path).canonicalFilePath()});
  QCOMPARE(QDir(download.path()).entryList(QDir::Files | QDir::Hidden).size(), 1);
  QVERIFY(result.remainingDownloadPaths.isEmpty());
}

void NativeDropTargetTests::emptyFileIsPublished()
{
  QTemporaryDir download;
  QTemporaryDir destination;
  const auto path = QDir(download.path()).filePath(QStringLiteral("empty.txt"));
  QVERIFY(writeFile(path, {}));
  const auto result = placeReceivedFiles({path}, {QFileInfo(destination.path()).canonicalFilePath()});
  QVERIFY(result.usedDropTarget);
  QCOMPARE(result.paths.size(), 1);
  QVERIFY(QFileInfo::exists(result.paths.first()));
  QCOMPARE(QFileInfo(result.paths.first()).size(), 0);
  QVERIFY(!QFileInfo::exists(path));
}

void NativeDropTargetTests::cancellationRollsBackPublishedFiles()
{
  QTemporaryDir download;
  QTemporaryDir destination;
  const auto first = QDir(download.path()).filePath(QStringLiteral("first.txt"));
  const auto second = QDir(download.path()).filePath(QStringLiteral("second.txt"));
  QVERIFY(writeFile(first, "first"));
  QVERIFY(writeFile(second, "second"));
  const auto published = QDir(destination.path()).filePath(QStringLiteral("first.txt"));
  const auto result = placeReceivedFiles({first, second}, {QFileInfo(destination.path()).canonicalFilePath()}, [&] {
    return QFileInfo::exists(published);
  });
  QVERIFY(!result.usedDropTarget);
  QCOMPARE(result.paths, (QStringList{first, second}));
  QCOMPARE(readFile(first), QByteArray("first"));
  QCOMPARE(readFile(second), QByteArray("second"));
  QVERIFY(QDir(destination.path()).entryList(QDir::Files | QDir::Hidden).isEmpty());
}

void NativeDropTargetTests::cancellationDuringCopyRemovesPartialFile()
{
  QTemporaryDir download;
  QTemporaryDir destination;
  const auto path = QDir(download.path()).filePath(QStringLiteral("large.bin"));
  const QByteArray contents(3 * 1024 * 1024, 'x');
  QVERIFY(writeFile(path, contents));
  int checks = 0;
  const auto result =
      placeReceivedFiles({path}, {QFileInfo(destination.path()).canonicalFilePath()}, [&] { return ++checks >= 4; });
  QVERIFY(!result.usedDropTarget);
  QCOMPARE(result.paths, QStringList{path});
  QCOMPARE(readFile(path), contents);
  QVERIFY(QDir(destination.path()).entryList(QDir::Files | QDir::Hidden).isEmpty());
}

void NativeDropTargetTests::changedDownloadIsPreservedAfterCommit()
{
  QTemporaryDir download;
  QTemporaryDir destination;
  const auto path = QDir(download.path()).filePath(QStringLiteral("test.txt"));
  // Placement returns paths under the canonical target directory. On macOS,
  // temporary directories can use /var, which resolves to /private/var.
  const QDir targetDirectory(QFileInfo(destination.path()).canonicalFilePath());
  const auto delivered = targetDirectory.filePath(QStringLiteral("test.txt"));
  QVERIFY(writeFile(path, "download"));
  bool changed = false;
  const auto result = placeReceivedFiles({path}, {targetDirectory.path()}, [&] {
    if (!changed && QFileInfo::exists(delivered)) {
      changed = writeFile(path, "local edit after copying");
    }
    return false;
  });
  QVERIFY(changed);
  QVERIFY(result.usedDropTarget);
  QCOMPARE(result.paths, QStringList{delivered});
  QCOMPARE(readFile(delivered), QByteArray("download"));
  QCOMPARE(readFile(path), QByteArray("local edit after copying"));
  QCOMPARE(result.remainingDownloadPaths, QStringList{path});
}

void NativeDropTargetTests::cancellationAfterCommitPreservesDestinationAndDownload()
{
  QTemporaryDir download;
  QTemporaryDir destination;
  const auto first = QDir(download.path()).filePath(QStringLiteral("first.txt"));
  const auto path = QDir(download.path()).filePath(QStringLiteral("test.txt"));
  const QDir targetDirectory(QFileInfo(destination.path()).canonicalFilePath());
  const auto firstDelivered = targetDirectory.filePath(QStringLiteral("first.txt"));
  const auto delivered = targetDirectory.filePath(QStringLiteral("test.txt"));
  QVERIFY(writeFile(first, "first"));
  QVERIFY(writeFile(path, "download"));
  // Interrupt once cleanup has removed one received copy. A rollback now would
  // lose that file: all destinations must remain, along with unremoved copies.
  const auto result =
      placeReceivedFiles({first, path}, {targetDirectory.path()}, [&] { return !QFileInfo::exists(first); });
  QVERIFY(result.usedDropTarget);
  QCOMPARE(result.paths, (QStringList{firstDelivered, delivered}));
  QVERIFY(!QFileInfo::exists(first));
  QCOMPARE(readFile(firstDelivered), QByteArray("first"));
  QCOMPARE(readFile(delivered), QByteArray("download"));
  QCOMPARE(readFile(path), QByteArray("download"));
  QCOMPARE(result.remainingDownloadPaths, QStringList{path});
}

} // namespace deskflow::gui::transfer

QTEST_GUILESS_MAIN(deskflow::gui::transfer::NativeDropTargetTests)
#include "NativeDropTargetTests.moc"
