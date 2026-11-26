#include "core/share/SmplFile.h"

#include "core/storage/OwnFiles.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>

namespace semplice {

namespace {

const QByteArray kMagic = QByteArrayLiteral("SMPLFILE");
constexpr qint64 kMaxManifest = 64 * 1024 * 1024;
constexpr qint64 kChunk = 1 << 20;

Error bad(const QString& what)
{
    return Error::make(ErrorCode::InvalidArgument, QStringLiteral("smpl: ") + what);
}

}  // namespace

// ------------------------------------------------------------------------------------------ writer

Result<void> SmplWriter::addFile(const QString& id, const QString& name, const QString& sourcePath)
{
    if (hasBlob(id))
        return {};
    QFile f(sourcePath);
    if (!f.open(QIODevice::ReadOnly))
        return Error::make(ErrorCode::IoError, QStringLiteral("cannot read ") + sourcePath);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!f.atEnd()) {
        const QByteArray chunk = f.read(kChunk);
        if (chunk.isEmpty() && !f.atEnd())
            return Error::make(ErrorCode::IoError, QStringLiteral("read error in ") + sourcePath);
        hash.addData(chunk);
    }
    Source s;
    s.blob.id = id;
    s.blob.name = name;
    s.blob.size = f.size();
    s.blob.sha256 = hash.result();
    s.path = sourcePath;
    m_sources.append(s);
    return {};
}

bool SmplWriter::hasBlob(const QString& id) const
{
    for (const Source& s : m_sources)
        if (s.blob.id == id)
            return true;
    return false;
}

Result<void> SmplWriter::write(const QString& path, const QJsonObject& body, const JobContext& job)
{
    QJsonObject manifest = body;
    QJsonArray blobs;
    for (const Source& s : std::as_const(m_sources))
        blobs.append(QJsonObject{{QStringLiteral("id"), s.blob.id},
                                 {QStringLiteral("name"), s.blob.name},
                                 {QStringLiteral("size"), static_cast<double>(s.blob.size)},
                                 {QStringLiteral("sha256"), QString::fromLatin1(s.blob.sha256.toHex())}});
    manifest.insert(QStringLiteral("blobs"), blobs);
    manifest.insert(QStringLiteral("version"), static_cast<int>(kVersion));
    const QByteArray json = QJsonDocument(manifest).toJson(QJsonDocument::Compact);

    QSaveFile out(path);  // written beside the target, renamed when committed: a cancelled export leaves nothing
    if (!out.open(QIODevice::WriteOnly))
        return Error::make(ErrorCode::IoError, QStringLiteral("cannot write ") + path);
    out.write(kMagic);
    QDataStream header(&out);
    header.setByteOrder(QDataStream::LittleEndian);
    header << static_cast<quint32>(kVersion) << static_cast<quint32>(json.size());
    out.write(QCryptographicHash::hash(json, QCryptographicHash::Sha256));
    out.write(json);

    qint64 done = 0;
    qint64 total = 0;
    for (const Source& s : std::as_const(m_sources))
        total += s.blob.size;
    for (const Source& s : std::as_const(m_sources)) {
        QFile in(s.path);
        if (!in.open(QIODevice::ReadOnly)) {
            out.cancelWriting();
            return Error::make(ErrorCode::IoError, QStringLiteral("cannot read ") + s.path);
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        qint64 written = 0;
        while (!in.atEnd()) {
            if (job.cancelled()) {
                out.cancelWriting();
                return Error::make(ErrorCode::Cancelled);
            }
            const QByteArray chunk = in.read(kChunk);
            hash.addData(chunk);
            if (out.write(chunk) != chunk.size()) {
                out.cancelWriting();
                return Error::make(ErrorCode::IoError, QStringLiteral("write error in ") + path);
            }
            written += chunk.size();
            done += chunk.size();
            job.report(QStringLiteral("export"), done, total);
        }
        if (written != s.blob.size || hash.result() != s.blob.sha256) {  // the file changed while it was being exported
            out.cancelWriting();
            return Error::make(ErrorCode::IoError, QStringLiteral("file changed during export: ") + s.path);
        }
    }
    if (!out.commit())
        return Error::make(ErrorCode::IoError, QStringLiteral("cannot finish ") + path);
    return {};
}

// ------------------------------------------------------------------------------------------ reader

Result<void> SmplReader::open(const QString& path)
{
    m_path = path;
    m_blobs.clear();
    m_manifest = {};
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return Error::make(ErrorCode::IoError, QStringLiteral("cannot read ") + path);
    if (f.read(kMagic.size()) != kMagic)
        return bad(QStringLiteral("not a Semplice share file"));
    QDataStream in(&f);
    in.setByteOrder(QDataStream::LittleEndian);
    quint32 version = 0, manifestSize = 0;
    in >> version >> manifestSize;
    if (version == 0 || version > SmplWriter::kVersion)
        return Error::make(ErrorCode::SchemaTooNew, QStringLiteral("smpl: format version ") + QString::number(version));
    if (manifestSize == 0 || manifestSize > kMaxManifest)
        return bad(QStringLiteral("bad manifest size"));
    const QByteArray expected = f.read(32);
    const QByteArray json = f.read(manifestSize);
    if (expected.size() != 32 || json.size() != static_cast<qint64>(manifestSize))
        return bad(QStringLiteral("the file is cut off"));
    if (QCryptographicHash::hash(json, QCryptographicHash::Sha256) != expected)
        return bad(QStringLiteral("the manifest is damaged"));
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
        return bad(QStringLiteral("the manifest is not readable"));
    m_manifest = doc.object();
    m_dataStart = f.pos();
    qint64 offset = 0;
    for (const QJsonValue& v : m_manifest.value(QStringLiteral("blobs")).toArray()) {
        const QJsonObject o = v.toObject();
        SmplBlob b;
        b.id = o.value(QStringLiteral("id")).toString();
        b.name = o.value(QStringLiteral("name")).toString();
        b.size = static_cast<qint64>(o.value(QStringLiteral("size")).toDouble(-1));
        b.sha256 = QByteArray::fromHex(o.value(QStringLiteral("sha256")).toString().toLatin1());
        if (b.id.isEmpty() || b.size < 0 || b.sha256.size() != 32 || find(b.id))
            return bad(QStringLiteral("a file entry is wrong"));
        b.offset = offset;
        offset += b.size;
        m_blobs.append(b);
    }
    if (m_dataStart + offset > f.size())
        return bad(QStringLiteral("the file is cut off"));
    return {};
}

const SmplBlob* SmplReader::find(const QString& id) const
{
    for (const SmplBlob& b : m_blobs)
        if (b.id == id)
            return &b;
    return nullptr;
}

bool SmplReader::hasBlob(const QString& id) const
{
    return find(id) != nullptr;
}

QString SmplReader::blobHash(const QString& id) const
{
    const SmplBlob* blob = find(id);
    return blob ? QString::fromLatin1(blob->sha256.toHex()) : QString();
}

Result<void> SmplReader::extract(const QString& id, const QString& destination, const JobContext& job) const
{
    const SmplBlob* blob = find(id);
    if (!blob)
        return bad(QStringLiteral("no such file in the package: ") + id);
    QFile in(m_path);
    if (!in.open(QIODevice::ReadOnly) || !in.seek(m_dataStart + blob->offset))
        return Error::make(ErrorCode::IoError, QStringLiteral("cannot read ") + m_path);
    QFile out(destination);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return Error::make(ErrorCode::IoError, QStringLiteral("cannot write ") + destination);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    qint64 left = blob->size;
    while (left > 0) {
        if (job.cancelled()) {
            out.close();
            (void)ownfiles::removeFile(destination);
            return Error::make(ErrorCode::Cancelled);
        }
        const QByteArray chunk = in.read(std::min(left, kChunk));
        if (chunk.isEmpty()) {
            out.close();
            (void)ownfiles::removeFile(destination);
            return bad(QStringLiteral("the file is cut off"));
        }
        hash.addData(chunk);
        out.write(chunk);
        left -= chunk.size();
    }
    out.close();
    if (hash.result() != blob->sha256) {
        (void)ownfiles::removeFile(destination);
        return bad(QStringLiteral("a file is damaged: ") + blob->name);
    }
    return {};
}

Result<void> SmplReader::verifyAll(const JobContext& job) const
{
    QFile in(m_path);
    if (!in.open(QIODevice::ReadOnly))
        return Error::make(ErrorCode::IoError, QStringLiteral("cannot read ") + m_path);
    qint64 done = 0;
    qint64 total = 0;
    for (const SmplBlob& b : m_blobs)
        total += b.size;
    for (const SmplBlob& b : m_blobs) {
        if (!in.seek(m_dataStart + b.offset))
            return bad(QStringLiteral("the file is cut off"));
        QCryptographicHash hash(QCryptographicHash::Sha256);
        qint64 left = b.size;
        while (left > 0) {
            if (job.cancelled())
                return Error::make(ErrorCode::Cancelled);
            const QByteArray chunk = in.read(std::min(left, kChunk));
            if (chunk.isEmpty())
                return bad(QStringLiteral("the file is cut off"));
            hash.addData(chunk);
            left -= chunk.size();
            done += chunk.size();
            job.report(QStringLiteral("verify"), done, total);
        }
        if (hash.result() != b.sha256)
            return bad(QStringLiteral("a file is damaged: ") + b.name);
    }
    return {};
}

}  // namespace semplice
