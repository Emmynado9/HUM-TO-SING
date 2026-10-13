#include "MainWindow.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QStringList>
#include <QTimer>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("HumToSing"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QCoreApplication::setOrganizationName(QStringLiteral("Hum-to-MIDI Music Sketchpad"));

    hum::app::MainWindow window;
    window.show();

    if (QCoreApplication::arguments().contains(QStringLiteral("--smoke-test"))) {
        QTimer::singleShot(250, &application, &QCoreApplication::quit);
    }

    return application.exec();
}
