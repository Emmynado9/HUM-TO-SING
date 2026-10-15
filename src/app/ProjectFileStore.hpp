#pragma once

#include "hum/core/project_file.hpp"

#include <QString>

#include <optional>

namespace hum::app {

enum class ProjectStoreStatus {
    ok,
    invalid_project,
    cannot_open,
    read_failed,
    write_failed,
    corrupt,
    unsupported_version,
    too_large,
};

struct ProjectLoadResult {
    ProjectStoreStatus status{ProjectStoreStatus::corrupt};
    std::optional<core::ProjectDocument> project;
    QString message;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == ProjectStoreStatus::ok && project.has_value();
    }
};

struct ProjectSaveResult {
    ProjectStoreStatus status{ProjectStoreStatus::write_failed};
    QString message;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == ProjectStoreStatus::ok;
    }
};

class ProjectFileStore final {
public:
    [[nodiscard]] static ProjectLoadResult load(const QString& path);
    [[nodiscard]] static ProjectSaveResult save(
        const QString& path,
        const core::ProjectDocument& project);
};

} // namespace hum::app
