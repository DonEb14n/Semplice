#include "platform/SharedReadFile.h"

#include <QDir>
#include <QMutexLocker>

#include <algorithm>

#include <windows.h>

namespace semplice::platform {

namespace {
HANDLE openShared(const QString& path)
{
    return CreateFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()), GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
}
constexpr unsigned long kWaitMs = 30000;  // a suspended device that nobody resumes ends in an error, not in a hang
}  // namespace

SharedReadFile::SharedReadFile(const QString& path, QObject* parent) : QIODevice(parent), m_path(path) {}

SharedReadFile::~SharedReadFile()
{
    SharedReadFile::close();
}

bool SharedReadFile::identityOf(void* handle, quint64* volume, quint64* index)
{
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(static_cast<HANDLE>(handle), &info))
        return false;
    *volume = info.dwVolumeSerialNumber;
    *index = (static_cast<quint64>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
    return true;
}

bool SharedReadFile::openHandle(const QString& path)
{
    HANDLE h = openShared(path);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER s;
    if (!GetFileSizeEx(h, &s)) {
        CloseHandle(h);
        return false;
    }
    m_handle = h;
    m_size = s.QuadPart;
    m_path = path;
    identityOf(h, &m_volume, &m_index);
    return true;
}

bool SharedReadFile::open(OpenMode)
{
    QMutexLocker lock(&m_mutex);
    if (!openHandle(m_path))
        return false;
    m_suspended = false;
    m_cancelled = false;
    m_offset = 0;
    return QIODevice::open(ReadOnly);
}

void SharedReadFile::close()
{
    {
        QMutexLocker lock(&m_mutex);
        if (m_handle) {
            CloseHandle(static_cast<HANDLE>(m_handle));
            m_handle = nullptr;
        }
        m_cancelled = true;
        m_wake.wakeAll();
    }
    QIODevice::close();
}

qint64 SharedReadFile::bytesAvailable() const
{
    QMutexLocker lock(&m_mutex);
    return QIODevice::bytesAvailable() + std::max<qint64>(0, m_size - m_offset);
}

bool SharedReadFile::seek(qint64 position)
{
    if (position < 0 || position > m_size)
        return false;
    {
        QMutexLocker lock(&m_mutex);
        m_offset = position;
    }
    return QIODevice::seek(position);
}

void SharedReadFile::suspend()
{
    QMutexLocker lock(&m_mutex);
    if (m_handle) {
        CloseHandle(static_cast<HANDLE>(m_handle));
        m_handle = nullptr;
    }
    m_suspended = true;
}

bool SharedReadFile::resume(const QString& path)
{
    QMutexLocker lock(&m_mutex);
    if (!m_suspended)
        return true;
    if (!openHandle(path))
        return false;
    m_suspended = false;
    m_wake.wakeAll();
    return true;
}

void SharedReadFile::cancel()
{
    QMutexLocker lock(&m_mutex);
    m_cancelled = true;
    m_wake.wakeAll();
}

bool SharedReadFile::isSuspended() const
{
    QMutexLocker lock(&m_mutex);
    return m_suspended;
}

bool SharedReadFile::sameFileAs(const QString& path) const
{
    HANDLE h = openShared(path);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    quint64 volume = 0, index = 0;
    const bool known = identityOf(h, &volume, &index);
    CloseHandle(h);
    QMutexLocker lock(&m_mutex);
    return known && volume == m_volume && index == m_index;
}

qint64 SharedReadFile::readData(char* data, qint64 maxSize)
{
    QMutexLocker lock(&m_mutex);
    while (m_suspended && !m_cancelled)
        if (!m_wake.wait(&m_mutex, kWaitMs))
            return -1;
    if (m_cancelled || !m_handle)
        return -1;
    LARGE_INTEGER at;
    at.QuadPart = m_offset;
    if (!SetFilePointerEx(static_cast<HANDLE>(m_handle), at, nullptr, FILE_BEGIN))
        return -1;
    DWORD got = 0;
    if (!ReadFile(static_cast<HANDLE>(m_handle), data, static_cast<DWORD>(std::min<qint64>(maxSize, 1 << 20)), &got, nullptr))
        return -1;
    m_offset += got;
    return got;
}

}  // namespace semplice::platform
