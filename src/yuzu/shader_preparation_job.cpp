// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QSettings>
#include "common/fs/path_util.h"
#include "common/settings.h"
#include "common/scm_rev.h"
#include "yuzu/shader_preparation_job.h"

namespace ShaderPreparationUi {
namespace {
QString Path(Common::FS::EdenPath path) {
    return QString::fromStdString(Common::FS::PathToUTF8String(Common::FS::GetEdenPath(path)));
}
}

QString WorkerPath() {
#ifdef _WIN32
    return QCoreApplication::applicationDirPath() + QStringLiteral("/lichii-tgm-shader-prepare.exe");
#else
    return QCoreApplication::applicationDirPath() + QStringLiteral("/lichii-tgm-shader-prepare");
#endif
}

QJsonObject MakeJob(u64 title_id, const QString& game_path, const QString& job_directory) {
    QJsonObject values;
    for (const auto& [label, setting] : Settings::values.linkage.by_key) {
        values.insert(QString::fromStdString(label), QString::fromStdString(setting->ToStringGlobal()));
    }
    // Read overrides without constructing QtConfig, which writes settings when it opens them.
    const QString custom = Path(Common::FS::EdenPath::ConfigDir) + QStringLiteral("/custom/") +
                           QStringLiteral("%1").arg(title_id, 16, 16, QLatin1Char('0')).toUpper() +
                           QStringLiteral(".ini");
    QSettings ini(custom, QSettings::IniFormat);
    for (const auto& key : ini.allKeys()) {
        if (!key.endsWith(QStringLiteral("/use_global")) || ini.value(key, true).toBool()) {
            continue;
        }
        const QString prefix = key.left(key.size() - QStringLiteral("/use_global").size());
        const auto label = prefix.section(QLatin1Char('/'), -1).toStdString();
        const auto found = Settings::values.linkage.by_key.find(label);
        if (found == Settings::values.linkage.by_key.end() || !found->second->Switchable()) {
            continue;
        }
        values[QString::fromStdString(label)] = ini.value(prefix + QStringLiteral("/default"), true).toBool()
            ? QString::fromStdString(found->second->DefaultToString())
            : ini.value(prefix).toString();
    }
    QJsonObject paths;
    for (const auto path : {Common::FS::EdenPath::EdenDir, Common::FS::EdenPath::CacheDir,
                           Common::FS::EdenPath::ConfigDir, Common::FS::EdenPath::KeysDir,
                           Common::FS::EdenPath::NANDDir, Common::FS::EdenPath::SDMCDir,
                           Common::FS::EdenPath::LoadDir, Common::FS::EdenPath::ShaderDir,
                           Common::FS::EdenPath::DumpDir, Common::FS::EdenPath::StreamlineDir,
                           Common::FS::EdenPath::FidelityFxDir}) {
        paths[QString::number(static_cast<int>(path))] = Path(path);
    }
    QJsonArray disabled;
    QJsonArray external;
    for (const auto& path : Settings::values.external_content_dirs) {
        external.append(QString::fromStdString(path));
    }
    if (const auto it = Settings::values.disabled_addons.find(title_id);
        it != Settings::values.disabled_addons.end()) {
        for (const auto& name : it->second) disabled.append(QString::fromStdString(name));
    }
    return {{QStringLiteral("format"), 1}, {QStringLiteral("title_id"), QString::number(title_id, 16)},
            {QStringLiteral("expected_source"), QString::fromUtf8(Common::g_scm_rev)},
            {QStringLiteral("game_path"), game_path}, {QStringLiteral("job_directory"), job_directory},
            {QStringLiteral("settings"), values}, {QStringLiteral("paths"), paths},
            {QStringLiteral("external_content_dirs"), external},
            {QStringLiteral("disabled_addons"), disabled}, {QStringLiteral("workers"), 4}};
}

QString ResultText(const QJsonObject& result) {
    const auto number = [&result](const char* key) { return result.value(QLatin1String(key)).toVariant().toULongLong(); };
    QString text = QCoreApplication::translate("ShaderWarmDialog",
        "Files scanned: %L1\nUnique shader programs: %L2\nShader combinations: %L3\n"
        "Pipeline records: %L4\nPrepared now: %L5\nPreviously prepared: %L6\n"
        "Failed: %L7\nUnsupported on this GPU: %L8\nQuarantined: %L9\nNot reached: %L10")
        .arg(number("files_scanned")).arg(number("programs")).arg(number("combinations"))
        .arg(number("total")).arg(number("built")).arg(number("already_prepared"))
        .arg(number("failed")).arg(number("incompatible")).arg(number("quarantined"))
        .arg(number("unreached"));
    text += QCoreApplication::translate("ShaderWarmDialog",
        "\nArchive/read errors: %L1\nFiles limited by scan budgets: %L2\n"
        "Recipe programs missing: %L3\nPrograms that could not be translated: %L4\n"
        "Recipe records omitted: %L5")
        .arg(number("scan_errors")).arg(number("limited_files"))
        .arg(number("recipe_programs_missing")).arg(number("recipe_programs_failed"))
        .arg(number("recipe_records_dropped"));
    for (const auto& warning : result.value(QStringLiteral("warnings")).toArray()) {
        text += QStringLiteral("\n\n") + warning.toString();
    }
    if (!result.value(QStringLiteral("error")).toString().isEmpty()) text += QStringLiteral("\n\n") + result.value(QStringLiteral("error")).toString();
    return text;
}
} // namespace ShaderPreparationUi
