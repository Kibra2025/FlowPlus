/*
 * FlowPlus -- mouse, keyboard, and file sharing utility
 * SPDX-FileCopyrightText: (C) 2026 FlowPlus Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QLabel>
#include <QPoint>
#include <QProgressBar>
#include <QTimer>
#include <QWidget>

namespace deskflow::gui::transfer {

class DragOverlayWidget : public QWidget
{
  Q_OBJECT

public:
  enum class State
  {
    Hidden,
    Dragging,
    Transferring,
    Completed
  };

  explicit DragOverlayWidget(QWidget *parent = nullptr);
  ~DragOverlayWidget() override = default;

  State state() const
  {
    return m_state;
  }

  void showDragging(const QString &summaryText, const QString &subtitleText);
  void showTransferring(const QString &fileName, int percent);
  void showCompleted(const QString &message);
  void hideOverlay();

Q_SIGNALS:
  void dropDetected(const QPoint &screenPos);

protected:
  void paintEvent(QPaintEvent *event) override;

private Q_SLOTS:
  void onCursorTrackTick();

private:
  static bool isLeftMouseButtonDown();
  void positionNearCursor();

  State m_state = State::Hidden;
  QLabel *m_titleLabel = nullptr;
  QLabel *m_subtitleLabel = nullptr;
  QProgressBar *m_progressBar = nullptr;
  QTimer *m_cursorTimer = nullptr;
  QTimer *m_autoHideTimer = nullptr;
  bool m_sawMouseDown = false;
};

} // namespace deskflow::gui::transfer
