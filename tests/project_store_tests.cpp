#include "ProjectFileStore.hpp"

#include <QFile>
#include <QTemporaryDir>

#include <iostream>
#include <string_view>

namespace {

int failures = 0;
int checks = 0;

void expect(const bool condition, const std::string_view message) {
    ++checks;
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void testAtomicSaveLoadAndOverwrite() {
    QTemporaryDir directory;
    expect(directory.isValid(), "temporary project directory is available");
    const auto path = directory.filePath(QStringLiteral("song.humproj"));
    hum::core::ProjectDocument source;
    source.tempo_bpm = 108.0;
    source.canonical_notes = {{61, 91, 0.25, 0.5, 0}};
    const auto first_save = hum::app::ProjectFileStore::save(path, source);
    expect(static_cast<bool>(first_save), "new project saves through atomic store");
    const auto first_load = hum::app::ProjectFileStore::load(path);
    expect(static_cast<bool>(first_load), "saved project loads through store");
    expect(first_load && first_load.project->canonical_notes[0].pitch == 61,
        "loaded file contains saved canonical pitch");

    source.canonical_notes[0].pitch = 68;
    const auto overwrite = hum::app::ProjectFileStore::save(path, source);
    const auto second_load = hum::app::ProjectFileStore::load(path);
    expect(static_cast<bool>(overwrite), "existing project is safely replaced");
    expect(second_load && second_load.project->canonical_notes[0].pitch == 68,
        "replacement file is complete and current");
}

void testFailuresAreActionable() {
    QTemporaryDir directory;
    const auto missing = hum::app::ProjectFileStore::load(
        directory.filePath(QStringLiteral("missing.humproj")));
    expect(missing.status == hum::app::ProjectStoreStatus::cannot_open
            && !missing.message.isEmpty(),
        "missing file returns an actionable open failure");

    const auto corrupt_path = directory.filePath(QStringLiteral("corrupt.humproj"));
    QFile corrupt(corrupt_path);
    expect(corrupt.open(QIODevice::WriteOnly), "corrupt fixture opens");
    static_cast<void>(corrupt.write("not a project\n"));
    corrupt.close();
    const auto corrupt_result = hum::app::ProjectFileStore::load(corrupt_path);
    expect(corrupt_result.status == hum::app::ProjectStoreStatus::corrupt
            && !corrupt_result.message.isEmpty(),
        "corrupt file returns a distinct actionable failure");

    const auto directory_save = hum::app::ProjectFileStore::save(directory.path(), {});
    expect(!directory_save && !directory_save.message.isEmpty(),
        "save failure is reported without claiming success");
}

} // namespace

int main() {
    testAtomicSaveLoadAndOverwrite();
    testFailuresAreActionable();
    if (failures == 0) {
        std::cout << "All project store tests passed (" << checks << " assertions).\n";
        return 0;
    }
    std::cerr << failures << " project store assertion(s) failed.\n";
    return 1;
}
