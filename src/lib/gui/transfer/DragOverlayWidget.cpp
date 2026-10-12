/*
 * FlowPlus -- mouse, keyboard, and file sharing utility
 * SPDX-FileCopyrightText: (C) 2026 FlowPlus Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "DragOverlayWidget.h"

#include <QCursor>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

#if defined(Q_OS_WIN)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace deskflow::gui::transfer {

DragOverlayWidget::DragOverlayWidget(QWidget *parent)
    : QWidget(
          parent,
          Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus |
              Qt::WindowTransparentForInput
      ),
      m_titleLabel{new QLabel(this)},
      m_subtitleLabel{new QLabel(this)},
      m_progressBar{new QProgressBar(this)},
      m_cursorTimer{new QTimer(this)},
      m_autoHideTimer{new QTimer(this)}
{
  setAttribute(Qt::WA_TranslucentBackground, true);
  setAttribute(Qt::WA_ShowWithoutActivating, true);
  setFixedWidth(290);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(14, 10, 14, 10);
  layout->setSpacing(4);

  m_titleLabel->setStyleSheet(QStringLiteral("color: #FFFFFF; font-weight: 600; font-size: 12px;"));
  m_titleLabel->setWordWrap(true);

  m_subtitleLabel->setStyleSheet(QStringLiteral("color: #B8C4D8; font-size: 11px;"));
  m_subtitleLabel->setWordWrap(true);

  m_progressBar->setRange(0, 100);
  m_progressBar->setValue(0);
  m_progressBar->setTextVisible(false);
  m_progressBar->setFixedHeight(6);
  m_progressBar->setStyleSheet(
      QStringLiteral("QProgressBar { background-color: rgba(255,255,255,0.18); border-radius: 3px; border: none; }"
                     "QProgressBar::chunk { background-color: #38BDF8; border-radius: 3px; }")
  );
  m_progressBar->hide();

  layout->addWidget(m_titleLabel);
  layout->addWidget(m_subtitleLabel);
  layout->addWidget(m_progressBar);

  m_cursorTimer->setInterval(16);
  connect(m_cursorTimer, &QTimer::timeout, this, &DragOverlayWidget::onCursorTrackTick);

  m_autoHideTimer->setSingleShot(true);
  connect(m_autoHideTimer, &QTimer::timeout, this, &DragOverlayWidget::hideOverlay);
}

void DragOverlayWidget::showDragging(const QString &summaryText, const QString &subtitleText)
{
  m_autoHideTimer->stop();
  m_state = State::Dragging;
  m_titleLabel->setText(summaryText);
  m_subtitleLabel->setText(subtitleText);
  m_progressBar->hide();
  adjustSize();

  m_dragStartElapsed.start();
  m_sawMouseDown = isLeftMouseButtonDown();
  m_lastCursorPos = QCursor::pos();
  m_stationaryTicks = 0;

  move(m_lastCursorPos + QPoint(18, 18));
  show();
  raise();
  m_cursorTimer->start();
}

void DragOverlayWidget::showTransferring(const QString &fileName, int percent)
{
  m_autoHideTimer->stop();
  m_cursorTimer->stop();
  m_state = State::Transferring;
  m_titleLabel->setText(tr("Trasferimento in corso: %1").arg(fileName));
  m_subtitleLabel->setText(tr("%1% completato via FlowPlus LocalSend").arg(percent));
  m_progressBar->setValue(qBound(0, percent, 100));
  m_progressBar->show();
  adjustSize();
  if (!isVisible()) {
    move(QCursor::pos() + QPoint(18, 18));
    show();
  }
  raise();
}

void DragOverlayWidget::showCompleted(const QString &message)
{
  m_cursorTimer->stop();
  m_state = State::Completed;
  m_titleLabel->setText(message);
  m_subtitleLabel->setText(tr("File pronto e rilasciato nella destinazione"));
  m_progressBar->setValue(100);
  m_progressBar->hide();
  adjustSize();
  if (!isVisible()) {
    move(QCursor::pos() + QPoint(18, 18));
    show();
  }
  raise();
  m_autoHideTimer->start(2200);
}

void DragOverlayWidget::hideOverlay()
{
  m_cursorTimer->stop();
  m_autoHideTimer->stop();
  m_state = State::Hidden;
  hide();
}

bool DragOverlayWidget::isLeftMouseButtonDown()
{
#if defined(Q_OS_WIN)
  return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
#else
  return (QGuiApplication::mouseButtons() & Qt::LeftButton) != 0;
#endif
}

void DragOverlayWidget::onCursorTrackTick()
{
  const QPoint pos = QCursor::pos();
  move(pos + QPoint(18, 18));

  if (m_state != State::Dragging) {
    return;
  }

  const bool leftDown = isLeftMouseButtonDown();
  if (leftDown) {
    m_sawMouseDown = true;
    m_stationaryTicks = 0;
  } else if (m_sawMouseDown && m_dragStartElapsed.elapsed() > 120) {
    // User had the mouse button held down and just released it on this screen
    m_cursorTimer->stop();
    Q_EMIT dropDetected(pos);
    return;
  } else if (!leftDown && m_dragStartElapsed.elapsed() > 750) {
    // Fallback if OS drag loop released button during screen transition:
    // trigger drop when the user pauses the cursor for ~400ms on the target window
    if ((pos - m_lastCursorPos).manhattanLength() <= 3) {
      ++m_stationaryTicks;
      if (m_stationaryTicks >= 25) {
        m_cursorTimer->stop();
        Q_EMIT dropDetected(pos);
        return;
      }
    } else {
      m_stationaryTicks = 0;
    }
  }

  m_lastCursorPos = pos;
}

void DragOverlayWidget::paintEvent(QPaintEvent *event)
{
  Q_UNUSED(event)
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);

  QPainterPath path;
  path.addRoundedRect(rect().adjusted(1, 1, -1, -1), 10.0, 10.0);

  painter.fillPath(path, QColor(15, 23, 42, 235));
  painter.setPen(QPen(QColor(56, 189, 248, 180), 1.4));
  painter.drawPath(path);
}

} // namespace deskflow::gui::transfer
