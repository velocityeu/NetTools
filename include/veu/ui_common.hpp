#pragma once
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <shobjidl.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <windows.h>
namespace veu::ui {
inline std::wstring wide(std::string_view s) {
  if (s.empty())
    return {};
  if (s.size() > size_t(INT_MAX))
    throw std::runtime_error("UTF-8 text is too long.");
  int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                              int(s.size()), nullptr, 0);
  if (!n)
    throw std::runtime_error("Invalid UTF-8.");
  std::wstring out(n, 0);
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                          int(s.size()), out.data(), n) != n)
    throw std::runtime_error("UTF-8 conversion failed.");
  return out;
}
inline std::string utf8(std::wstring_view s) {
  if (s.empty())
    return {};
  if (s.size() > size_t(INT_MAX))
    throw std::runtime_error("Unicode text is too long.");
  int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(),
                              int(s.size()), nullptr, 0, nullptr, nullptr);
  if (!n)
    throw std::runtime_error("Invalid Unicode.");
  std::string out(n, 0);
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(),
                          int(s.size()), out.data(), n, nullptr, nullptr) != n)
    throw std::runtime_error("Unicode conversion failed.");
  return out;
}
inline std::wstring text(HWND h) {
  int n = GetWindowTextLengthW(h);
  std::wstring out(size_t(n) + 1, 0);
  int copied = GetWindowTextW(h, out.data(), n + 1);
  out.resize(size_t(copied));
  return out;
}
inline void error(HWND h, const std::exception &e) {
  std::wstring message;
  try {
    message = wide(e.what());
  } catch (...) {
    message = L"The operation failed; its error text could not be decoded.";
  }
  MessageBoxW(h, message.c_str(), L"Velocity NetTools", MB_OK | MB_ICONWARNING);
}
inline void copy(HWND h, std::wstring_view s) {
  if (s.size() > (std::numeric_limits<size_t>::max() / sizeof(wchar_t)) - 1)
    throw std::runtime_error("Clipboard text is too long.");
  HGLOBAL block = GlobalAlloc(GMEM_MOVEABLE, (s.size() + 1) * sizeof(wchar_t));
  if (!block)
    throw std::runtime_error("Clipboard allocation failed.");
  auto data = static_cast<wchar_t *>(GlobalLock(block));
  if (!data) {
    GlobalFree(block);
    throw std::runtime_error("Clipboard lock failed.");
  }
  std::memcpy(data, s.data(), s.size() * sizeof(wchar_t));
  data[s.size()] = 0;
  GlobalUnlock(block);
  if (!OpenClipboard(h)) {
    GlobalFree(block);
    throw std::runtime_error("Clipboard is busy.");
  }
  if (!EmptyClipboard()) {
    CloseClipboard();
    GlobalFree(block);
    throw std::runtime_error("Could not clear the clipboard.");
  }
  if (!SetClipboardData(CF_UNICODETEXT, block)) {
    CloseClipboard();
    GlobalFree(block);
    throw std::runtime_error("Could not write text to the clipboard.");
  }
  CloseClipboard();
}
struct ReleaseCom {
  template <class T> void operator()(T *p) const {
    if (p)
      p->Release();
  }
};
struct ReleaseTaskMemory {
  void operator()(wchar_t *p) const { CoTaskMemFree(p); }
};
inline std::filesystem::path file_dialog(HWND h, bool save, bool csv = false) {
  IFileDialog *raw = nullptr;
  if (FAILED(
          CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog,
                           nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&raw))))
    throw std::runtime_error("Windows file dialog unavailable.");
  std::unique_ptr<IFileDialog, ReleaseCom> dialog(raw);
  COMDLG_FILTERSPEC filter = {csv ? L"CSV report (*.csv)"
                                  : L"Velocity NetTools plan (*.json)",
                              csv ? L"*.csv" : L"*.json"};
  DWORD flags = 0;
  if (FAILED(dialog->SetFileTypes(1, &filter)) ||
      FAILED(dialog->SetDefaultExtension(csv ? L"csv" : L"json")) ||
      FAILED(dialog->GetOptions(&flags)) ||
      FAILED(dialog->SetOptions(
          flags | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR | FOS_DONTADDTORECENT |
          (save ? FOS_OVERWRITEPROMPT : FOS_FILEMUSTEXIST))))
    throw std::runtime_error("Could not configure the Windows file dialog.");
  HRESULT shown = dialog->Show(h);
  if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED))
    return {};
  if (FAILED(shown))
    throw std::runtime_error("The Windows file dialog failed.");
  IShellItem *item_raw = nullptr;
  if (FAILED(dialog->GetResult(&item_raw)))
    throw std::runtime_error("Could not read the selected file.");
  std::unique_ptr<IShellItem, ReleaseCom> item(item_raw);
  PWSTR name_raw = nullptr;
  if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &name_raw)))
    throw std::runtime_error("The selected item has no filesystem path.");
  std::unique_ptr<wchar_t, ReleaseTaskMemory> name(name_raw);
  return std::filesystem::path(name.get());
}
} // namespace veu::ui
