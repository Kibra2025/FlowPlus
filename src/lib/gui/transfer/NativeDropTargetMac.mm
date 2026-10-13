/*
 * FlowPlus -- mouse, keyboard, and file sharing utility
 * SPDX-FileCopyrightText: (C) 2026 FlowPlus Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "NativeDropTarget.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>

#import <ApplicationServices/ApplicationServices.h>
#import <Cocoa/Cocoa.h>

namespace deskflow::gui::transfer {
namespace {

QString directoryFromAttribute(AXUIElementRef element, CFStringRef attribute)
{
  CFTypeRef value = nullptr;
  if (AXUIElementCopyAttributeValue(element, attribute, &value) != kAXErrorSuccess || !value) {
    return {};
  }
  QUrl url;
  if (CFGetTypeID(value) == CFStringGetTypeID()) {
    url = QUrl(QString::fromCFString(static_cast<CFStringRef>(value)));
  } else if (CFGetTypeID(value) == CFURLGetTypeID()) {
    url = QUrl::fromCFURL(static_cast<CFURLRef>(value));
  }
  CFRelease(value);
  if (!url.isLocalFile()) {
    return {};
  }
  const QFileInfo info(url.toLocalFile());
  return info.isDir() ? info.canonicalFilePath() : QString();
}

QString finderDirectory(pid_t process, const CGPoint &point, bool &desktopBackground)
{
  desktopBackground = false;
  AXUIElementRef app = AXUIElementCreateApplication(process);
  // A busy Finder must not stall the input/transfer event loop indefinitely.
  AXUIElementSetMessagingTimeout(app, 0.25f);
  AXUIElementRef element = nullptr;
  AXUIElementCopyElementAtPosition(app, point.x, point.y, &element);
  CFRelease(app);
  QElapsedTimer deadline;
  deadline.start();
  for (int depth = 0; element && depth < 16 && deadline.elapsed() < 400; ++depth) {
    AXUIElementSetMessagingTimeout(element, 0.1f);
    auto directory = directoryFromAttribute(element, kAXURLAttribute);
    if (!directory.isEmpty()) {
      CFRelease(element);
      return directory;
    }
    CFTypeRef role = nullptr;
    AXUIElementCopyAttributeValue(element, kAXRoleAttribute, &role);
    if (role) {
      const bool item = CFEqual(role, kAXImageRole) || CFEqual(role, kAXRowRole) || CFEqual(role, kAXButtonRole) ||
                        CFEqual(role, kAXTextFieldRole);
      if (item) {
        CFRelease(role);
        CFRelease(element);
        // A folder icon without an exposed filesystem URL is ambiguous. Do not
        // walk past it and silently put the file in its containing directory.
        desktopBackground = false;
        return {};
      }
      desktopBackground = desktopBackground || CFEqual(role, kAXScrollAreaRole) || CFEqual(role, kAXListRole);
      CFRelease(role);
    }
    if (directory.isEmpty()) {
      directory = directoryFromAttribute(element, kAXDocumentAttribute);
    }
    if (!directory.isEmpty()) {
      CFRelease(element);
      return directory;
    }
    CFTypeRef parent = nullptr;
    AXUIElementCopyAttributeValue(element, kAXParentAttribute, &parent);
    CFRelease(element);
    element = nullptr;
    if (parent) {
      if (CFGetTypeID(parent) == AXUIElementGetTypeID()) {
        element = static_cast<AXUIElementRef>(const_cast<void *>(parent));
      } else {
        CFRelease(parent);
      }
    }
  }
  if (element) {
    CFRelease(element);
  }
  return {};
}

} // namespace

NativeDropTarget captureNativeDropTarget()
{
  @autoreleasepool {
    if (!AXIsProcessTrusted()) {
      return {};
    }
    CGEventRef event = CGEventCreate(nullptr);
    if (!event) {
      return {};
    }
    const auto point = CGEventGetLocation(event);
    CFRelease(event);
    CFArrayRef windows = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly, kCGNullWindowID);
    if (!windows) {
      return {};
    }
    NativeDropTarget result;
    for (CFIndex index = 0; index < CFArrayGetCount(windows); ++index) {
      auto window = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(windows, index));
      auto owner = static_cast<CFNumberRef>(CFDictionaryGetValue(window, kCGWindowOwnerPID));
      auto layerValue = static_cast<CFNumberRef>(CFDictionaryGetValue(window, kCGWindowLayer));
      auto boundsValue = static_cast<CFDictionaryRef>(CFDictionaryGetValue(window, kCGWindowBounds));
      pid_t process = 0;
      int layer = 0;
      CGRect bounds = {};
      if (!owner || !layerValue || !boundsValue || !CFNumberGetValue(owner, kCFNumberIntType, &process) ||
          !CFNumberGetValue(layerValue, kCFNumberIntType, &layer) ||
          !CGRectMakeWithDictionaryRepresentation(boundsValue, &bounds) || !CGRectContainsPoint(bounds, point)) {
        continue;
      }
      // The badge is offset from the pointer. Do not skip our own windows:
      // dropping on FlowPlus must not target a Finder window hidden behind it.
      NSRunningApplication *application = [NSRunningApplication runningApplicationWithProcessIdentifier:process];
      if ([application.bundleIdentifier isEqualToString:@"com.apple.finder"]) {
        bool desktopBackground = false;
        result.directory = finderDirectory(process, point, desktopBackground);
        if (result.directory.isEmpty() && desktopBackground &&
            layer == CGWindowLevelForKey(kCGDesktopIconWindowLevelKey)) {
          const QFileInfo desktop(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
          result.directory = desktop.isDir() ? desktop.canonicalFilePath() : QString();
        }
        break;
      }
      if (layer >= 0) {
        break; // Another app, the Dock or a menu is the actual drop target.
      }
    }
    CFRelease(windows);
    return result;
  }
}

} // namespace deskflow::gui::transfer
