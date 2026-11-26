#include "ui/ShareVm.h"

#include "ui/CardVms.h"

#include <QCoreApplication>
#include <QFileInfo>

#include <algorithm>

namespace semplice {

namespace {
QString dateText(int year, int month, int day)
{
    if (year <= 0)
        return {};
    if (month <= 0)
        return QString::number(year);
    return day > 0 ? QStringLiteral("%1 / %2 / %3").arg(year).arg(month, 2, 10, QLatin1Char('0')).arg(day, 2, 10, QLatin1Char('0'))
                   : QStringLiteral("%1 / %2").arg(year).arg(month, 2, 10, QLatin1Char('0'));
}
QString typeLabel(int type)
{
    return albumTypeName(type);
}
}  // namespace

QVariantList ShareVm::tracksOf(int nodeIndex) const
{
    QVariantList tracks;
    if (!m_nodes.at(nodeIndex).expanded)
        return tracks;
    for (int j = nodeIndex + 1; j < m_nodes.size() && m_nodes.at(j).type == TrackRow; ++j) {
        const Node& t = m_nodes.at(j);
        tracks.append(QVariantMap{{QStringLiteral("index"), j},
                                  {QStringLiteral("title"), t.title},
                                  {QStringLiteral("subtitle"), t.subtitle},
                                  {QStringLiteral("cover"), t.cover},
                                  {QStringLiteral("durationMs"), t.durationMs},
                                  {QStringLiteral("checked"), t.checked},
                                  {QStringLiteral("existing"), t.existing},
                                  {QStringLiteral("position"), tracks.size() + 1}});
    }
    return tracks;
}

QVariant ShareVm::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const int i = m_rows.at(index.row());
    const Node& n = m_nodes.at(i);
    switch (role) {
    case TypeRole: return n.type;
    case NodeIndexRole: return i;
    case TitleRole: return n.title;
    case LabelRole: return n.label;
    case SubtitleRole: return n.subtitle;
    case CoverRole: return n.cover;
    case AuthorsRole: return n.authors;
    case GenresRole: return n.genres;
    case LanguagesRole: return n.languages;
    case MoodsRole: return n.moods;
    case DateTextRole: return n.dateText;
    case CheckedRole: return n.checked;
    case ExpandedRole: return n.expanded;
    case MainRole: return n.main;
    case HiddenRole: return n.hidden;
    case ExistingRole: return n.existing;
    case TracksRole: return tracksOf(i);
    }
    return {};
}

QHash<int, QByteArray> ShareVm::roleNames() const
{
    return {{TypeRole, "type"}, {NodeIndexRole, "nodeIndex"}, {TitleRole, "title"}, {LabelRole, "label"}, {SubtitleRole, "subtitle"},
            {CoverRole, "cover"}, {AuthorsRole, "authors"}, {GenresRole, "genres"}, {LanguagesRole, "languages"}, {MoodsRole, "moods"},
            {DateTextRole, "dateText"}, {CheckedRole, "checked"}, {ExpandedRole, "expanded"}, {MainRole, "main"}, {HiddenRole, "hidden"},
            {ExistingRole, "existing"}, {TracksRole, "tracks"}};
}

// a card (or the card of a track) changed: only its row is told, so the list keeps its place
void ShareVm::touch(int nodeIndex, const QList<int>& roles)
{
    const int card = m_nodes.at(nodeIndex).type == TrackRow ? m_nodes.at(nodeIndex).parent : nodeIndex;
    const int row = card >= 0 ? rowOf(card) : -1;
    if (row >= 0)
        emit dataChanged(index(row), index(row), roles);
    emit changed();
}

QString ShareVm::coverUrl(const QString& path) const
{
    if (path.isEmpty())
        return {};
    return QFileInfo(path).isAbsolute() ? QUrl::fromLocalFile(path).toString() : coverUrlFor(m_root, path);   // a picture of a package or of the library
}

bool ShareVm::canGo() const
{
    if (!m_ready || m_busy)
        return false;
    for (const Node& n : m_nodes)
        if (n.type == TrackRow && n.checked && !n.existing)
            return true;
    return false;
}

QString ShareVm::suggestedName() const
{
    QString name = m_tree.title;
    for (const QChar c : QStringLiteral("\\/:*?\"<>|"))
        name.replace(c, QLatin1Char('_'));
    if (m_tree.kind == ShareKind::Artist)
        name += QStringLiteral(" - Discography");
    return name.trimmed();
}

void ShareVm::open(int kind, qint64 id)
{
    m_file.clear();
    m_ready = false;
    m_busy = false;
    beginResetModel();
    m_nodes.clear();
    m_rows.clear();
    endResetModel();
    emit changed();
    emit treeRequested(kind, id);
}

void ShareVm::openFile(const QUrl& file)
{
    if (!file.isLocalFile())
        return;
    m_file = file.toLocalFile();
    m_ready = false;
    m_busy = false;
    beginResetModel();
    m_nodes.clear();
    m_rows.clear();
    endResetModel();
    emit changed();
    m_preview = std::make_unique<QTemporaryDir>();
    emit fileRequested(m_file, m_preview->isValid() ? m_preview->path() : QString());
}

void ShareVm::addTags(Node& node, const QStringList& keys) const
{
    for (const QString& key : keys) {
        const QString name = m_genres ? m_genres->name(key, m_language) : key;
        if (GenreVocabulary::isLanguageKey(key))
            node.languages << name;
        else if (GenreVocabulary::isMoodKey(key))
            node.moods << name;
        else
            node.genres << name;
    }
}

void ShareVm::setTree(const ShareTree& tree)
{
    beginResetModel();
    m_tree = tree;
    m_nodes.clear();
    const bool imp = tree.importing;
    auto heading = [&](const QString& text) {
        Node n;
        n.type = Heading;
        n.title = text;
        m_nodes.append(n);
    };
    auto addTrack = [&](int parent, const ShareTrackNode& t, const QString& key, const QString& cover) {
        Node n;
        n.type = TrackRow;
        n.id = t.id;
        n.key = key;
        n.parent = parent;
        n.title = t.title;
        n.subtitle = t.artists;
        n.durationMs = t.durationMs;
        n.cover = cover;
        n.existing = t.existing;
        n.checked = !t.existing;   // a track the library has is not imported again
        m_nodes.append(n);
    };
    auto addArtists = [&](bool mainOnly, bool all) {
        for (const ShareArtistNode& a : tree.artists) {
            if (!all && a.main != mainOnly)
                continue;
            Node n;
            n.type = ArtistCard;
            n.id = a.id;
            n.key = QStringLiteral("artist:") + a.name;
            n.title = a.name;
            n.main = a.main;
            n.hidden = a.hidden;
            n.existing = a.existing;
            n.label = a.hidden ? QCoreApplication::translate("semplice::ShareVm", "Hidden Artist") : QCoreApplication::translate("semplice::ShareVm", "Artist");
            n.cover = coverUrl(a.avatar);
            addTags(n, a.tags);
            m_nodes.append(n);
        }
    };

    if (tree.kind == ShareKind::Artist) {
        heading(QCoreApplication::translate("semplice::ShareVm", "Main Artists"));
        addArtists(true, false);
        bool related = false;
        for (const ShareArtistNode& a : tree.artists)
            related = related || !a.main;
        if (related) {
            heading(QCoreApplication::translate("semplice::ShareVm", "Related Artists"));
            addArtists(false, false);
        }
    } else if (tree.kind == ShareKind::Album) {
        heading(QCoreApplication::translate("semplice::ShareVm", "Main Artists"));
        addArtists(false, true);
    }

    if (!tree.albums.isEmpty()) {
        if (tree.kind == ShareKind::Artist)
            heading(QCoreApplication::translate("semplice::ShareVm", "Discography"));
        for (int i = 0; i < tree.albums.size(); ++i) {
            const ShareAlbumNode& a = tree.albums.at(i);
            Node n;
            n.type = AlbumCard;
            n.id = a.id;
            n.key = QStringLiteral("album:%1").arg(i);
            n.title = a.title;
            n.label = typeLabel(a.type);
            n.cover = coverUrl(a.cover);
            n.authors = a.authors.isEmpty() ? a.artists.split(QStringLiteral(", "), Qt::SkipEmptyParts) : a.authors;
            n.dateText = dateText(a.year, a.month, a.day);
            n.existing = a.existing;
            n.checked = !a.existing;   // an album the library has is skipped
            addTags(n, a.tags);
            const int parent = static_cast<int>(m_nodes.size());
            m_nodes.append(n);
            for (int j = 0; j < a.tracks.size(); ++j)
                addTrack(parent, a.tracks.at(j), QStringLiteral("track:%1:%2").arg(i).arg(j), n.cover);
        }
    }

    auto addPlaylist = [&](const QString& name, const QString& key, qint64 id, bool existing, const QList<ShareTrackNode>& tracks, const QString& coverRel, int index) {
        Node n;
        n.type = PlaylistCard;
        n.id = id;
        n.key = key;
        n.title = name;
        n.label = QCoreApplication::translate("semplice::ShareVm", "Playlist");
        n.cover = coverUrl(coverRel);
        n.existing = existing;   // a playlist of that name exists: the new one gets "(2)" - it is not skipped
        const int parent = static_cast<int>(m_nodes.size());
        m_nodes.append(n);
        for (int j = 0; j < tracks.size(); ++j)
            addTrack(parent, tracks.at(j), QStringLiteral("ptrack:%1:%2").arg(index).arg(j), QString());
    };
    if (imp) {
        for (int i = 0; i < tree.playlists.size(); ++i)
            addPlaylist(tree.playlists.at(i).name, QStringLiteral("playlist:%1").arg(i), 0, false, tree.playlists.at(i).tracks, tree.playlists.at(i).cover, i);
    } else if (tree.kind == ShareKind::Playlist) {
        addPlaylist(tree.title, QStringLiteral("playlist:0"), tree.id, false, tree.tracks, tree.cover, 0);
    }
    m_rows.clear();
    for (int i = 0; i < m_nodes.size(); ++i)
        if (m_nodes.at(i).type != TrackRow)
            m_rows.append(i);
    m_ready = true;
    m_busy = false;
    endResetModel();
    emit changed();
}

void ShareVm::toggle(int index)
{
    if (index < 0 || index >= m_nodes.size() || m_busy)
        return;
    Node& n = m_nodes[index];
    if (n.type == Heading)
        return;
    if (n.type == AlbumCard || n.type == PlaylistCard) {  // every track follows its card
        bool any = false;
        for (const Node& t : std::as_const(m_nodes))
            if (t.type == TrackRow && t.parent == index && t.checked)
                any = true;
        const bool on = !any;
        n.checked = on;
        for (int i = 0; i < m_nodes.size(); ++i)
            if (m_nodes[i].type == TrackRow && m_nodes[i].parent == index)
                m_nodes[i].checked = on && !m_nodes[i].existing;
    } else {
        n.checked = !n.checked;
        if (n.type == TrackRow && n.parent >= 0) {  // the card stays ticked while one of its tracks is
            bool any = false;
            for (const Node& t : std::as_const(m_nodes))
                if (t.type == TrackRow && t.parent == n.parent && t.checked)
                    any = true;
            m_nodes[n.parent].checked = any;
        }
    }
    touch(n.type == TrackRow && n.parent >= 0 ? n.parent : index, {CheckedRole, TracksRole});
}

void ShareVm::toggleExpanded(int index)
{
    if (index < 0 || index >= m_nodes.size() || m_nodes.at(index).type == TrackRow || m_nodes.at(index).type == Heading)
        return;
    m_nodes[index].expanded = !m_nodes.at(index).expanded;
    touch(index, {ExpandedRole, TracksRole});
}

void ShareVm::setHidden(int index, bool hidden)
{
    if (index < 0 || index >= m_nodes.size() || m_nodes.at(index).type != ArtistCard || m_busy)
        return;
    if (m_tree.importing && m_nodes.at(index).existing)
        return;   // the library's own status for an artist it has is not changed by a file
    m_nodes[index].hidden = hidden;
    m_nodes[index].label = hidden ? QCoreApplication::translate("semplice::ShareVm", "Hidden Artist") : QCoreApplication::translate("semplice::ShareVm", "Artist");
    touch(index, {HiddenRole, LabelRole});
}

ShareSelection ShareVm::exportSelection() const
{
    ShareSelection s;
    s.kind = m_tree.kind;
    s.id = m_tree.id;
    for (const Node& n : m_nodes) {
        if (n.type == ArtistCard) {
            if (n.checked)
                s.avatarArtists.insert(n.id);
            s.artistHidden.insert(n.id, n.hidden);
        } else if (!n.checked) {
            continue;
        } else if (n.type == AlbumCard) {
            s.albums.insert(n.id);
        } else if (n.type == TrackRow) {
            s.tracks.insert(n.id);
        }
    }
    return s;
}

ImportSelection ShareVm::importSelection() const
{
    ImportSelection s;
    s.all = false;
    for (const Node& n : m_nodes) {
        if (n.type == ArtistCard) {
            s.artistHidden.insert(n.title, n.hidden);
            if (n.checked)
                s.keys.insert(n.key);
        } else if (n.checked && n.type != Heading && !n.existing) {
            s.keys.insert(n.key);
        } else if (n.checked && n.type == PlaylistCard) {
            s.keys.insert(n.key);
        }
    }
    return s;
}

void ShareVm::exportTo(const QUrl& file)
{
    if (!canGo() || m_tree.importing || !file.isLocalFile())
        return;
    QString path = file.toLocalFile();
    if (QFileInfo(path).suffix().compare(QStringLiteral("smpl"), Qt::CaseInsensitive) != 0)
        path += QStringLiteral(".smpl");
    m_busy = true;
    m_done = m_total = 0;
    emit changed();
    emit exportRequested(path, exportSelection());
}

void ShareVm::importNow()
{
    if (!canGo() || !m_tree.importing || m_file.isEmpty())
        return;
    m_busy = true;
    m_done = m_total = 0;
    emit changed();
    emit importRequested(m_file, importSelection());
}

void ShareVm::setProgress(qint64 done, qint64 total)
{
    m_done = done;
    m_total = total;
    emit changed();
}

void ShareVm::finished()
{
    m_busy = false;
    emit changed();
}

}  // namespace semplice
