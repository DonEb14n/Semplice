#pragma once

#include "core/util/Job.h"
#include "core/util/Result.h"

#include <QJsonObject>
#include <QList>
#include <QString>

namespace semplice {

// The container of a shared playlist / album / discography (*.smpl, format version 1, see docs/design-notes/M12-share-smpl.md):
//   "SMPLFILE" | u32 version | u32 manifest size | SHA-256 of the manifest | manifest (JSON) | blobs back to back
// Every blob (an audio file, a picture) has its own SHA-256 in the manifest, so a damaged or cut-off file is found before
// anything is imported.
struct SmplBlob {
    QString id;
    QString name;       // the file name it had
    qint64 size = 0;
    QByteArray sha256;  // 32 bytes
    qint64 offset = 0;  // from the start of the first blob (reader only)
};

class SmplWriter {
public:
    static constexpr quint32 kVersion = 1;

    // Registers a file to be stored; reads it once for its size and hash. `id` is what the manifest refers to.
    Result<void> addFile(const QString& id, const QString& name, const QString& sourcePath);
    bool hasBlob(const QString& id) const;

    // Writes `path` (through a temporary file that is renamed at the end): header, manifest = `body` + the list of blobs,
    // the blobs. A cancel or a failure leaves no file.
    Result<void> write(const QString& path, const QJsonObject& body, const JobContext& job = {});

private:
    struct Source {
        SmplBlob blob;
        QString path;
    };
    QList<Source> m_sources;
};

class SmplReader {
public:
    // Reads and checks the header and the manifest, and that every blob lies inside the file.
    Result<void> open(const QString& path);
    const QJsonObject& manifest() const { return m_manifest; }
    bool hasBlob(const QString& id) const;
    QString blobHash(const QString& id) const;   // hex SHA-256 of a blob, empty if there is none
    // Copies one blob to `destination` and checks its hash; the destination is removed on a mismatch.
    Result<void> extract(const QString& id, const QString& destination, const JobContext& job = {}) const;
    // Checks every blob (done before an import changes anything).
    Result<void> verifyAll(const JobContext& job = {}) const;

private:
    const SmplBlob* find(const QString& id) const;
    QString m_path;
    QJsonObject m_manifest;
    QList<SmplBlob> m_blobs;
    qint64 m_dataStart = 0;
};

}  // namespace semplice
