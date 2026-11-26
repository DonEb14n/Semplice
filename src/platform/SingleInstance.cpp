#include "platform/SingleInstance.h"

#include <QLocalSocket>
#include <QThread>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace semplice::platform {

SingleInstance::SingleInstance(const QString& key, QObject* parent) : QObject(parent), m_key(key)
{
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket* socket = m_server.nextPendingConnection()) {
            socket->deleteLater();
            socket->disconnectFromServer();
        }
        emit activationRequested();
    });
}

SingleInstance::~SingleInstance()
{
#ifdef Q_OS_WIN
    if (m_mutex)
        CloseHandle(static_cast<HANDLE>(m_mutex));
#endif
}

bool SingleInstance::notifyRunningInstance()
{
    QLocalSocket socket;
    socket.connectToServer(m_key);
    if (!socket.waitForConnected(300))
        return false;
    socket.write("activate");
    socket.waitForBytesWritten(300);
    socket.disconnectFromServer();
    return true;
}

bool SingleInstance::acquire()
{
    bool otherExists = false;
#ifdef Q_OS_WIN
    const QString mutexName = QStringLiteral("Local\\") + m_key;
    m_mutex = CreateMutexW(nullptr, FALSE, reinterpret_cast<LPCWSTR>(mutexName.utf16()));
    otherExists = m_mutex && GetLastError() == ERROR_ALREADY_EXISTS;
#endif

    // If the mutex says someone else is there, give it up to ~3 s to start listening.
    const int attempts = otherExists ? 30 : 1;
    for (int i = 0; i < attempts; ++i) {
        if (notifyRunningInstance())
            return false;
        if (otherExists)
            QThread::msleep(100);
    }
    if (otherExists)
        return false;  // another process owns the mutex but does not answer: never run two

    QLocalServer::removeServer(m_key);  // stale socket from a crashed run (no-op on Windows)
    return m_server.listen(m_key);
}

}  // namespace semplice::platform
