#pragma once

#include "core/storage/ITrash.h"

#include <QDir>
#include <QFileInfo>

namespace semplice::testing {

// Test double for the Recycle Bin: moves the item into <sandbox>/<n>/<name> so a test can check
// that nothing was lost. Never deletes anything.
class FakeTrash final : public ITrash {
public:
    explicit FakeTrash(QString sandbox) : m_sandbox(std::move(sandbox)) {}

    Result<void> moveToTrash(const QString& absolutePath) override
    {
        if (m_failNext) {
            m_failNext = false;
            return Error::make(ErrorCode::IoError, QStringLiteral("simulated Recycle Bin failure"));
        }
        const QFileInfo info(absolutePath);
        if (!info.exists())
            return Error::make(ErrorCode::NotFound, absolutePath);
        const QString slot = QDir(m_sandbox).filePath(QString::number(++m_counter));
        QDir().mkpath(slot);
        const QString target = QDir(slot).filePath(info.fileName());
        const bool ok = info.isDir() ? QDir().rename(absolutePath, target) : QFile::rename(absolutePath, target);
        if (!ok)
            return Error::make(ErrorCode::IoError, QStringLiteral("fake trash failed for ") + absolutePath);
        m_items << info.fileName();
        return {};
    }

    void failNext() { m_failNext = true; }
    QStringList items() const { return m_items; }
    QString sandbox() const { return m_sandbox; }

private:
    QString m_sandbox;
    int m_counter = 0;
    bool m_failNext = false;
    QStringList m_items;
};

}  // namespace semplice::testing
