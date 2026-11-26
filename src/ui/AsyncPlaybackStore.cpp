#include "ui/AsyncPlaybackStore.h"

#include "core/db/Database.h"
#include "core/util/Logger.h"

#include <QMetaObject>

namespace semplice {

// Lives in the writer thread: owns the connection used only there.
struct AsyncPlaybackStore::Writer : QObject {
    explicit Writer(QString path) : dbPath(std::move(path)) {}
    void open()
    {
        auto opened = Database::open(dbPath);
        if (!opened.ok()) {
            SLOG_ERROR(QStringLiteral("playback writer: ") + opened.error().devMessage);
            return;
        }
        db = std::move(opened).value();
        store = std::make_unique<LibraryPlaybackStore>(*db);
    }
    void close()
    {
        store.reset();
        db.reset();
    }
    QString dbPath;
    std::unique_ptr<Database> db;
    std::unique_ptr<LibraryPlaybackStore> store;
};

AsyncPlaybackStore::AsyncPlaybackStore(const QString& dbPath, Database& readerConnection, QObject* parent)
    : QObject(parent), m_reader(readerConnection)
{
    m_writer = new Writer(dbPath);
    m_writer->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_writer, &QObject::deleteLater);
    m_thread.setObjectName(QStringLiteral("semplice-playback-writer"));
    m_thread.start();
    QMetaObject::invokeMethod(m_writer, [w = m_writer] { w->open(); }, Qt::QueuedConnection);
}

AsyncPlaybackStore::~AsyncPlaybackStore()
{
    QMetaObject::invokeMethod(m_writer, [w = m_writer] { w->close(); }, Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
}

void AsyncPlaybackStore::recordPlay(TrackId id, qint64 nowMs)
{
    QMetaObject::invokeMethod(m_writer, [w = m_writer, id, nowMs] {
        if (w->store)
            w->store->recordPlay(id, nowMs);
    }, Qt::QueuedConnection);
}

void AsyncPlaybackStore::touchRecent(ContainerKind kind, qint64 id, qint64 nowMs)
{
    QMetaObject::invokeMethod(m_writer, [this, w = m_writer, kind, id, nowMs] {
        if (w->store)
            w->store->touchRecent(kind, id, nowMs);
        QMetaObject::invokeMethod(this, &AsyncPlaybackStore::recentWritten, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

QJsonObject AsyncPlaybackStore::loadSession()
{
    return m_reader.loadSession();
}

void AsyncPlaybackStore::saveSession(const QJsonObject& session)
{
    QMetaObject::invokeMethod(m_writer, [w = m_writer, session] {
        if (w->store)
            w->store->saveSession(session);
    }, Qt::QueuedConnection);
}

void AsyncPlaybackStore::flush()
{
    QMetaObject::invokeMethod(m_writer, [] {}, Qt::BlockingQueuedConnection);
}

}  // namespace semplice
