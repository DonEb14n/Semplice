#pragma once

#include "core/playback/PlaybackService.h"

#include <QObject>
#include <QPointer>

namespace semplice {

// The player as QML sees it: texts, targets, times and modes of the current PlaybackService (which comes
// and goes with the open library). All the rules live in the service; this class only formats.
class PlayerVm : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool hasTrack READ hasTrack NOTIFY changed FINAL)
    Q_PROPERTY(bool playing READ playing NOTIFY changed FINAL)
    Q_PROPERTY(QString headerText READ headerText NOTIFY changed FINAL)
    Q_PROPERTY(bool headerIsDiscography READ headerIsDiscography NOTIFY changed FINAL)
    Q_PROPERTY(bool headerClickable READ headerClickable NOTIFY changed FINAL)
    Q_PROPERTY(QString title READ title NOTIFY changed FINAL)
    Q_PROPERTY(QString artist READ artist NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList artists READ artists NOTIFY changed FINAL)  // [{ id, name }]: one link per credited artist
    Q_PROPERTY(QString coverUrl READ coverUrl NOTIFY changed FINAL)
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged FINAL)
    Q_PROPERTY(qint64 duration READ duration NOTIFY changed FINAL)
    Q_PROPERTY(bool shuffle READ shuffle NOTIFY changed FINAL)
    Q_PROPERTY(int repeat READ repeat NOTIFY changed FINAL)  // 0 off, 1 all, 2 one
    Q_PROPERTY(bool canPrevious READ canPrevious NOTIFY changed FINAL)
    Q_PROPERTY(bool canNext READ canNext NOTIFY changed FINAL)
    Q_PROPERTY(double volume READ volume NOTIFY changed FINAL)
    Q_PROPERTY(bool muted READ muted NOTIFY changed FINAL)
    Q_PROPERTY(bool queueAvailable READ queueAvailable NOTIFY changed FINAL)
public:
    explicit PlayerVm(QObject* parent = nullptr) : QObject(parent) {}

    // nullptr detaches (library closed). Volume and mute stay valid without a service.
    void attach(PlaybackService* service);
    void setVolumeSource(ISettings* settings) { m_settings = settings; }

    bool hasTrack() const { return m_service && m_service->currentTrack().has_value(); }
    bool playing() const { return m_service && m_service->state() == PlayState::Playing; }
    QString headerText() const;
    bool headerIsDiscography() const;
    bool headerClickable() const;
    QString title() const;
    QString artist() const;
    QVariantList artists() const;
    QString coverUrl() const;
    qint64 position() const { return m_service ? m_service->positionMs() : 0; }
    qint64 duration() const { return m_service ? m_service->durationMs() : 0; }
    bool shuffle() const { return m_service && m_service->shuffle(); }
    int repeat() const { return m_service ? static_cast<int>(m_service->repeat()) : 0; }
    bool canPrevious() const { return m_service && m_service->canPrevious(); }
    bool canNext() const { return m_service && m_service->canNext(); }
    double volume() const;
    bool muted() const;
    bool queueAvailable() const { return m_service && !m_service->queue().isEmpty(); }

    Q_INVOKABLE void toggle() { if (m_service) m_service->toggle(); }
    Q_INVOKABLE void next() { if (m_service) m_service->next(); }
    Q_INVOKABLE void previous() { if (m_service) m_service->previous(); }
    Q_INVOKABLE void seek(qint64 ms) { if (m_service) m_service->seek(ms); }
    Q_INVOKABLE void toggleShuffle() { if (m_service) m_service->setShuffle(!m_service->shuffle()); }
    Q_INVOKABLE void cycleRepeat();
    Q_INVOKABLE void setVolume(double volume01);
    Q_INVOKABLE void toggleMute();
    Q_INVOKABLE void stepVolume(double delta) { setVolume(volume() + delta); }

    // Targets for the header / title / artist links: kind is a NavigationVm::Kind, id 0 = nothing to open.
    Q_INVOKABLE int headerKind() const;
    Q_INVOKABLE qint64 headerId() const;
    Q_INVOKABLE QString headerKey() const;  // category key of a carousel queue
    Q_INVOKABLE int titleKind() const;
    Q_INVOKABLE qint64 titleId() const;
    Q_INVOKABLE qint64 artistId() const;

    PlaybackService* service() const { return m_service; }

signals:
    void changed();
    void positionChanged();

private:
    QPointer<PlaybackService> m_service;
    ISettings* m_settings = nullptr;
    double m_volume = 0.5;
    bool m_muted = false;
};

}  // namespace semplice
