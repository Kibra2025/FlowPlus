/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "VersionChecker.h"

#include "common/Settings.h"
#include "common/VersionInfo.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QVersionNumber>
#include <climits>

VersionChecker::VersionChecker(QObject *parent) : QObject(parent), m_network{new QNetworkAccessManager(this)}
{
  connect(m_network, &QNetworkAccessManager::finished, this, &VersionChecker::replyFinished, Qt::UniqueConnection);
}

void VersionChecker::checkLatest() const
{
  const QString url = Settings::value(Settings::Gui::UpdateCheckUrl).toString();
  qDebug("checking for updates at: %s", qPrintable(url));
  auto request = QNetworkRequest(url);
  auto userAgent = QString("%1 %2 on %3").arg(kAppName, kVersion, QSysInfo::prettyProductName());
  request.setHeader(QNetworkRequest::UserAgentHeader, userAgent);
  request.setRawHeader("X-FlowPlus-Version", kVersion);
  request.setRawHeader("X-FlowPlus-Language", qPrintable(QLocale::system().name()));
  request.setTransferTimeout(15000);
  m_network->get(request);
}

void VersionChecker::replyFinished(QNetworkReply *reply)
{
  reply->deleteLater();
  const auto httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  if (reply->error() != QNetworkReply::NoError) {
    qWarning("version check server error: %s", qPrintable(reply->errorString()));
    qWarning("version check server response: %s", qPrintable(QString(reply->readAll())));
    qWarning("error checking for updates, http status: %d", httpStatus);
    return;
  }

  qDebug("version check server success, http status: %d", httpStatus);

  const auto response = reply->readAll().trimmed();
  const auto document = QJsonDocument::fromJson(response);
  const auto newestVersion = document.isObject()
                                 ? document.object().value(QStringLiteral("tag_name")).toString().trimmed()
                                 : QString::fromUtf8(response);
  qDebug("version check response: %s", qPrintable(newestVersion));

  if (newestVersion.isEmpty()) {
    qWarning() << "version check is response is empty";
    return;
  }

  if (compareVersions(kVersion, newestVersion) > 0) {
    qWarning().noquote() //
        << QStringLiteral("current version %1 out of date, update available: %2").arg(kVersion, newestVersion);
    Q_EMIT updateFound(newestVersion);
  } else {
    qDebug().noquote() << QStringLiteral("current version %1 is upto date").arg(kVersion);
  }
}

int VersionChecker::getStageVersion(QString stage)
{
  const char *stableName = "stable";
  const char *rcName = "rc";
  const char *betaName = "beta";

  // use max int for stable so it's always the highest value.
  const int stableValue = INT_MAX;
  const int rcValue = 2;
  const int betaValue = 1;
  const int otherValue = 0;

  if (stage.isEmpty() || stage == stableName) {
    return stableValue;
  } else if (stage.startsWith(rcName, Qt::CaseInsensitive)) {
    static const QRegularExpression re(QStringLiteral("^\\d*$"));
    auto match = re.match(stage.mid(2));
    if (match.hasMatch()) {
      // return the rc value plus the rc number (e.g. 2 + 1)
      // this should be ok since stable is max int.
      return rcValue + qMin(match.captured(0).toInt(), INT_MAX - rcValue - 1);
    }
  } else if (stage == betaName) {
    return betaValue;
  }

  return otherValue;
}

int VersionChecker::compareVersions(const QString &left, const QString &right)
{
  static const QRegularExpression versionPattern(QStringLiteral("^v?(\\d+(?:\\.\\d+){2,3})(?:-([A-Za-z0-9.]+))?$"));
  const auto leftMatch = versionPattern.match(left.trimmed());
  const auto rightMatch = versionPattern.match(right.trimmed());
  if (!leftMatch.hasMatch() || !rightMatch.hasMatch()) {
    qWarning() << "ignoring invalid version response";
    return 0;
  }

  const auto leftNumber = QVersionNumber::fromString(leftMatch.captured(1)).normalized();
  const auto rightNumber = QVersionNumber::fromString(rightMatch.captured(1)).normalized();
  const auto comparison = QVersionNumber::compare(rightNumber, leftNumber);
  if (comparison != 0)
    return comparison > 0 ? 1 : -1;

  const auto leftStage = getStageVersion(leftMatch.captured(2));
  const auto rightStage = getStageVersion(rightMatch.captured(2));
  if (leftStage == rightStage)
    return 0;
  return rightStage > leftStage ? 1 : -1;
}
