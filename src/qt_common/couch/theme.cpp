// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <QColor>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>
#include <QUuid>
#include "qt_common/couch/theme.h"

namespace QtCommon::Couch {
namespace {
bool Fail(QString* error, const QString& message) {
    if (error)
        *error = message;
    return false;
}
QString SafeAsset(const QString& root, const QString& path) {
    if (path.isEmpty() || QDir::isAbsolutePath(path) || path.contains(QLatin1Char(':')) ||
        path.contains(QLatin1Char('\\')) || QDir::cleanPath(path) != path ||
        path.split(QLatin1Char('/')).contains(QStringLiteral("..")))
        return {};
    const QString canonical_root = QFileInfo(root).canonicalFilePath();
    const QFileInfo file(QDir(root).filePath(path));
    const QString canonical_file = file.canonicalFilePath();
    if (canonical_root.isEmpty() || !file.isFile() || file.isSymLink() ||
        !canonical_file.startsWith(canonical_root + QLatin1Char('/')) ||
        file.size() > 16 * 1024 * 1024)
        return {};
    const QString suffix = file.suffix().toLower();
    if (!QStringList{QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                     QStringLiteral("webp"), QStringLiteral("svg"), QStringLiteral("ttf"),
                     QStringLiteral("otf")}
             .contains(suffix))
        return {};
    return canonical_file;
}
} // namespace

Theme::Theme(QString directory_, QObject* parent)
    : QObject(parent), directory(std::move(directory_)), data(Defaults()) {}

QVariantMap Theme::Defaults(bool light) {
    QVariantMap colours{
        {QStringLiteral("background"),
         light ? QStringLiteral("#ededed") : QStringLiteral("#29292c")},
        {QStringLiteral("surface"), light ? QStringLiteral("#fafafa") : QStringLiteral("#37373b")},
        {QStringLiteral("text"), light ? QStringLiteral("#303033") : QStringLiteral("#f8f8fa")},
        {QStringLiteral("muted"), light ? QStringLiteral("#66666b") : QStringLiteral("#bcbcc3")},
        {QStringLiteral("line"), light ? QStringLiteral("#d2d2d5") : QStringLiteral("#525258")},
        {QStringLiteral("focus"), QStringLiteral("#16b8d5")},
        {QStringLiteral("accent"), QStringLiteral("#e84b4f")},
        {QStringLiteral("onPrimary"),
         light ? QStringLiteral("#ffffff") : QStringLiteral("#29292c")},
        {QStringLiteral("scrim"), QStringLiteral("#99000000")}};
    QVariantMap metrics{
        {QStringLiteral("margin"), 80},           {QStringLiteral("top"), 48},
        {QStringLiteral("headerHeight"), 96},     {QStringLiteral("groupHeight"), 56},
        {QStringLiteral("settingsHeading"), 68},  {QStringLiteral("sidebarWidth"), 380},
        {QStringLiteral("columnGap"), 40},        {QStringLiteral("rowHeight"), 86},
        {QStringLiteral("mappingRowHeight"), 76}, {QStringLiteral("sectionHeight"), 60},
        {QStringLiteral("helpHeight"), 64},       {QStringLiteral("footerHeight"), 64},
        {QStringLiteral("dockHeight"), 112},      {QStringLiteral("radius"), 6},
        {QStringLiteral("focusWidth"), 4},        {QStringLiteral("coverRadius"), 14},
        {QStringLiteral("fontSize"), 26},         {QStringLiteral("valueSize"), 22},
        {QStringLiteral("smallSize"), 18},        {QStringLiteral("titleSize"), 40},
        {QStringLiteral("motionMs"), 100}};
    return {{QStringLiteral("version"), 1},
            {QStringLiteral("id"),
             light ? QStringLiteral("horizon-light") : QStringLiteral("horizon-dark")},
            {QStringLiteral("name"), light ? tr("Horizon light") : tr("Horizon dark")},
            {QStringLiteral("author"), QStringLiteral("Lichii-TGM")},
            {QStringLiteral("fontFamily"), QStringLiteral("Segoe UI")},
            {QStringLiteral("colours"), colours},
            {QStringLiteral("metrics"), metrics},
            {QStringLiteral("assets"), QVariantMap{}},
            {QStringLiteral("layers"), QVariantList{}},
            {QStringLiteral("views"), QVariantMap{}}};
}

bool Theme::Validate(const QByteArray& input, const QString& root, QVariantMap* output,
                     QString* error) {
    if (input.size() > 1024 * 1024)
        return Fail(error, tr("Theme manifests must be smaller than 1 MiB."));
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(input, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject())
        return Fail(error, tr("The theme manifest is not valid JSON."));
    const auto source = document.object().toVariantMap();
    if (source.value(QStringLiteral("version")).toInt() != 1)
        return Fail(error, tr("This theme format is not supported."));
    const QString id = source.value(QStringLiteral("id")).toString();
    if (!QRegularExpression(QStringLiteral("^[a-z0-9][a-z0-9-]{0,63}$")).match(id).hasMatch() ||
        id == QStringLiteral("horizon-dark") || id == QStringLiteral("horizon-light"))
        return Fail(error,
                    tr("Choose a unique theme id using lowercase letters, numbers and hyphens."));
    QVariantMap result =
        Defaults(source.value(QStringLiteral("base")).toString() == QStringLiteral("light"));
    for (const auto& key : {QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("author"),
                            QStringLiteral("fontFamily")}) {
        if (source.contains(key)) {
            const QString text = source.value(key).toString();
            if (text.size() > 256 || (key == QStringLiteral("name") && text.trimmed().isEmpty()))
                return Fail(error, tr("A theme name or font is invalid."));
            result[key] = text;
        }
    }
    auto colours = result.value(QStringLiteral("colours")).toMap();
    const auto supplied_colours = source.value(QStringLiteral("colours")).toMap();
    for (auto it = supplied_colours.cbegin(); it != supplied_colours.cend(); ++it) {
        if (!colours.contains(it.key()) || !QColor(it.value().toString()).isValid())
            return Fail(error, tr("The theme contains an invalid colour."));
        colours[it.key()] = it.value();
    }
    result[QStringLiteral("colours")] = colours;
    auto metrics = result.value(QStringLiteral("metrics")).toMap();
    const auto supplied_metrics = source.value(QStringLiteral("metrics")).toMap();
    for (auto it = supplied_metrics.cbegin(); it != supplied_metrics.cend(); ++it) {
        bool ok{};
        const double number = it.value().toDouble(&ok);
        if (!metrics.contains(it.key()) || !ok || !std::isfinite(number) || number < 0 ||
            number > 500)
            return Fail(error, tr("A theme dimension is invalid."));
        metrics[it.key()] = number;
    }
    // These bounds keep navigation and input accessible even in a heavily customised theme.
    for (const auto& key : {QStringLiteral("rowHeight"), QStringLiteral("mappingRowHeight"),
                            QStringLiteral("sectionHeight")})
        if (metrics[key].toDouble() < 48 || metrics[key].toDouble() > 180)
            return Fail(error, tr("Theme rows must be between 48 and 180 design pixels."));
    if (metrics[QStringLiteral("fontSize")].toDouble() < 20 ||
        metrics[QStringLiteral("smallSize")].toDouble() < 16 ||
        metrics[QStringLiteral("sidebarWidth")].toDouble() < 240 ||
        metrics[QStringLiteral("sidebarWidth")].toDouble() > 480)
        return Fail(error, tr("The theme's text or settings sidebar is too small."));
    if (metrics[QStringLiteral("columnGap")].toDouble() < 12 ||
        metrics[QStringLiteral("columnGap")].toDouble() > 80 ||
        metrics[QStringLiteral("focusWidth")].toDouble() < 2 ||
        metrics[QStringLiteral("focusWidth")].toDouble() > 10 ||
        metrics[QStringLiteral("fontSize")].toDouble() > 40 ||
        metrics[QStringLiteral("valueSize")].toDouble() < 18 ||
        metrics[QStringLiteral("valueSize")].toDouble() > 40 ||
        metrics[QStringLiteral("smallSize")].toDouble() > 32 ||
        metrics[QStringLiteral("titleSize")].toDouble() < 28 ||
        metrics[QStringLiteral("titleSize")].toDouble() > 64)
        return Fail(
            error,
            tr("Theme spacing, focus outlines or text sizes are outside the supported range."));
    result[QStringLiteral("metrics")] = metrics;
    QVariantMap assets;
    const auto supplied_assets = source.value(QStringLiteral("assets")).toMap();
    if (supplied_assets.size() > 128)
        return Fail(error, tr("The theme has too many assets."));
    for (auto it = supplied_assets.cbegin(); it != supplied_assets.cend(); ++it) {
        const QString path = SafeAsset(root, it.value().toString());
        if (path.isEmpty())
            return Fail(error,
                        tr("Theme assets must be local images or fonts inside the theme folder."));
        assets[it.key()] = QUrl::fromLocalFile(path).toString();
    }
    result[QStringLiteral("assets")] = assets;
    const auto layers = source.value(QStringLiteral("layers")).toList();
    if (layers.size() > 64)
        return Fail(error, tr("The theme has too many visual layers."));
    for (const auto& value : layers) {
        const auto layer = value.toMap();
        if (!QStringList{QStringLiteral("image"), QStringLiteral("text"),
                         QStringLiteral("rectangle")}
                 .contains(layer.value(QStringLiteral("type")).toString()))
            return Fail(error, tr("Unknown theme layer type."));
        for (const auto& key : {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("width"),
                                QStringLiteral("height")}) {
            bool ok{};
            const double number = layer.value(key).toDouble(&ok);
            if (!ok || !std::isfinite(number) || number < -1920 || number > 3840)
                return Fail(error, tr("A theme layer has invalid coordinates."));
        }
        if (layer.contains(QStringLiteral("colour")) &&
            !QColor(layer.value(QStringLiteral("colour")).toString()).isValid())
            return Fail(error, tr("A theme layer has an invalid colour."));
        if (layer.contains(QStringLiteral("opacity"))) {
            bool ok{};
            const double opacity = layer.value(QStringLiteral("opacity")).toDouble(&ok);
            if (!ok || !std::isfinite(opacity) || opacity < 0 || opacity > 1)
                return Fail(error, tr("A theme layer has an invalid opacity."));
        }
        if (layer.value(QStringLiteral("width")).toDouble() < 0 ||
            layer.value(QStringLiteral("height")).toDouble() < 0)
            return Fail(error, tr("Theme layer sizes cannot be negative."));
        if (layer.value(QStringLiteral("text")).toString().size() > 1024 ||
            (!layer.value(QStringLiteral("asset")).toString().isEmpty() &&
             !assets.contains(layer.value(QStringLiteral("asset")).toString())))
            return Fail(error, tr("A theme layer refers to an unknown asset or overly long text."));
    }
    result[QStringLiteral("layers")] = layers;
    // View rectangles describe stable slots; they never replace controls or executable QML.
    const auto views = source.value(QStringLiteral("views")).toMap();
    QVariantMap accepted_views;
    for (auto it = views.cbegin(); it != views.cend(); ++it) {
        if (!QStringList{QStringLiteral("home"), QStringLiteral("library"),
                         QStringLiteral("collections"), QStringLiteral("themes"),
                         QStringLiteral("settings")}
                 .contains(it.key()))
            return Fail(error, tr("Unknown theme view."));
        QVariantMap layout_slots;
        const auto supplied_slots = it.value().toMap();
        for (auto slot = supplied_slots.cbegin(); slot != supplied_slots.cend(); ++slot) {
            if (!QStringList{QStringLiteral("hero"), QStringLiteral("games"),
                             QStringLiteral("dock"), QStringLiteral("background")}
                     .contains(slot.key()))
                return Fail(error, tr("Unknown theme layout slot."));
            const auto rect = slot.value().toMap();
            for (const auto& key : {QStringLiteral("x"), QStringLiteral("y"),
                                    QStringLiteral("width"), QStringLiteral("height")}) {
                bool ok{};
                const double number = rect.value(key).toDouble(&ok);
                if (!ok || !std::isfinite(number) || number < 0 ||
                    number > (key == QStringLiteral("y") || key == QStringLiteral("height") ? 1080
                                                                                            : 1920))
                    return Fail(error, tr("A theme layout slot is outside the design canvas."));
            }
            if (rect.value(QStringLiteral("x")).toDouble() +
                        rect.value(QStringLiteral("width")).toDouble() >
                    1920 ||
                rect.value(QStringLiteral("y")).toDouble() +
                        rect.value(QStringLiteral("height")).toDouble() >
                    1080)
                return Fail(error, tr("A theme layout slot extends beyond the design canvas."));
            layout_slots[slot.key()] = rect;
        }
        accepted_views[it.key()] = layout_slots;
    }
    result[QStringLiteral("views")] = accepted_views;
    if (output)
        *output = std::move(result);
    return true;
}

QVariantList Theme::Installed() const {
    QVariantList result{Defaults(), Defaults(true)};
    const QDir root(directory);
    for (const auto& id : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QString path = root.filePath(id);
        QFile file(QDir(path).filePath(QStringLiteral("theme.json")));
        QVariantMap value;
        if (file.open(QIODevice::ReadOnly) && Validate(file.read(1024 * 1024 + 1), path, &value))
            result.push_back(value);
    }
    return result;
}
bool Theme::Load(const QString& id, QString* error) {
    if (id == QStringLiteral("horizon-dark") || id == QStringLiteral("horizon-light") ||
        id.isEmpty()) {
        data = Defaults(id == QStringLiteral("horizon-light"));
        current_root.clear();
        emit changed();
        return true;
    }
    if (!QRegularExpression(QStringLiteral("^[a-z0-9][a-z0-9-]{0,63}$")).match(id).hasMatch())
        return Fail(error, tr("Invalid theme id."));
    const QString root = QDir(directory).filePath(id);
    if (QFileInfo(root).isSymLink())
        return Fail(error, tr("Theme folders cannot be symbolic links."));
    QFile file(QDir(root).filePath(QStringLiteral("theme.json")));
    QVariantMap next;
    if (!file.open(QIODevice::ReadOnly) ||
        !Validate(file.read(1024 * 1024 + 1), root, &next, error))
        return false;
    data = std::move(next);
    current_root = root;
    emit changed();
    return true;
}
QString Theme::Asset(const QString& name) const {
    return data.value(QStringLiteral("assets")).toMap().value(name).toString();
}

bool Theme::Import(const QString& manifest, QString* installed_id, QString* error) {
    const QFileInfo selected(manifest);
    const QString root = selected.canonicalPath();
    QFile file(manifest);
    QVariantMap validated;
    if (!file.open(QIODevice::ReadOnly))
        return Fail(error, tr("The theme manifest could not be opened."));
    const QByteArray input = file.read(1024 * 1024 + 1);
    if (!Validate(input, root, &validated, error))
        return false;
    const QString id = validated.value(QStringLiteral("id")).toString();
    if (!QDir().mkpath(directory))
        return Fail(error, tr("The themes folder could not be created."));
    const QString destination = QDir(directory).filePath(id);
    if (QFileInfo::exists(destination))
        return Fail(
            error,
            tr("A theme with this id is already installed. Use a new id for a new version."));
    const QString staging = QDir(directory).filePath(QStringLiteral(".import-") +
                                                     QUuid::createUuid().toString(QUuid::Id128));
    if (!QDir().mkpath(staging))
        return Fail(error, tr("The theme could not be staged."));
    // Only referenced, validated assets are copied; themes cannot install programs or external
    // files.
    const auto source = QJsonDocument::fromJson(input).object().toVariantMap();
    const auto assets = source.value(QStringLiteral("assets")).toMap();
    qint64 bytes = 0;
    bool success = true;
    for (auto it = assets.cbegin(); it != assets.cend(); ++it) {
        const QString path = SafeAsset(root, it.value().toString());
        const QString target = QDir(staging).filePath(QDir::cleanPath(it.value().toString()));
        bytes += QFileInfo(path).size();
        if (path.isEmpty() || bytes > 100 * 1024 * 1024 ||
            !QDir().mkpath(QFileInfo(target).path()) ||
            (!QFileInfo::exists(target) && !QFile::copy(path, target))) {
            success = false;
            break;
        }
    }
    QSaveFile output(QDir(staging).filePath(QStringLiteral("theme.json")));
    success = success && output.open(QIODevice::WriteOnly) && output.write(input) == input.size() &&
              output.commit();
    if (success)
        success = QDir(directory).rename(QFileInfo(staging).fileName(), id);
    if (!success) {
        QDir(staging).removeRecursively();
        return Fail(error, tr("The theme could not be installed. No existing theme was replaced."));
    }
    if (installed_id)
        *installed_id = id;
    emit changed();
    return true;
}
} // namespace QtCommon::Couch
