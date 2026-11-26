#pragma once

#include <QList>
#include <QString>

namespace semplice {

enum class BannerKind { Info, Error };

struct Banner {
    int id = 0;
    BannerKind kind = BannerKind::Info;
    QString key;    // translation key / message id, so the UI can build the (translated) text
    int count = 0;  // number for plural texts ("3 tracks could not be played"), 0 if unused
    QString detail; // raw text (a path, an error code); never translated
};

// The banners at the bottom of the middle window (UI doc p.27-28): at most three, the newest below the
// others, the oldest disappears when a fourth arrives; nothing expires on its own.
class BannerQueue {
public:
    static constexpr int kMaxVisible = 3;

    // Returns the id of the new banner.
    int push(BannerKind kind, const QString& key, int count = 0, const QString& detail = {})
    {
        m_banners.append({++m_nextId, kind, key, count, detail});
        while (m_banners.size() > kMaxVisible)
            m_banners.removeFirst();
        return m_nextId;
    }
    bool dismiss(int id)
    {
        for (int i = 0; i < m_banners.size(); ++i) {
            if (m_banners.at(i).id == id) {
                m_banners.removeAt(i);
                return true;
            }
        }
        return false;
    }
    void clear() { m_banners.clear(); }
    const QList<Banner>& banners() const { return m_banners; }

private:
    QList<Banner> m_banners;
    int m_nextId = 0;
};

}  // namespace semplice
