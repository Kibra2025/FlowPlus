/*
 * FlowPlus -- mouse, keyboard, and file sharing utility
 * SPDX-FileCopyrightText: (C) 2026 FlowPlus Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "gui/transfer/LocalSendService.h"

#include <QTemporaryDir>
#include <QTest>

#include <memory>

class LocalSendServiceTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void init();
  void cleanup();
  void unsolicitedUploadRejected();
  void authorizationMatchesManifestAndIsOneShot();
  void authorizationRequiresTheApprovedSourceAddress();
  void invalidDestinationRejectedWithoutConsumingGrant();
  void binaryBatchPreservesExistingFileAndSeparatesSenderCompletion();
  void interruptedUploadRemovesTemporaryFiles();
  void cancellationRemovesEntireStagedBatch();
  void uploadRequiresExactSizeAndToken();
  void malformedRequests_data();
  void malformedRequests();
  void senderDoesNotFallbackAfterPolicyRejection();
  void senderCanFallbackBeforeUploadOnConnectionFailure();
  void senderNeverFallbacksAfterUploadFailure();
  void cancelledSenderStopsOutstandingRequest();
  void invalidLocalFilesReportFailure();

private:
  QByteArray request(const QByteArray &target, const QByteArray &body = {}, const QByteArray &extraHeaders = {});
  QByteArray rawRequest(const QByteArray &requestBytes);
  QJsonObject prepare(const QString &transferId, const QJsonArray &manifest);
  QByteArray uploadTarget(const QJsonObject &prepared, const QString &fileId) const;
  QStringList downloads() const;

  std::unique_ptr<QTemporaryDir> m_directory;
  std::unique_ptr<deskflow::gui::transfer::LocalSendService> m_service;
};
