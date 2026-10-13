/*
 * FlowPlus -- mouse, keyboard, and file sharing utility
 * SPDX-FileCopyrightText: (C) 2026 FlowPlus Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "NativeDropTarget.h"
#include "TransferUtils.h"

#include <QFileInfo>
#include <QStandardPaths>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
// WIN32_LEAN_AND_MEAN excludes COM definitions from Windows.h. The Microsoft
// SDK's exdisp.h uses them in forward declarations before its own imports.
#include <objbase.h>

#include <exdisp.h>
#include <oleacc.h>
#include <servprov.h>
#include <shlguid.h>
#include <shlobj.h>

#include <memory>

namespace deskflow::gui::transfer {
namespace {

struct ReleaseCom
{
  void operator()(IUnknown *object) const
  {
    if (object) {
      object->Release();
    }
  }
};

template <typename T> using ComPtr = std::unique_ptr<T, ReleaseCom>;

class ComApartment
{
public:
  ComApartment() : m_result(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))
  {
  }
  ~ComApartment()
  {
    if (SUCCEEDED(m_result)) {
      CoUninitialize();
    }
  }
  bool available() const
  {
    return SUCCEEDED(m_result) || m_result == RPC_E_CHANGED_MODE;
  }

private:
  HRESULT m_result;
};

NativeDropTarget directoryTarget(const QString &path)
{
  const QFileInfo info(path);
  return {info.isDir() ? info.canonicalFilePath() : QString()};
}

NativeDropTarget shellItemAtPoint(const NativeDropTarget &folder, HWND root, const POINT &point)
{
  if (folder.directory.isEmpty()) {
    return {};
  }
  IAccessible *rawAccessible = nullptr;
  VARIANT child;
  VariantInit(&child);
  if (FAILED(AccessibleObjectFromPoint(point, &rawAccessible, &child)) || !rawAccessible) {
    VariantClear(&child);
    return {};
  }
  ComPtr<IAccessible> accessible(rawAccessible);
  HWND accessibleWindow = nullptr;
  if (FAILED(WindowFromAccessibleObject(accessible.get(), &accessibleWindow)) ||
      GetAncestor(accessibleWindow, GA_ROOT) != root) {
    VariantClear(&child);
    return {};
  }
  for (int depth = 0; accessible && depth < 12; ++depth) {
    VARIANT role;
    VariantInit(&role);
    const auto roleResult = accessible->get_accRole(child, &role);
    const auto roleId = SUCCEEDED(roleResult) && role.vt == VT_I4 ? role.lVal : 0;
    VariantClear(&role);
    if (roleId == ROLE_SYSTEM_LISTITEM || roleId == ROLE_SYSTEM_OUTLINEITEM) {
      BSTR name = nullptr;
      const auto nameResult = accessible->get_accName(child, &name);
      const auto itemName = SUCCEEDED(nameResult) && name ? QString::fromWCharArray(name) : QString();
      SysFreeString(name);
      VariantClear(&child);
      // Explorer may expose a localized/virtual display name. Resolve only an
      // exact real child directory; an ambiguous item must use the fallback.
      return isPortableFileName(itemName) ? directoryTarget(QDir(folder.directory).filePath(itemName))
                                          : NativeDropTarget{};
    }
    if (roleId == ROLE_SYSTEM_LIST && child.vt == VT_I4 && child.lVal == CHILDID_SELF) {
      VariantClear(&child);
      return folder; // A positively identified empty part of the folder view.
    }
    if (roleId != ROLE_SYSTEM_TEXT && roleId != ROLE_SYSTEM_STATICTEXT && roleId != ROLE_SYSTEM_CELL) {
      break;
    }
    IDispatch *rawParent = nullptr;
    if (FAILED(accessible->get_accParent(&rawParent)) || !rawParent) {
      break;
    }
    ComPtr<IDispatch> parent(rawParent);
    IAccessible *rawParentAccessible = nullptr;
    if (FAILED(parent->QueryInterface(IID_IAccessible, reinterpret_cast<void **>(&rawParentAccessible)))) {
      break;
    }
    accessible.reset(rawParentAccessible);
    VariantClear(&child);
    child.vt = VT_I4;
    child.lVal = CHILDID_SELF;
  }
  VariantClear(&child);
  return {};
}

NativeDropTarget explorerDirectory(IDispatch *dispatch, HWND root, const POINT &point)
{
  IWebBrowserApp *rawBrowser = nullptr;
  if (FAILED(dispatch->QueryInterface(IID_IWebBrowserApp, reinterpret_cast<void **>(&rawBrowser)))) {
    return {};
  }
  ComPtr<IWebBrowserApp> browser(rawBrowser);
  SHANDLE_PTR window = 0;
  if (FAILED(browser->get_HWND(&window)) || reinterpret_cast<HWND>(window) != root) {
    return {};
  }
  IServiceProvider *rawProvider = nullptr;
  if (FAILED(browser->QueryInterface(IID_IServiceProvider, reinterpret_cast<void **>(&rawProvider)))) {
    return {};
  }
  ComPtr<IServiceProvider> provider(rawProvider);
  IShellBrowser *rawShell = nullptr;
  if (FAILED(provider->QueryService(SID_STopLevelBrowser, IID_IShellBrowser, reinterpret_cast<void **>(&rawShell)))) {
    return {};
  }
  ComPtr<IShellBrowser> shell(rawShell);
  IShellView *rawView = nullptr;
  if (FAILED(shell->QueryActiveShellView(&rawView))) {
    return {};
  }
  ComPtr<IShellView> view(rawView);
  HWND viewWindow = nullptr;
  RECT bounds = {};
  if (FAILED(view->GetWindow(&viewWindow)) || !IsWindowVisible(viewWindow) || !GetClientRect(viewWindow, &bounds)) {
    return {};
  }
  MapWindowPoints(viewWindow, nullptr, reinterpret_cast<POINT *>(&bounds), 2);
  if (!PtInRect(&bounds, point)) {
    return {};
  }
  IFolderView *rawFolderView = nullptr;
  if (FAILED(view->QueryInterface(IID_IFolderView, reinterpret_cast<void **>(&rawFolderView)))) {
    return {};
  }
  ComPtr<IFolderView> folderView(rawFolderView);
  IPersistFolder2 *rawFolder = nullptr;
  if (FAILED(folderView->GetFolder(IID_IPersistFolder2, reinterpret_cast<void **>(&rawFolder)))) {
    return {};
  }
  ComPtr<IPersistFolder2> folder(rawFolder);
  PIDLIST_ABSOLUTE pidl = nullptr;
  if (FAILED(folder->GetCurFolder(&pidl))) {
    return {};
  }
  wchar_t path[32768] = {};
  const bool resolved = SHGetPathFromIDListEx(pidl, path, 32768, GPFIDL_DEFAULT);
  CoTaskMemFree(pidl);
  return resolved ? shellItemAtPoint(directoryTarget(QString::fromWCharArray(path)), root, point) : NativeDropTarget{};
}

} // namespace

NativeDropTarget captureNativeDropTarget()
{
  POINT point = {};
  if (!GetCursorPos(&point)) {
    return {};
  }
  const HWND target = WindowFromPoint(point);
  const HWND root = GetAncestor(target, GA_ROOT);
  if (!root) {
    return {};
  }
  const ComApartment apartment;
  if (!apartment.available()) {
    return {};
  }
  DWORD process = 0;
  DWORD shellProcess = 0;
  GetWindowThreadProcessId(root, &process);
  GetWindowThreadProcessId(GetShellWindow(), &shellProcess);
  wchar_t className[256] = {};
  GetClassNameW(root, className, 256);
  const auto name = QString::fromWCharArray(className);
  if (process == shellProcess && (name == QStringLiteral("Progman") || name == QStringLiteral("WorkerW"))) {
    return shellItemAtPoint(
        directoryTarget(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)), root, point
    );
  }
  // Do not interpret an arbitrary application's controls as file destinations.
  if (name != QStringLiteral("CabinetWClass") && name != QStringLiteral("ExploreWClass")) {
    return {};
  }
  IShellWindows *rawWindows = nullptr;
  if (FAILED(CoCreateInstance(
          CLSID_ShellWindows, nullptr, CLSCTX_LOCAL_SERVER, IID_IShellWindows, reinterpret_cast<void **>(&rawWindows)
      ))) {
    return {};
  }
  ComPtr<IShellWindows> windows(rawWindows);
  long count = 0;
  if (FAILED(windows->get_Count(&count))) {
    return {};
  }
  for (long index = 0; index < count; ++index) {
    VARIANT item;
    VariantInit(&item);
    item.vt = VT_I4;
    item.lVal = index;
    IDispatch *rawDispatch = nullptr;
    if (SUCCEEDED(windows->Item(item, &rawDispatch)) && rawDispatch) {
      ComPtr<IDispatch> dispatch(rawDispatch);
      const auto destination = explorerDirectory(dispatch.get(), root, point);
      if (!destination.directory.isEmpty()) {
        return destination;
      }
    }
  }
  return {};
}

} // namespace deskflow::gui::transfer
