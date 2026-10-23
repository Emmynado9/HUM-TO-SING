#include "ProjectFileStore.hpp"

#include <QFile>
#include <QSaveFile>

#include <limits>

namespace hum::app {

namespace {

constexpr qint64 kMaximumProjectBytes = 64 * 1024 * 1024;

} // namespace

ProjectLoadResult ProjectFileStore::load(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {ProjectStoreStatus::cannot_open, std::nullopt,
            QStringLiteral("The project could not be opened: %1").arg(file.errorString())};
    }
    if (file.size() < 0 || file.size() > kMaximumProjectBytes) {
        return {ProjectStoreStatus::too_large, std::nullopt,
            QStringLiteral("The project is larger than the 64 MiB safety limit.")};
    }
    const auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        return {ProjectStoreStatus::read_failed, std::nullopt,
            QStringLiteral("The project could not be read: %1").arg(file.errorString())};
    }
    const auto decoded = core::ProjectFileCodec::decode(
        std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    if (!decoded) {
        const auto status = decoded.status == core::ProjectDecodeStatus::unsupported_version
            ? ProjectStoreStatus::unsupported_version : ProjectStoreStatus::corrupt;
        return {status, std::nullopt, QString::fromStdString(decoded.message)};
    }
    return {ProjectStoreStatus::ok, std::move(decoded.project), {}};
}

ProjectSaveResult ProjectFileStore::save(
    const QString& path,
    const core::ProjectDocument& project) {
    const auto encoded = core::ProjectFileCodec::encode(project);
    if (!encoded || encoded->size() > static_cast<std::size_t>(kMaximumProjectBytes)
        || encoded->size() > static_cast<std::size_t>(std::numeric_limits<qint64>::max())) {
        return {ProjectStoreStatus::invalid_project,
            QStringLiteral("The current project contains values that cannot be saved safely.")};
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return {ProjectStoreStatus::cannot_open,
            QStringLiteral("The project destination could not be opened: %1").arg(file.errorString())};
    }
    const auto written = file.write(encoded->data(), static_cast<qint64>(encoded->size()));
    if (written != static_cast<qint64>(encoded->size()) || !file.commit()) {
        file.cancelWriting();
        return {ProjectStoreStatus::write_failed,
            QStringLiteral("The project could not be committed safely: %1").arg(file.errorString())};
    }
    return {ProjectStoreStatus::ok, {}};
}

} // namespace hum::app
