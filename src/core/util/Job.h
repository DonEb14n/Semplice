#pragma once

#include <QString>

#include <functional>
#include <stop_token>

namespace semplice {

struct JobProgress {
    QString phase;     // short machine-readable step name ("scan", "copy", "tags", ...)
    qint64 done = 0;
    qint64 total = 0;  // 0 = unknown
};

// Passed to long-running service calls (SPEC 3): cancel with request_stop() on the matching
// std::stop_source, watch progress through the callback (called on the executing thread).
struct JobContext {
    std::stop_token cancel;
    std::function<void(const JobProgress&)> progress;

    bool cancelled() const { return cancel.stop_requested(); }
    void report(const QString& phase, qint64 done, qint64 total = 0) const
    {
        if (progress)
            progress({phase, done, total});
    }
};

}  // namespace semplice
