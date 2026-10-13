/*
 * FlowPlus -- mouse, keyboard, and file sharing utility
 * SPDX-FileCopyrightText: (C) 2026 FlowPlus Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "NativeDragSource.h"

#include <QFileInfo>

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

namespace deskflow::gui::transfer {

QStringList pollNativeDragSource(DragSourceState &state, bool localScreenActive)
{
  @autoreleasepool {
    NSPasteboard *pasteboard = [NSPasteboard pasteboardWithName:NSPasteboardNameDrag];
    // Combined-session state includes events injected by the sharing server
    // when this Mac is a client, as well as its own physical mouse events.
    const bool leftDown = CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState, kCGMouseButtonLeft);
    if (!state.observe(pasteboard.changeCount, leftDown, localScreenActive)) {
      return {};
    }

    NSArray *urls = [pasteboard readObjectsForClasses:@[ [NSURL class] ]
                                              options:@{
                                                NSPasteboardURLReadingFileURLsOnlyKey : @YES
                                              }];
    QStringList files;
    for (NSURL *url in urls) {
      if (!url.isFileURL) {
        continue;
      }
      const auto path = QString::fromNSString(url.path);
      const QFileInfo file(path);
      // Promised files and directories need a separate protocol. Never turn a
      // partial selection into a transfer that silently omits some entries.
      if (!file.isFile() || !file.isReadable()) {
        return {};
      }
      if (!files.contains(path)) {
        files.append(path);
      }
    }
    return files;
  }
}

} // namespace deskflow::gui::transfer
