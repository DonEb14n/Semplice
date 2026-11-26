// semplice_libtool: a developer/QA tool to try the M4 services on REAL music before the UI exists.
//
//   semplice_libtool import <library> <file-or-folder>... [--year N] [--playlist NAME]
//   semplice_libtool rescan <library> [--force]
//   semplice_libtool check  <library>
//   semplice_libtool info   <library>
//
// <library> is an empty folder (a new library starts there) or an existing library. Works on copies:
// the source files are only read. Uses the real Recycle Bin and the real Windows file functions.
#include "IntegrityChecker.h"

#include "core/services/LibrarySession.h"
#include "core/storage/ITrash.h"
#include "core/util/Clock.h"
#include "platform/WindowsAtomicReplace.h"
#include "platform/WindowsCodepageConverter.h"
#include "platform/WindowsFileAttributes.h"

#include <QCoreApplication>
#include <QDirIterator>
#include <QTextStream>

using namespace semplice;

namespace {

QTextStream out(stdout);
QTextStream err(stderr);

int fail(const QString& what)
{
    err << what << "\n";
    return 1;
}

QString describe(const Error& e) { return e.symbol() + QStringLiteral(": ") + e.devMessage; }

struct Env {
    QtTrash trash;
    platform::WindowsAtomicReplace replace;
    platform::WindowsCodepageConverter codepages;
    SystemClock clock;
    platform::WindowsFileAttributes attributes;
    SessionDependencies deps{trash, replace, codepages, clock, attributes, nullptr, nullptr};
};

JobContext consoleJob()
{
    JobContext job;
    job.progress = [last = QString()](const JobProgress& p) mutable {
        if (p.phase != last) {
            out << "  " << p.phase << "...\n";
            out.flush();
            last = p.phase;
        }
    };
    return job;
}

int check(LibrarySession& session)
{
    int problems = 0;
    for (const QString& line : testing::checkLibraryIntegrity(session.database())) {
        out << "INTEGRITY: " << line << "\n";
        ++problems;
    }
    QSet<QString> disk;
    for (const QString& top : {QStringLiteral("Artists"), QStringLiteral("Playlists")}) {
        QDirIterator it(session.layout().root + QLatin1Char('/') + top, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
        while (it.hasNext())
            disk.insert(QDir(session.layout().root).relativeFilePath(it.next()));
    }
    TrackRepo tracks(session.database());
    auto files = tracks.allFiles();
    if (!files.ok())
        return fail(describe(files.error()));
    QSet<QString> known;
    int tagMismatches = 0;
    for (const auto& row : files.value()) {
        known.insert(row.path);
        auto t = tracks.get(row.id);
        if (!t.ok() || !t.value() || t.value()->tagsDirty)
            continue;
        auto want = session.library().buildTagWrite(row.id);
        auto got = session.tags().read(session.layout().root + QLatin1Char('/') + row.path);
        if (!want.ok() || !got.ok()) {
            out << "TAGS: cannot compare " << row.path << "\n";
            ++tagMismatches;
            continue;
        }
        if (got.value().title != want.value().title || got.value().artists != want.value().artists ||
            (want.value().album && got.value().album != *want.value().album)) {
            out << "TAGS differ: " << row.path << "\n";
            ++tagMismatches;
        }
    }
    for (const QString& f : disk - known) {
        out << "FILE NOT IN DATABASE: " << f << "\n";
        ++problems;
    }
    for (const QString& f : known - disk) {
        out << "ROW WITHOUT FILE: " << f << "\n";
        ++problems;
    }
    out << files.value().size() << " tracks, " << disk.size() << " files, " << problems << " problems, " << tagMismatches
        << " tag mismatches\n";
    return problems + tagMismatches == 0 ? 0 : 2;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments().mid(1);
    if (args.size() < 2)
        return fail(QStringLiteral("usage: semplice_libtool import|rescan|check|info <library> ..."));
    const QString command = args.at(0);
    const QString root = QDir::cleanPath(args.at(1));

    Env env;
    auto opened = LibrarySwitcher::select(root, env.deps);
    if (!opened.ok())
        return fail(QStringLiteral("cannot open the library: ") + describe(opened.error()));
    LibrarySession& session = *opened.value();
    if (session.recovery().repairedAnything())
        out << "start-up repaired an interrupted operation\n";

    if (command == QLatin1String("info")) {
        out << "library " << session.info().uid << ", schema " << session.info().schema << "\n";
        return check(session);
    }
    if (command == QLatin1String("check"))
        return check(session);

    if (command == QLatin1String("rescan")) {
        RescanOptions options;
        options.allowMassRemoval = args.contains(QStringLiteral("--force"));
        auto r = session.rescanner().run(options, consoleJob());
        if (!r.ok())
            return fail(describe(r.error()));
        const RescanReport& rep = r.value();
        out << "checked " << rep.checked << ", refreshed " << rep.refreshed << ", adopted " << rep.adopted << ", duplicates to Recycle Bin "
            << rep.duplicatesTrashed << ", missing removed " << rep.missingRemoved << ", missing kept (guard) " << rep.missingBlocked
            << ", tags synced " << rep.tagsSynced << ", tags still failing " << rep.tagsFailed << "\n";
        for (const QString& n : rep.notes)
            out << "  note: " << n << "\n";
        return check(session);
    }

    if (command == QLatin1String("import")) {
        QStringList paths;
        int year = 0;
        QString playlistName;
        for (int i = 2; i < args.size(); ++i) {
            if (args.at(i) == QLatin1String("--year") && i + 1 < args.size())
                year = args.at(++i).toInt();
            else if (args.at(i) == QLatin1String("--playlist") && i + 1 < args.size())
                playlistName = args.at(++i);
            else
                paths << args.at(i);
        }
        auto& importer = session.importer();
        auto scan = importer.scan(paths, consoleJob());
        if (!scan.ok())
            return fail(describe(scan.error()));
        out << scan.value().tracks.size() << " audio files, " << scan.value().skipped.size() << " unreadable\n";
        for (const SkippedFile& s : scan.value().skipped)
            out << "  skipped " << s.path << " (" << s.reason << ")\n";
        ImportProposal proposal = importer.propose(scan.value());
        if (!playlistName.isEmpty()) {
            proposal.kind = ImportKind::Playlist;
            proposal.playlist.name = playlistName;
        }
        Result<ImportResult> result = Error::make(ErrorCode::Unknown);
        if (proposal.kind == ImportKind::Album) {
            if (proposal.album.date.year == 0)
                proposal.album.date.year = year;
            if (proposal.album.date.year == 0)
                return fail(QStringLiteral("the tags have no year: add --year N"));
            out << "album \"" << proposal.album.title << "\" by " << proposal.album.authors.join(QStringLiteral(", ")) << " ("
                << proposal.album.tracks.size() << " tracks)\n";
            result = importer.commitAlbum(scan.value(), proposal.album, consoleJob());
        } else {
            if (proposal.playlist.name.trimmed().isEmpty())
                proposal.playlist.name = QStringLiteral("Imported");
            out << "playlist \"" << proposal.playlist.name << "\" (" << proposal.playlist.tracks.size() << " tracks)\n";
            result = importer.commitPlaylist(scan.value(), proposal.playlist, consoleJob());
        }
        if (!result.ok())
            return fail(describe(result.error()));
        out << "imported " << result.value().imported << ", already in the library " << result.value().duplicates << ", tag write failures "
            << result.value().report.tagWriteFailed.size() << "\n";
        return check(session);
    }
    return fail(QStringLiteral("unknown command ") + command);
}
