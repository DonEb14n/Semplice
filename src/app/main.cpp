#include "app/AppBootstrap.h"

#include <QGuiApplication>

int main(int argc, char* argv[])
{
    // Fractional scaling (125 %, 150 %) is passed through unchanged (SPEC 8.1).
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QGuiApplication app(argc, argv);
    return semplice::runApplication(app);
}
