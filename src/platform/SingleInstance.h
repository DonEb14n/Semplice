#pragma once

#include <QLocalServer>
#include <QObject>
#include <QString>

namespace semplice::platform {

// Only one Semplice process per user session. A second launch asks the first to come to the
// front and exits. Windows: named mutex decides who is first, a local socket carries the request.
class SingleInstance : public QObject {
    Q_OBJECT
public:
    explicit SingleInstance(const QString& key, QObject* parent = nullptr);
    ~SingleInstance() override;

    // true  = this process is the primary instance (and now listens for activation requests)
    // false = another instance is running and has been asked to activate; the caller should exit
    bool acquire();

signals:
    void activationRequested();

private:
    bool notifyRunningInstance();

    QString m_key;
    QLocalServer m_server;
    void* m_mutex = nullptr;  // HANDLE on Windows
};

}  // namespace semplice::platform
