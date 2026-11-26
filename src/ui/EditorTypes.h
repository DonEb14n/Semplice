#pragma once

#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

namespace semplice {

// One scanned audio file as the editors list it (the forms of UI doc p.36-41 and p.45).
struct ScanRow {
    int index = -1;                 // index in the engine's scan (stays valid until the editor is closed)
    QString path;
    QString fileName;
    QString title;
    QStringList creditNames;        // proposed credits, main artists first
    QList<int> creditRoles;         // CreditRole per name (0 main, 1 feat)
    qint64 durationMs = 0;
    bool duplicate = false;         // this exact file is already in the library
    QString duplicateTitle;
};

// A track as the track editor needs it (UI doc p.39).
struct TrackEdit {
    qint64 id = 0;
    QString title;
    QStringList artistNames;
    QList<qint64> artistIds;
    QList<int> roles;         // CreditRole per artist
    QList<bool> hidden;
    QStringList avatars;      // library-relative picture of each artist (may be empty)
    QStringList tags;         // vocabulary keys of the track itself
};

// The answer to "scan these files": the rows and what the importer would propose for them (UI doc p.29).
// An author found in the tags of the files: an artist the library has already (id, picture) or only a name.
struct ScanAuthor {
    qint64 id = 0;
    QString name;
    QString avatar;        // library-relative
    bool hidden = false;
};

struct ScanSummary {
    int token = 0;
    QList<ScanRow> rows;
    int skipped = 0;                // unreadable files
    bool hasProposal = false;
    int kind = 1;                   // 0 album, 1 playlist
    QString albumTitle;
    QStringList authors;
    QList<ScanAuthor> authorRefs;   // the same authors, with what the library knows about them
    int albumType = 1;
    int year = 0, month = 0, day = 0;
    QStringList tags;               // vocabulary keys
    QString coverPath;              // a picture file found next to the music
    bool embeddedCover = false;     // a picture inside the first track
    QString playlistName;
};


// What a delete would do, counted (from LibraryService::planDelete*): the confirmation text is built from it.
struct DeleteSummary {
    int op = 0;                     // OperationKind of the delete
    qint64 id = 0;
    bool ok = false;
    int tracksDeleted = 0;          // go to the Recycle Bin
    int tracksKept = 0;             // stay in the collection (they exist elsewhere)
    int tracksMoved = 0;
    int albumsDeleted = 0;
    int playlistsDeleted = 0;
    int albumsRehomed = 0;
    int artistsDeleted = 0;
    int artistsHidden = 0;
};
}  // namespace semplice

Q_DECLARE_METATYPE(semplice::ScanRow)
Q_DECLARE_METATYPE(semplice::ScanSummary)
Q_DECLARE_METATYPE(semplice::TrackEdit)
Q_DECLARE_METATYPE(semplice::DeleteSummary)
