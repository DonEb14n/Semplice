#include "core/home/CategoryGenerator.h"

#include <algorithm>

namespace semplice {

void CategoryGenerator::reset()
{
    m_shown.clear();
    m_typesInCycle.clear();
}

QList<CategorySpec> CategoryGenerator::next(int count)
{
    static const CategoryType kTypes[] = {CategoryType::ArtistDiscography, CategoryType::Year, CategoryType::Decade,
                                          CategoryType::Genre, CategoryType::YourPlaylists, CategoryType::YourAlbums};
    QList<CategorySpec> out;
    // unused instances per type, computed once per call and consumed as they are picked
    QHash<int, QList<CategorySpec>> pool;
    for (CategoryType t : kTypes) {
        QList<CategorySpec> specs;
        for (const auto& inst : m_source.instances(t)) {
            CategorySpec spec{t, inst.arg, inst.label};
            if (!m_shown.contains(spec.key()))
                specs.append(spec);
        }
        if (!specs.isEmpty())
            pool.insert(static_cast<int>(t), specs);
    }
    while (out.size() < count && !pool.isEmpty()) {
        QList<int> candidates;
        for (auto it = pool.cbegin(); it != pool.cend(); ++it)
            if (!m_typesInCycle.contains(it.key()))
                candidates.append(it.key());
        if (candidates.isEmpty()) {  // every eligible type was used: the cycle starts again
            m_typesInCycle.clear();
            candidates = pool.keys();
        }
        std::sort(candidates.begin(), candidates.end());  // the RNG decides, not the hash order
        const int type = candidates.at(static_cast<int>(m_rng() % static_cast<quint64>(candidates.size())));
        QList<CategorySpec>& specs = pool[type];
        const int pick = static_cast<int>(m_rng() % static_cast<quint64>(specs.size()));
        const CategorySpec spec = specs.takeAt(pick);
        if (specs.isEmpty())
            pool.remove(type);
        m_shown.insert(spec.key());
        m_typesInCycle.insert(type);
        out.append(spec);
    }
    return out;
}

}  // namespace semplice
