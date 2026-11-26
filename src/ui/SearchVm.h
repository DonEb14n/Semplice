#pragma once

#include "ui/LibraryListModel.h"

#include <QObject>
#include <QTimer>

namespace semplice {

// The search popup (UI doc p.18): artists, albums, playlists, tracks in that order, five per category
// with "Show more", categories without hits omitted. The text survives closing the popup.
class SearchVm : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged FINAL)
    Q_PROPERTY(QObject* artists READ artists CONSTANT FINAL)
    Q_PROPERTY(QObject* albums READ albums CONSTANT FINAL)
    Q_PROPERTY(QObject* playlists READ playlists CONSTANT FINAL)
    Q_PROPERTY(QObject* tracks READ tracks CONSTANT FINAL)
    Q_PROPERTY(int artistsTotal READ total0 NOTIFY resultsChanged FINAL)
    Q_PROPERTY(int albumsTotal READ total1 NOTIFY resultsChanged FINAL)
    Q_PROPERTY(int playlistsTotal READ total2 NOTIFY resultsChanged FINAL)
    Q_PROPERTY(int tracksTotal READ total3 NOTIFY resultsChanged FINAL)
    Q_PROPERTY(bool hasResults READ hasResults NOTIFY resultsChanged FINAL)
public:
    static constexpr int kPerCategory = 5;
    enum Category { Artists, Albums, Playlists, Tracks };
    Q_ENUM(Category)

    explicit SearchVm(QObject* parent = nullptr);

    QString text() const { return m_text; }
    void setText(const QString& text);
    QObject* artists() { return &m_models[Artists]; }
    QObject* albums() { return &m_models[Albums]; }
    QObject* playlists() { return &m_models[Playlists]; }
    QObject* tracks() { return &m_models[Tracks]; }
    int total0() const { return m_totals[Artists]; }
    int total1() const { return m_totals[Albums]; }
    int total2() const { return m_totals[Playlists]; }
    int total3() const { return m_totals[Tracks]; }
    bool hasResults() const { return m_totals[0] + m_totals[1] + m_totals[2] + m_totals[3] > 0; }

    // "Show more": the whole category is listed.
    Q_INVOKABLE void showMore(int category);
    // The library changed: run the same query again.
    void refresh() { if (!m_text.trimmed().isEmpty()) request(); }
    Q_INVOKABLE bool canShowMore(int category) const { return !m_expanded[category] && m_totals[category] > kPerCategory; }

    // Called by the controller with the engine's answer.
    void setResults(const SearchResults& results, int token);
    void clearResults();

signals:
    void textChanged();
    void resultsChanged();
    void queryRequested(const QString& text, int token);

private:
    void fill(int category);
    void request();

    QString m_text;
    int m_token = 0;
    QTimer m_debounce;
    LibraryListModel m_models[4];
    QList<ListRow> m_all[4];
    int m_totals[4] = {0, 0, 0, 0};
    bool m_expanded[4] = {false, false, false, false};
};

}  // namespace semplice
