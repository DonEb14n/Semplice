#pragma once

#include "ui/LibraryListModel.h"

#include <QObject>

namespace semplice {

// The search-and-pick submenu of the editors and track menus (UI doc p.37-38): a search text, five visible
// results (the list scrolls), the answer of the engine thread. One picker is open at a time.
class PickerVm : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* results READ results CONSTANT FINAL)
public:
    explicit PickerVm(QObject* parent = nullptr) : QObject(parent) {}
    QObject* results() { return &m_results; }

    // kind: 0 artists (hidden ones too), 1 playlists, 2 albums
    Q_INVOKABLE void search(int kind, const QString& text)
    {
        m_kind = kind;
        emit queryRequested(kind, text, ++m_token);
    }
    Q_INVOKABLE void clear()
    {
        ++m_token;
        m_results.setRows({});
    }
    void setResults(const QList<ListRow>& rows, int token)
    {
        if (token == m_token)
            m_results.setRows(rows);
    }

signals:
    void queryRequested(int kind, const QString& text, int token);

private:
    int m_token = 0;
    int m_kind = 0;
    LibraryListModel m_results;
};

}  // namespace semplice
