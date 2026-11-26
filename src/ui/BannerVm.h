#pragma once

#include "core/browse/BannerQueue.h"

#include <QAbstractListModel>

namespace semplice {

// The banners at the bottom of the middle window. Texts are built here (translated, with plural forms)
// from the message key so the core never holds user-visible strings.
class BannerVm : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { IdRole = Qt::UserRole + 1, TitleRole, TextRole, IsErrorRole };
    explicit BannerVm(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : static_cast<int>(m_queue.banners().size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override { return {{IdRole, "bannerId"}, {TitleRole, "title"}, {TextRole, "text"}, {IsErrorRole, "isError"}}; }

    // Keys: tracks_skipped(count), folder_not_empty, folder_missing, library_unusable(detail = code), schema_too_new,
    // cannot_play, generic_error (the "Error -1" banner of the UI doc).
    Q_INVOKABLE void info(const QString& key, int count = 0, const QString& detail = {});
    Q_INVOKABLE void error(const QString& key, int count = 0, const QString& detail = {});
    Q_INVOKABLE void dismiss(int id);

    static QString titleFor(const Banner& banner);
    static QString textFor(const Banner& banner);

private:
    void push(BannerKind kind, const QString& key, int count, const QString& detail);
    BannerQueue m_queue;
};

}  // namespace semplice
