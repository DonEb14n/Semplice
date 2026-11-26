#include "platform/StartMenuShortcut.h"

#include <QDir>
#include <QFileInfo>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <propkey.h>
#include <propvarutil.h>
#endif

namespace semplice::platform {

#ifdef Q_OS_WIN
namespace {

// System.AppUserModel.ID {9F4C2855-9F79-4B39-A8D0-E1D42DE1D5F3}, 5 (declared here so no extra library is needed)
const PROPERTYKEY kAppUserModelId = {{0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 5};

QString programsFolder()
{
    PWSTR raw = nullptr;
    QString out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Programs, 0, nullptr, &raw)) && raw)
        out = QString::fromWCharArray(raw);
    CoTaskMemFree(raw);
    return out;
}

// Does the shortcut at `linkPath` already point at `exe` and carry `id`? (read with a link object of its own: a link
// loaded for reading cannot be changed afterwards)
bool upToDate(const QString& linkPath, const QString& exe, const QString& id)
{
    if (!QFileInfo::exists(linkPath))
        return false;
    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&link))) || !link)
        return false;
    bool same = false;
    IPersistFile* file = nullptr;
    if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file))) && file) {
        if (SUCCEEDED(file->Load(reinterpret_cast<LPCOLESTR>(linkPath.utf16()), STGM_READ))) {
            wchar_t target[MAX_PATH] = {};
            if (SUCCEEDED(link->GetPath(target, MAX_PATH, nullptr, 0)) && QString::fromWCharArray(target).compare(exe, Qt::CaseInsensitive) == 0) {
                IPropertyStore* store = nullptr;
                if (SUCCEEDED(link->QueryInterface(IID_IPropertyStore, reinterpret_cast<void**>(&store))) && store) {
                    PROPVARIANT value;
                    PropVariantInit(&value);
                    same = SUCCEEDED(store->GetValue(kAppUserModelId, &value)) && value.vt == VT_LPWSTR && value.pwszVal
                           && QString::fromWCharArray(value.pwszVal) == id;
                    PropVariantClear(&value);
                    store->Release();
                }
            }
        }
        file->Release();
    }
    link->Release();
    return same;
}

}  // namespace
#endif

bool ensureStartMenuShortcut(const QString& exePath, const QString& appUserModelId, const QString& displayName)
{
#ifdef Q_OS_WIN
    const QString folder = programsFolder();
    if (folder.isEmpty() || !QFileInfo::exists(exePath))
        return false;
    const QString linkPath = QDir::toNativeSeparators(QDir(folder).filePath(displayName + QStringLiteral(".lnk")));
    const QString exeNative = QDir::toNativeSeparators(exePath);
    if (upToDate(linkPath, exeNative, appUserModelId))
        return true;

    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&link))) || !link)
        return false;
    bool ok = false;
    link->SetPath(reinterpret_cast<LPCWSTR>(exeNative.utf16()));
    link->SetWorkingDirectory(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(QFileInfo(exePath).absolutePath()).utf16()));
    link->SetIconLocation(reinterpret_cast<LPCWSTR>(exeNative.utf16()), 0);
    link->SetDescription(reinterpret_cast<LPCWSTR>(displayName.utf16()));
    IPropertyStore* store = nullptr;
    IPersistFile* file = nullptr;
    if (SUCCEEDED(link->QueryInterface(IID_IPropertyStore, reinterpret_cast<void**>(&store))) && store
        && SUCCEEDED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file))) && file) {
        PROPVARIANT value;
        PropVariantInit(&value);
        value.vt = VT_LPWSTR;
        value.pwszVal = static_cast<LPWSTR>(CoTaskMemAlloc((appUserModelId.size() + 1) * sizeof(wchar_t)));
        if (value.pwszVal) {
            appUserModelId.toWCharArray(value.pwszVal);
            value.pwszVal[appUserModelId.size()] = L'\0';
            if (SUCCEEDED(store->SetValue(kAppUserModelId, value)) && SUCCEEDED(store->Commit()))
                ok = SUCCEEDED(file->Save(reinterpret_cast<LPCOLESTR>(linkPath.utf16()), TRUE));
        }
        PropVariantClear(&value);
    }
    if (file)
        file->Release();
    if (store)
        store->Release();
    link->Release();
    return ok;
#else
    Q_UNUSED(exePath)
    Q_UNUSED(appUserModelId)
    Q_UNUSED(displayName)
    return false;
#endif
}

}  // namespace semplice::platform
