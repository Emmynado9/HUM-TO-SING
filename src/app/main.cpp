#include "MainWindow.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QStringList>
#include <QTimer>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QDialog>
#include <QLabel>
#include <utility>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("HumToSing"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QCoreApplication::setOrganizationName(QStringLiteral("Hum-to-MIDI Music Sketchpad"));

    hum::app::MainWindow window;
    window.show();

    if (QCoreApplication::arguments().contains(QStringLiteral("--smoke-test"))) {
        QTimer::singleShot(250, &application, [&application, &window] {
            const auto complete_shell = window.isVisible()
                && window.findChild<QPushButton*>(QStringLiteral("openProjectButton"))
                && window.findChild<QPushButton*>(QStringLiteral("saveProjectButton"))
                && window.findChild<QPushButton*>(QStringLiteral("midiPlayButton"))
                && window.findChild<QWidget*>(QStringLiteral("playlist"))
                && window.findChild<QDoubleSpinBox*>(QStringLiteral("tempoSpin"));
            if (!complete_shell) { application.exit(2); return; }
            for (const auto& pair : {
                     std::pair{"openRecorderButton", "recordingDialog"},
                     std::pair{"openPianoRollButton", "pianoRollDialog"},
                     std::pair{"openPitchLabButton", "pitchLabDialog"}}) {
                auto* button = window.findChild<QPushButton*>(QString::fromLatin1(pair.first));
                auto* dialog = window.findChild<QDialog*>(QString::fromLatin1(pair.second));
                if (!button || !dialog || dialog->isVisible()) { application.exit(3); return; }
                button->click();
                if (!dialog->isVisible() || dialog->isModal()) { application.exit(4); return; }
                dialog->reject();
                button->click();
                if (!dialog->isVisible()) { application.exit(5); return; }
                dialog->reject();
            }
            auto* add_track = window.findChild<QPushButton*>(QStringLiteral("addTrackButton"));
            auto* undo = window.findChild<QPushButton*>(QStringLiteral("playlistUndoButton"));
            auto* redo = window.findChild<QPushButton*>(QStringLiteral("playlistRedoButton"));
            if (!add_track || !undo || !redo) { application.exit(6); return; }
            add_track->click();
            if (!undo->isEnabled()) { application.exit(7); return; }
            undo->click();
            if (!redo->isEnabled()) { application.exit(8); return; }
            redo->click();
            application.exit(0);
        });
    }

    return application.exec();
}
