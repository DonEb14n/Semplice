#pragma once

#include <QIODevice>
#include <QMutex>
#include <QString>
#include <QWaitCondition>

#include <cstdint>

namespace semplice::platform {

// A music file opened for reading in a way that lets Windows rename or delete it while it is open. The player reads from
// this device instead of opening the file itself. A folder rename still needs every handle inside it closed, so the
// owner can suspend() the device for a moment: the handle is closed and reads wait; resume() opens the file again
// (possibly at a new path) and the music goes on without the player ever noticing.
class SharedReadFile final : public QIODevice {
    Q_OBJECT
public:
    explicit SharedReadFile(const QString& path, QObject* parent = nullptr);
    ~SharedReadFile() override;

    bool open(OpenMode mode = ReadOnly) override;
    void close() override;
    bool isSequential() const override { return false; }
    qint64 size() const override { return m_size; }
    qint64 bytesAvailable() const override;
    bool seek(qint64 position) override;

    // Closes the handle; readers wait until resume() or cancel().
    void suspend();
    // Opens `path` (the same file, maybe moved) and lets the readers go on. False when it cannot be opened.
    bool resume(const QString& path);
    // Wakes every waiting reader with an error (the player is about to be told to stop).
    void cancel();
    bool isSuspended() const;
    // Is `path` the very file this device has (or had) open?
    bool sameFileAs(const QString& path) const;

protected:
    qint64 readData(char* data, qint64 maxSize) override;
    qint64 writeData(const char*, qint64) override { return -1; }

private:
    bool openHandle(const QString& path);
    static bool identityOf(void* handle, quint64* volume, quint64* index);

    mutable QMutex m_mutex;
    QWaitCondition m_wake;
    QString m_path;
    void* m_handle = nullptr;
    bool m_suspended = false;
    bool m_cancelled = false;
    qint64 m_size = 0;
    qint64 m_offset = 0;  // where the next read starts in the file (the device may buffer, so pos() is not it)
    quint64 m_volume = 0;
    quint64 m_index = 0;
};

}  // namespace semplice::platform
