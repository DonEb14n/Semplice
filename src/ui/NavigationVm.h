#pragma once

#include "core/browse/NavigationHistory.h"

#include <QObject>

namespace semplice {

// What the middle window shows, plus the hotbar's back / forward arrows. M6 only routes; the pages are M7/M8.
class NavigationVm : public QObject {
    Q_OBJECT
    Q_PROPERTY(int kind READ kind NOTIFY pageChanged FINAL)
    Q_PROPERTY(qint64 pageId READ pageId NOTIFY pageChanged FINAL)
    Q_PROPERTY(QString pageText READ pageText NOTIFY pageChanged FINAL)
    Q_PROPERTY(bool canBack READ canBack NOTIFY pageChanged FINAL)
    Q_PROPERTY(bool canForward READ canForward NOTIFY pageChanged FINAL)
    Q_PROPERTY(int direction READ direction NOTIFY pageChanged FINAL)   // the last change: -1 back, 1 forward / new page (the page slides in from there)
public:
    // The same numbers as semplice::PageKind (QML cannot see the C++ enum).
    enum Kind { Home, Settings, Artist, Album, Playlist, Category, SearchResults, AddArtist, AddAlbum, AddPlaylist, Import, EditArtist, EditAlbum, EditPlaylist, EditTrack, Share };
    Q_ENUM(Kind)

    explicit NavigationVm(QObject* parent = nullptr) : QObject(parent) {}

    int kind() const { return static_cast<int>(m_history.current().kind); }
    qint64 pageId() const { return m_history.current().id; }
    QString pageText() const { return m_history.current().text; }
    bool canBack() const { return m_history.canBack(); }
    int direction() const { return m_direction; }
    bool canForward() const { return m_history.canForward(); }

    Q_INVOKABLE void open(int kind, qint64 id = 0, const QString& text = {});
    Q_INVOKABLE void home() { open(Home); }
    Q_INVOKABLE void back();
    Q_INVOKABLE void forward();
    void forget(PageKind kind, qint64 id);

signals:
    void pageChanged();

private:
    NavigationHistory m_history;
    int m_direction = 1;
};

}  // namespace semplice
