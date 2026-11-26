#pragma once

#include "core/home/Greeting.h"
#include "core/tags/GenreVocabulary.h"
#include "ui/LibraryEngine.h"
#include "ui/LibraryListModel.h"

#include <QAbstractListModel>
#include <QObject>

#include <functional>
#include <memory>

namespace semplice {

// Titles of the generated categories ("Wild Garden: Discography", "2019", "1990s", "Rock", "Your Albums" ...).
QString categoryTitle(const CategorySpec& spec, const GenreVocabulary* genres, const QString& language);

// CarouselItem -> list row. Albums keep their AlbumType in `type`; playlists are marked with -1.
ListRow toListRow(const CarouselItem& item);
QList<ListRow> toListRows(const QList<CarouselItem>& items);
// Rows back to the references the play-request builder wants.
QList<ContainerRef> toRefs(const QList<ListRow>& rows);

// The carousels below "Recently Played": every row has its own item model.
class CategoryListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { KeyRole = Qt::UserRole + 1, TitleRole, ItemsRole };
    explicit CategoryListModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : static_cast<int>(m_entries.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override { return {{KeyRole, "key"}, {TitleRole, "title"}, {ItemsRole, "items"}}; }

    void clear();
    void append(const QString& key, const QString& title, QList<ListRow> rows);
    // Replaces the items of known categories (and the titles), keeps their order.
    void update(const QString& key, const QString& title, QList<ListRow> rows);
    QStringList keys() const;
    bool contains(const QString& key) const;
    void removeKey(const QString& key);

private:
    struct Entry {
        QString key;
        QString title;
        std::unique_ptr<LibraryListModel> items;
    };
    std::vector<Entry> m_entries;
};

// The Home page (UI doc p.23-26).
class HomeVm : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString greeting READ greeting NOTIFY greetingChanged FINAL)
    Q_PROPERTY(bool hasContent READ hasContent NOTIFY changed FINAL)
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed FINAL)
    Q_PROPERTY(QObject* recent READ recent CONSTANT FINAL)
    Q_PROPERTY(QObject* categories READ categories CONSTANT FINAL)
public:
    // `userName` returns the name to show (the localised "Listener" when the user set none).
    HomeVm(std::function<QString()> userName, const GenreVocabulary* genres, const QString& language, QObject* parent = nullptr);

    QString greeting() const { return m_greeting; }
    bool hasContent() const { return m_hasContent; }
    bool loaded() const { return m_loaded; }
    QObject* recent() { return &m_recent; }
    QObject* categories() { return &m_categories; }

    // The page became visible / the library changed / something was played.
    Q_INVOKABLE void refresh(bool recentOnly = false);
    // Called when the page nears its end while scrolling: more categories while content lasts.
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void refreshGreeting();
    // Start the item at `index` of a carousel (the Recently Played carousel or a category carousel).
    Q_INVOKABLE void playFromCarousel(QObject* itemsModel, int index);
    void reset();  // library closed

    // from the engine
    void setHome(const HomeData& data);
    void addCategories(const QList<CategoryContents>& more);

signals:
    void changed();
    void greetingChanged();
    void homeRequested(const QStringList& knownKeys, bool recentOnly);
    void moreRequested(int count);
    void carouselPlayRequested(const QList<semplice::ContainerRef>& items, int index);

private:
    std::function<QString()> m_userName;
    const GenreVocabulary* m_genres;
    QString m_language;
    QString m_greeting;
    bool m_hasContent = false;
    bool m_loaded = false;
    bool m_loadingMore = false;
    bool m_exhausted = false;
    LibraryListModel m_recent;
    CategoryListModel m_categories;
};

// The category page: the whole list of one category as tiles (UI doc p.25).
class CategoryVm : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY changed FINAL)
    Q_PROPERTY(QString key READ key NOTIFY changed FINAL)
    Q_PROPERTY(QString title READ title NOTIFY changed FINAL)
    Q_PROPERTY(QString stats READ stats NOTIFY changed FINAL)
    Q_PROPERTY(QObject* items READ items CONSTANT FINAL)
public:
    CategoryVm(const GenreVocabulary* genres, const QString& language, QObject* parent = nullptr)
        : QObject(parent), m_genres(genres), m_language(language)
    {
    }

    bool ready() const { return m_ready; }
    QString key() const { return m_key; }
    QString title() const { return m_title; }
    QString stats() const { return m_stats; }
    QObject* items() { return &m_items; }

    // Opens a category (called when the page is shown); the answer arrives through setContents().
    Q_INVOKABLE void open(const QString& key);
    void reload() { emit categoryRequested(m_key); }
    // The play button: ONE queue with everything in the category.
    Q_INVOKABLE void playAll();
    // A tile: that item, then the next ones as separate queues (like a carousel).
    Q_INVOKABLE void playItem(int index);
    void setContents(const CategoryContents& contents);
    void reset();

    static QString statsText(const QList<ListRow>& rows, qint64 durationMs);

signals:
    void changed();
    void categoryRequested(const QString& key);
    void playAllRequested(const QList<semplice::ContainerRef>& items, const QString& title, const QString& key);
    void carouselPlayRequested(const QList<semplice::ContainerRef>& items, int index);

private:
    const GenreVocabulary* m_genres;
    QString m_language;
    QString m_key;
    QString m_title;
    QString m_stats;
    bool m_ready = false;
    LibraryListModel m_items;
};

}  // namespace semplice
