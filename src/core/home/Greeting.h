#pragma once

#include <QDateTime>

namespace semplice {

enum class GreetingKind { Welcome, NewYear, Spring, Summer, Autumn, Winter, Morning, Afternoon, Evening };

// Which greeting the Home page shows (UI doc p.23-24, SPEC 8.6). Pure: the texts belong to the view-model.
//   empty library              -> Welcome ("Welcome, {name}!")
//   Dec 31 - Jan 2             -> NewYear
//   Mar 1 / Jun 1 / Sep 1 / Dec 1 (first day of a meteorological season, northern hemisphere) -> that season
//   otherwise by local time    -> Morning 05-11, Afternoon 12-17, Evening 18-04
GreetingKind pickGreeting(const QDateTime& localNow, bool libraryHasContent);

}  // namespace semplice
