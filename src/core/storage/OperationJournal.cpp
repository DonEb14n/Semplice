#include "core/storage/OperationJournal.h"

#include "core/storage/OwnFiles.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace semplice {

namespace {

QString kindName(FileOp::Kind k)
{
    switch (k) {
    case FileOp::Kind::EnsureDir: return QStringLiteral("ensureDir");
    case FileOp::Kind::Copy: return QStringLiteral("copy");
    case FileOp::Kind::MoveFile: return QStringLiteral("moveFile");
    case FileOp::Kind::MoveDir: return QStringLiteral("moveDir");
    case FileOp::Kind::Trash: return QStringLiteral("trash");
    }
    return {};
}

std::optional<FileOp::Kind> kindFromName(const QString& s)
{
    for (FileOp::Kind k : {FileOp::Kind::EnsureDir, FileOp::Kind::Copy, FileOp::Kind::MoveFile, FileOp::Kind::MoveDir,
                           FileOp::Kind::Trash}) {
        if (kindName(k) == s)
            return k;
    }
    return std::nullopt;
}

}  // namespace

QString OperationJournal::pathFor(const QString& id) const
{
    return QDir(m_dir).filePath(id + QStringLiteral(".json"));
}

Result<void> OperationJournal::write(const JournalEntry& entry)
{
    if (!QDir().mkpath(m_dir))
        return Error::make(ErrorCode::IoError, QStringLiteral("cannot create journal folder ") + m_dir);

    QJsonArray ops;
    for (const JournalOp& op : entry.ops) {
        QJsonObject o;
        o.insert(QStringLiteral("kind"), kindName(op.kind));
        o.insert(QStringLiteral("from"), op.from);
        o.insert(QStringLiteral("to"), op.to);
        o.insert(QStringLiteral("via"), op.via);
        o.insert(QStringLiteral("createdDirs"), QJsonArray::fromStringList(op.createdDirs));
        o.insert(QStringLiteral("dstPreexisting"), op.dstPreexisting);
        ops.append(o);
    }
    QJsonObject root;
    root.insert(QStringLiteral("id"), entry.id);
    root.insert(QStringLiteral("description"), entry.description);
    root.insert(QStringLiteral("autoCommit"), entry.autoCommit);
    root.insert(QStringLiteral("createdAt"), static_cast<double>(entry.createdAtMs));
    root.insert(QStringLiteral("ops"), ops);

    QSaveFile file(pathFor(entry.id));  // temp file + atomic rename + flush
    if (!file.open(QIODevice::WriteOnly))
        return Error::make(ErrorCode::IoError, QStringLiteral("cannot write journal entry: ") + file.errorString());
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!file.commit())
        return Error::make(ErrorCode::IoError, QStringLiteral("cannot commit journal entry: ") + file.errorString());
    return {};
}

Result<void> OperationJournal::remove(const QString& id)
{
    return ownfiles::removeFile(pathFor(id));
}

Result<QList<JournalEntry>> OperationJournal::pending(QStringList* problems) const
{
    QList<JournalEntry> out;
    const QDir dir(m_dir);
    if (!dir.exists())
        return out;
    const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Time | QDir::Reversed);
    for (const QString& name : files) {
        QFile f(dir.filePath(name));
        QJsonParseError parseError;
        QJsonDocument doc;
        if (f.open(QIODevice::ReadOnly))
            doc = QJsonDocument::fromJson(f.readAll(), &parseError);
        if (!doc.isObject()) {
            if (problems)
                problems->append(QStringLiteral("unreadable journal entry ") + name);
            continue;
        }
        const QJsonObject root = doc.object();
        JournalEntry e;
        e.id = root.value(QStringLiteral("id")).toString();
        e.description = root.value(QStringLiteral("description")).toString();
        e.autoCommit = root.value(QStringLiteral("autoCommit")).toBool();
        e.createdAtMs = static_cast<qint64>(root.value(QStringLiteral("createdAt")).toDouble());
        bool valid = !e.id.isEmpty();
        for (const QJsonValue& v : root.value(QStringLiteral("ops")).toArray()) {
            const QJsonObject o = v.toObject();
            const auto kind = kindFromName(o.value(QStringLiteral("kind")).toString());
            if (!kind) {
                valid = false;
                break;
            }
            JournalOp op;
            op.kind = *kind;
            op.from = o.value(QStringLiteral("from")).toString();
            op.to = o.value(QStringLiteral("to")).toString();
            op.via = o.value(QStringLiteral("via")).toString();
            for (const QJsonValue& d : o.value(QStringLiteral("createdDirs")).toArray())
                op.createdDirs << d.toString();
            op.dstPreexisting = o.value(QStringLiteral("dstPreexisting")).toBool();
            e.ops.append(op);
        }
        if (!valid) {
            if (problems)
                problems->append(QStringLiteral("invalid journal entry ") + name);
            continue;
        }
        out.append(e);
    }
    return out;
}

}  // namespace semplice
