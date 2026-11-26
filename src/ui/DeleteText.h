#pragma once

#include "ui/EditorTypes.h"

#include <QString>

namespace semplice {

// The confirmation card of a delete: its title and a sentence that states what will really happen (UI doc p.33,
// 38, 43, 47). The sentences come from the counts of LibraryService::planDelete*; each one is translated whole.
QString deleteTitle(int op);
QString deleteText(const DeleteSummary& plan);

}  // namespace semplice
