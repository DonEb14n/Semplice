// Not compiled. Used by the test "no_direct_deletes_detects_violations": the guard script must reject this.
#include <QFile>
void bad() { QFile::remove("x"); }
