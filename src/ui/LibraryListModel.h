#pragma once

#include "ui/LibraryEngine.h"

#include <QAbstractListModel>

namespace semplice {

// A whole library list (artists / albums / playlists / search hits). The rows are swapped in one step;
// QML views create delegates only for what is visible, so 200k rows cost nothing but memory.
class LibraryListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
public:
    enum Role { IdRole = Qt::UserRole + 1, TitleRole, SubtitleRole, CoverRole, OwnerRole, YearRole, TypeRole, LetterRole, DimmedRole };

    explicit LibraryListModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : static_cast<int>(m_rows.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(m_rows.size()); }
    void setRows(QList<ListRow> rows);
    // Only the first `limit` rows are visible (search: 5 per category until "Show more").
    void setLimit(int limit);
    const QList<ListRow>& rows() const { return m_rows; }
    Q_INVOKABLE qint64 idAt(int row) const { return row >= 0 && row < m_rows.size() ? m_rows.at(row).id : 0; }
    Q_INVOKABLE QString titleAt(int row) const { return row >= 0 && row < m_rows.size() ? m_rows.at(row).title : QString(); }
    Q_INVOKABLE QString subtitleAt(int row) const { return row >= 0 && row < m_rows.size() ? m_rows.at(row).subtitle : QString(); }
    // the picture url of a row (carousels build their tiles by index)
    Q_INVOKABLE QString coverAt(int row) const { return row >= 0 && row < m_rows.size() && !m_rows.at(row).cover.isEmpty() ? data(index(row), CoverRole).toString() : QString(); }
    Q_INVOKABLE qint64 ownerAt(int row) const { return row >= 0 && row < m_rows.size() ? m_rows.at(row).ownerId : 0; }
    // [{ id, name }] of every artist of an album row (empty when the row has no artist links)
    Q_INVOKABLE QVariantList ownersAt(int row) const;
    // AlbumType of an album row, -1 for a playlist
    Q_INVOKABLE int typeAt(int row) const { return row >= 0 && row < m_rows.size() ? m_rows.at(row).type : 0; }

signals:
    void countChanged();

private:
    QList<ListRow> m_rows;
    int m_limit = -1;
};

}  // namespace semplice
