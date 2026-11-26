#include "ui/LibraryListModel.h"

#include <QUrl>

namespace semplice {

QVariant LibraryListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};
    const ListRow& r = m_rows.at(index.row());
    switch (role) {
    case IdRole: return r.id;
    case Qt::DisplayRole:
    case TitleRole: return r.title;
    case SubtitleRole: return r.subtitle;
    case CoverRole:
        return r.cover.isEmpty() ? QString() : QStringLiteral("image://cover/") + QString::fromLatin1(QUrl::toPercentEncoding(r.cover, "/"));
    case OwnerRole: return r.ownerId;
    case YearRole: return r.year;
    case TypeRole: return r.type;
    case LetterRole: return r.title.trimmed().isEmpty() ? QString() : r.title.trimmed().left(1).toUpper();
    case DimmedRole: return r.dimmed;
    }
    return {};
}

QHash<int, QByteArray> LibraryListModel::roleNames() const
{
    return {{IdRole, "itemId"},   {TitleRole, "title"}, {SubtitleRole, "subtitle"}, {CoverRole, "cover"},
            {OwnerRole, "ownerId"}, {YearRole, "year"},  {TypeRole, "albumType"},    {LetterRole, "letter"}, {DimmedRole, "dimmed"}};
}

void LibraryListModel::setRows(QList<ListRow> rows)
{
    beginResetModel();
    m_rows = std::move(rows);
    if (m_limit >= 0 && m_rows.size() > m_limit)
        m_rows.resize(m_limit);
    endResetModel();
    emit countChanged();
}

void LibraryListModel::setLimit(int limit)
{
    m_limit = limit;
}

QVariantList LibraryListModel::ownersAt(int row) const
{
    QVariantList out;
    if (row < 0 || row >= m_rows.size())
        return out;
    for (const OwnerLink& o : m_rows.at(row).owners)
        out.append(QVariantMap{{QStringLiteral("id"), o.id}, {QStringLiteral("name"), o.name}});
    return out;
}

}  // namespace semplice
