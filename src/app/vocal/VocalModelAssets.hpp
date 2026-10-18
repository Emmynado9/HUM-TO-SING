#pragma once
#include "hum/vocal/neural_pitch.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>

namespace hum::app::vocal {
inline std::vector<std::uint8_t> bundledModel() {
    QFile file(QStringLiteral(":/vocal/swift-f0.onnx"));
    if (!file.open(QIODevice::ReadOnly)) { return {}; }
    const auto bytes = file.readAll(); return {bytes.begin(), bytes.end()};
}
inline std::string bundledRuntime() {
#ifdef Q_OS_MACOS
    return QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
        QStringLiteral("../Frameworks/libonnxruntime.1.20.1.dylib")).toStdString();
#else
    return hum::vocal::defaultRuntimePath();
#endif
}
}
