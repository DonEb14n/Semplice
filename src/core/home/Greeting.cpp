#include "core/home/Greeting.h"

namespace semplice {

GreetingKind pickGreeting(const QDateTime& now, bool hasContent)
{
    if (!hasContent)
        return GreetingKind::Welcome;
    const QDate date = now.date();
    const int month = date.month();
    const int day = date.day();
    if ((month == 12 && day == 31) || (month == 1 && day <= 2))
        return GreetingKind::NewYear;
    if (day == 1) {
        switch (month) {
        case 3: return GreetingKind::Spring;
        case 6: return GreetingKind::Summer;
        case 9: return GreetingKind::Autumn;
        case 12: return GreetingKind::Winter;
        default: break;
        }
    }
    const int hour = now.time().hour();
    if (hour >= 5 && hour <= 11)
        return GreetingKind::Morning;
    if (hour >= 12 && hour <= 17)
        return GreetingKind::Afternoon;
    return GreetingKind::Evening;
}

}  // namespace semplice
