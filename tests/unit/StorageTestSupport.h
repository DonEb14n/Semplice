#pragma once

#include "FakeTrash.h"

#include "core/storage/FileStorageService.h"
#include "core/util/Clock.h"

#include <QDirIterator>
#include <QTemporaryDir>
#include <QtTest>

namespace semplice::testing {

class FixedClock final : public IClock {
public:
    QDateTime now() const override { return QDateTime(QDate(2026, 10, 19), QTime(12, 0), QTimeZone::UTC); }
};

// Files and folders below a directory, with content, for comparing "before" and "after".
struct TreeSnapshot {
    QMap<QString, QByteArray> files;  // relative path -> content
    QSet<QString> dirs;               // relative paths of folders (including empty ones)

    bool operator==(const TreeSnapshot&) const = default;

    QString describe() const
    {
        QStringList lines;
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            lines << QStringLiteral("  F %1 = %2").arg(it.key(), QString::fromUtf8(it.value()));
        QStringList d = QStringList(dirs.begin(), dirs.end());
        d.sort();
        for (const QString& s : std::as_const(d))
            lines << QStringLiteral("  D ") + s;
        return lines.join(QLatin1Char('\n'));
    }
};

inline TreeSnapshot snapshotOf(const QString& rootDir, const QStringList& skipTop = {QStringLiteral(".library")})
{
    TreeSnapshot s;
    const QDir root(rootDir);
    QDirIterator it(rootDir, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString rel = root.relativeFilePath(path);
        if (skipTop.contains(rel.section(QLatin1Char('/'), 0, 0)))
            continue;
        if (it.fileInfo().isDir()) {
            s.dirs.insert(rel);
        } else {
            QFile f(path);
            if (f.open(QIODevice::ReadOnly))
                s.files.insert(rel, f.readAll());
        }
    }
    return s;
}

// What the (fake) Recycle Bin holds, without the numbering of its slots.
inline TreeSnapshot trashSnapshot(const QString& sandbox)
{
    TreeSnapshot raw = snapshotOf(sandbox, {});
    TreeSnapshot out;
    for (auto it = raw.files.cbegin(); it != raw.files.cend(); ++it)
        out.files.insert(it.key().section(QLatin1Char('/'), 1), it.value());
    for (const QString& d : std::as_const(raw.dirs)) {
        const QString stripped = d.section(QLatin1Char('/'), 1);
        if (!stripped.isEmpty())
            out.dirs.insert(stripped);
    }
    return out;
}

// A temporary library root, a separate folder with "original" files, a fake Recycle Bin, and a
// FileStorageService on top.
struct StorageEnv {
    StorageEnv()
        : root(dir.filePath(QStringLiteral("library"))),
          src(dir.filePath(QStringLiteral("source"))),
          trashDir(dir.filePath(QStringLiteral("trash"))),
          trash(trashDir)
    {
        QDir().mkpath(root + QStringLiteral("/.library/journal"));
        QDir().mkpath(src);
        QDir().mkpath(trashDir);
        storage = std::make_unique<FileStorageService>(root, journalDir(), trash, clock);
    }

    QString journalDir() const { return root + QStringLiteral("/.library/journal"); }
    QString lib(const QString& rel) const { return root + QLatin1Char('/') + rel; }
    QString source(const QString& name) const { return src + QLatin1Char('/') + name; }

    void write(const QString& absolute, const QByteArray& content) const
    {
        QDir().mkpath(QFileInfo(absolute).absolutePath());
        QFile f(absolute);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(content);
    }
    void writeLib(const QString& rel, const QByteArray& content) const { write(lib(rel), content); }
    void mkdirLib(const QString& rel) const { QDir().mkpath(lib(rel)); }
    QByteArray read(const QString& absolute) const
    {
        QFile f(absolute);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }
    int journalEntries() const { return QDir(journalDir()).entryList({QStringLiteral("*.json")}, QDir::Files).size(); }

    TreeSnapshot libraryTree() const { return snapshotOf(root); }
    TreeSnapshot sourceTree() const { return snapshotOf(src); }
    TreeSnapshot trashTree() const { return trashSnapshot(trashDir); }

    QTemporaryDir dir;
    QString root;
    QString src;
    QString trashDir;
    FixedClock clock;
    FakeTrash trash;
    std::unique_ptr<FileStorageService> storage;
};

}  // namespace semplice::testing
