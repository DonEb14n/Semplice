#include "ui/HomeVm.h"

#include "core/cards/CardQueries.h"
#include "core/home/HomeQueries.h"
#include "ui/CardVms.h"

#include <QCoreApplication>
#include <QDateTime>

namespace semplice {

// ------------------------------------------------------------------------------------------ helpers

QString categoryTitle(const CategorySpec& spec, const GenreVocabulary* genres, const QString& language)
{
    switch (spec.type) {
    case CategoryType::ArtistDiscography:
        return QCoreApplication::translate("semplice::HomeVm", "%1: Discography").arg(spec.label);
    case CategoryType::Year: return spec.arg;
    case CategoryType::Decade: return QCoreApplication::translate("semplice::HomeVm", "%1s").arg(spec.arg);
    case CategoryType::Genre: {  // "mood:cheerful+lang:en+rap" -> "Cheerful English Rap"
        QStringList words;
        for (const QString& key : spec.arg.split(QLatin1Char('+'), Qt::SkipEmptyParts))
            words << (genres ? genres->name(key, language) : key);
        return words.join(QLatin1Char(' '));
    }
    case CategoryType::YourPlaylists: return QCoreApplication::translate("semplice::HomeVm", "Your Playlists");
    case CategoryType::YourAlbums: return QCoreApplication::translate("semplice::HomeVm", "Your Albums");
    }
    return {};
}

ListRow toListRow(const CarouselItem& item)
{
    return {item.id, item.title, item.subtitle, item.cover, item.ownerId, item.year,
            item.kind == ContainerKind::Playlist ? -1 : item.albumType, item.owners};
}

QList<ListRow> toListRows(const QList<CarouselItem>& items)
{
    QList<ListRow> out;
    out.reserve(items.size());
    for (const CarouselItem& i : items)
        out.append(toListRow(i));
    return out;
}

QList<ContainerRef> toRefs(const QList<ListRow>& rows)
{
    QList<ContainerRef> out;
    out.reserve(rows.size());
    for (const ListRow& r : rows)
        out.append({r.type < 0 ? ContainerKind::Playlist : ContainerKind::Album, r.id});
    return out;
}

// ------------------------------------------------------------------------------------------ CategoryListModel

QVariant CategoryListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};
    const Entry& e = m_entries[static_cast<size_t>(index.row())];
    switch (role) {
    case KeyRole: return e.key;
    case TitleRole: return e.title;
    case ItemsRole: return QVariant::fromValue(static_cast<QObject*>(e.items.get()));
    }
    return {};
}

void CategoryListModel::clear()
{
    beginResetModel();
    m_entries.clear();
    endResetModel();
}

void CategoryListModel::append(const QString& key, const QString& title, QList<ListRow> rows)
{
    const int row = rowCount();
    beginInsertRows({}, row, row);
    Entry e{key, title, std::make_unique<LibraryListModel>()};
    e.items->setRows(std::move(rows));
    m_entries.push_back(std::move(e));
    endInsertRows();
}

void CategoryListModel::update(const QString& key, const QString& title, QList<ListRow> rows)
{
    for (size_t i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].key == key) {
            m_entries[i].title = title;
            m_entries[i].items->setRows(std::move(rows));
            emit dataChanged(index(static_cast<int>(i)), index(static_cast<int>(i)), {TitleRole});
            return;
        }
    }
}

QStringList CategoryListModel::keys() const
{
    QStringList out;
    for (const Entry& e : m_entries)
        out << e.key;
    return out;
}

bool CategoryListModel::contains(const QString& key) const
{
    return keys().contains(key);
}

void CategoryListModel::removeKey(const QString& key)
{
    for (size_t i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].key == key) {
            beginRemoveRows({}, static_cast<int>(i), static_cast<int>(i));
            m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(i));
            endRemoveRows();
            return;
        }
    }
}

// ------------------------------------------------------------------------------------------ HomeVm

HomeVm::HomeVm(std::function<QString()> userName, const GenreVocabulary* genres, const QString& language, QObject* parent)
    : QObject(parent), m_userName(std::move(userName)), m_genres(genres), m_language(language)
{
    refreshGreeting();
}

void HomeVm::refreshGreeting()
{
    const QString name = m_userName ? m_userName() : QString();
    QString text;
    switch (pickGreeting(QDateTime::currentDateTime(), m_hasContent)) {
    case GreetingKind::Welcome: text = QCoreApplication::translate("semplice::HomeVm", "Welcome, %1!"); break;
    case GreetingKind::NewYear: text = QCoreApplication::translate("semplice::HomeVm", "Happy New Year, %1!"); break;
    case GreetingKind::Spring: text = QCoreApplication::translate("semplice::HomeVm", "Happy first day of spring, %1!"); break;
    case GreetingKind::Summer: text = QCoreApplication::translate("semplice::HomeVm", "Happy first day of summer, %1!"); break;
    case GreetingKind::Autumn: text = QCoreApplication::translate("semplice::HomeVm", "Happy first day of autumn, %1!"); break;
    case GreetingKind::Winter: text = QCoreApplication::translate("semplice::HomeVm", "Happy first day of winter, %1!"); break;
    case GreetingKind::Morning: text = QCoreApplication::translate("semplice::HomeVm", "Good morning, %1!"); break;
    case GreetingKind::Afternoon: text = QCoreApplication::translate("semplice::HomeVm", "Good afternoon, %1!"); break;
    case GreetingKind::Evening: text = QCoreApplication::translate("semplice::HomeVm", "Good evening, %1!"); break;
    }
    const QString result = text.arg(name);
    if (result != m_greeting) {
        m_greeting = result;
        emit greetingChanged();
    }
}

void HomeVm::refresh(bool recentOnly)
{
    refreshGreeting();
    emit homeRequested(m_categories.keys(), recentOnly);
}

void HomeVm::reset()
{
    m_loaded = false;
    m_hasContent = false;
    m_loadingMore = false;
    m_exhausted = false;
    m_recent.setRows({});
    m_categories.clear();
    refreshGreeting();
    emit changed();
}

void HomeVm::setHome(const HomeData& data)
{
    m_hasContent = data.hasContent;
    m_recent.setRows(toListRows(data.recent));
    if (!data.recentOnly) {
        m_categories.clear();
        for (const CategoryContents& c : data.categories)
            m_categories.append(c.spec.key(), categoryTitle(c.spec, m_genres, m_language), toListRows(c.items));
        m_exhausted = false;
        m_loadingMore = false;
    }
    m_loaded = true;
    refreshGreeting();
    emit changed();
}

void HomeVm::loadMore()
{
    if (!m_loaded || m_loadingMore || m_exhausted)
        return;
    m_loadingMore = true;
    emit moreRequested(3);
}

void HomeVm::addCategories(const QList<CategoryContents>& more)
{
    m_loadingMore = false;
    if (more.isEmpty()) {
        m_exhausted = true;
        return;
    }
    for (const CategoryContents& c : more)
        if (!m_categories.contains(c.spec.key()))
            m_categories.append(c.spec.key(), categoryTitle(c.spec, m_genres, m_language), toListRows(c.items));
}

void HomeVm::playFromCarousel(QObject* itemsModel, int index)
{
    const auto* model = qobject_cast<LibraryListModel*>(itemsModel);
    if (!model || index < 0 || index >= model->count())
        return;
    emit carouselPlayRequested(toRefs(model->rows()), index);
}

// ------------------------------------------------------------------------------------------ CategoryVm

QString CategoryVm::statsText(const QList<ListRow>& rows, qint64 durationMs)
{
    int albums = 0, playlists = 0;
    for (const ListRow& r : rows)
        (r.type < 0 ? playlists : albums)++;
    QString count;
    if (playlists == 0)
        count = QCoreApplication::translate("semplice::CategoryVm", "%n album(s)", nullptr, albums);
    else if (albums == 0)
        count = QCoreApplication::translate("semplice::CategoryVm", "%n playlist(s)", nullptr, playlists);
    else
        count = QCoreApplication::translate("semplice::CategoryVm", "%n item(s)", nullptr, albums + playlists);
    const qint64 minutes = durationMs / 60000;
    const QString length = minutes >= 60
        ? QCoreApplication::translate("semplice::CategoryVm", "%1 h %2 min").arg(minutes / 60).arg(minutes % 60)
        : QCoreApplication::translate("semplice::CategoryVm", "%1 min").arg(minutes);
    return count + QStringLiteral(", ") + length;
}

void CategoryVm::open(const QString& key)
{
    if (key == m_key && m_ready)
        return;
    m_key = key;
    m_ready = false;
    m_items.setRows({});
    emit changed();
    emit categoryRequested(key);
}

void CategoryVm::setContents(const CategoryContents& contents)
{
    // an answer for a page that is no longer open is ignored
    if (contents.requestedKey != m_key)
        return;
    const QList<ListRow> rows = toListRows(contents.items);
    if (m_key == QLatin1String("recent"))
        m_title = QCoreApplication::translate("semplice::CategoryVm", "Recently Played");
    else if (const auto section = CardQueries::parseSectionKey(m_key))
        m_title = QCoreApplication::translate("semplice::CategoryVm", "%1: %2").arg(contents.sectionLabel, sectionTitle(section->second));
    else
        m_title = categoryTitle(contents.spec, m_genres, m_language);
    m_stats = statsText(rows, contents.durationMs);
    m_items.setRows(rows);
    m_ready = true;
    emit changed();
}

void CategoryVm::reset()
{
    m_key.clear();
    m_ready = false;
    m_items.setRows({});
    emit changed();
}

void CategoryVm::playAll()
{
    if (m_items.count() == 0)
        return;
    emit playAllRequested(toRefs(m_items.rows()), m_title, m_key);
}

void CategoryVm::playItem(int index)
{
    if (index < 0 || index >= m_items.count())
        return;
    emit carouselPlayRequested(toRefs(m_items.rows()), index);
}

}  // namespace semplice
