// SPDX-License-Identifier: GPL-3.0-or-later
#include "yuzu/couch/couch_mode.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <QAbstractItemModel>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLayout>
#include <QMenu>
#include <QMutex>
#include <QMutexLocker>
#include <QPushButton>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QQuickWindow>
#include <QResizeEvent>
#include <QSaveFile>
#include <QScreen>
#include <QStandardItemModel>
#include <QTextBoundaryFinder>
#include <QUuid>
#include <SDL3/SDL.h>

#include "common/logging.h"
#include "common/scm_rev.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/couch/native_dialog.h"
#include "qt_common/game_list/game_list_p.h"

static void InitializeCouchResources() {
    Q_INIT_RESOURCE(couch);
}

namespace {
class CouchImages final : public QQuickImageProvider {
public:
    CouchImages() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString& id, QSize* size, const QSize& requested) override {
        QMutexLocker lock(&mutex);
        QImage image = id.startsWith(QStringLiteral("profile")) ? profile
                       : id.startsWith(QStringLiteral("controller"))
                           ? controller
                           : games.value(id.section(QLatin1Char('?'), 0, 0));
        if (size)
            *size = image.size();
        if (requested.isValid() && !image.isNull())
            image = image.scaled(requested, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        return image;
    }
    QMutex mutex;
    QImage controller;
    QImage profile;
    QHash<QString, QImage> games;
};
QStringList MainTabs() {
    return {QStringLiteral("home"), QStringLiteral("library"), QStringLiteral("collections"),
            QStringLiteral("themes"), QStringLiteral("settings")};
}
} // namespace

CouchMode::CouchMode(QStandardItemModel* library_, QString directory, SourceFactory source_factory,
                     QWidget* parent)
    : QQuickWidget(parent), library(library_), config_directory(std::move(directory)),
      factory(std::move(source_factory)), settings(this),
      theme(QDir(config_directory).filePath(QStringLiteral("couch-themes")), this) {
    InitializeCouchResources();
    setObjectName(QStringLiteral("CouchMode"));
    setProperty("couchPresentation", true);
    setResizeMode(QQuickWidget::SizeRootObjectToView);
    setFocusPolicy(Qt::StrongFocus);
    setClearColor(QColor(QStringLiteral("#29292c")));
    ui = {{QStringLiteral("tab"), QStringLiteral("home")},
          {QStringLiteral("selected"), QString{}},
          {QStringLiteral("group"), QStringLiteral("Application")},
          {QStringLiteral("section"), QStringLiteral("general")},
          {QStringLiteral("player"), 0},
          {QStringLiteral("scope"), QStringLiteral("global")},
          {QStringLiteral("query"), QString{}},
          {QStringLiteral("libraryQuery"), QString{}},
          {QStringLiteral("collection"), QString{}},
          {QStringLiteral("toast"), QString{}},
          {QStringLiteral("controllerImage"), QString{}},
          {QStringLiteral("controllerName"), tr("No controller detected")},
          {QStringLiteral("mapping"), false},
          {QStringLiteral("reducedMotion"), false},
          {QStringLiteral("textScale"), 1.0},
          {QStringLiteral("prompts"), QStringLiteral("xbox")},
          {QStringLiteral("version"),
           QString::fromUtf8(*Common::g_build_release_tag ? Common::g_build_release_tag
                                                          : Common::g_build_version)}};
    images = new CouchImages;
    engine()->addImageProvider(QStringLiteral("couch"), images);
    rootContext()->setContextProperty(QStringLiteral("Couch"), this);
    connect(&settings, &QtCommon::Couch::WidgetModel::changed, this, [this] {
        PreviewPreferences();
        Notify();
    });
    connect(&theme, &QtCommon::Couch::Theme::changed, this, [this] {
        const auto assets = theme.Data().value(QStringLiteral("assets")).toMap();
        const QString font = assets.value(QStringLiteral("font")).toString();
        if (!font.isEmpty())
            QFontDatabase::addApplicationFont(QUrl(font).toLocalFile());
        Notify();
    });
    model_timer.setSingleShot(true);
    model_timer.setInterval(80);
    connect(&model_timer, &QTimer::timeout, this, &CouchMode::RebuildGames);
    if (library) {
        connect(library, &QAbstractItemModel::rowsInserted, this, [this] { model_timer.start(); });
        connect(library, &QAbstractItemModel::rowsRemoved, this, [this] { model_timer.start(); });
        connect(library, &QAbstractItemModel::modelReset, this, [this] { model_timer.start(); });
        connect(library, &QAbstractItemModel::dataChanged, this, [this] { model_timer.start(); });
    }
    clock.start();
    input_timer.setInterval(16);
    connect(&input_timer, &QTimer::timeout, this, &CouchMode::PollController);
    LoadCollections();
    ReloadPreferences();
    RebuildGames();
    setSource(QUrl(QStringLiteral("qrc:/couch/Horizon.qml")));
}
CouchMode::~CouchMode() {
    setSource(QUrl{});
    SetActive(false);
    ReleaseSettings(true);
}

void CouchMode::SetActive(bool enabled) {
    if (active == enabled)
        return;
    active = enabled;
    emit NavigationOwnerChanged(enabled);
    if (active) {
        previous_native_dialogs = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        qApp->installEventFilter(this);
        // SDL keeps its own subsystem reference; existing emulator devices remain owned by
        // input_common.
        sdl_owned = SDL_InitSubSystem(SDL_INIT_GAMEPAD);
        input_timer.start();
        setFocus();
    } else {
        input_timer.stop();
        qApp->removeEventFilter(this);
        for (auto* pad : pads)
            SDL_CloseGamepad(pad);
        pads.clear();
        pad_state.clear();
        navigation_pad = 0;
        held.clear();
        held_since.clear();
        repeated_at.clear();
        if (sdl_owned)
            SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
        sdl_owned = false;
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, previous_native_dialogs);
    }
}

QVariantMap CouchMode::State() const {
    auto state = ui;
    state[QStringLiteral("modal")] = modals.empty() ? QVariantMap{} : modals.back().data;
    state[QStringLiteral("dirty")] = Dirty();
    state[QStringLiteral("changeCount")] =
        settings.Changes().size() + ((source.external_dirty && source.external_dirty()) ? 1 : 0);
    state[QStringLiteral("externalChanges")] = source.external_dirty && source.external_dirty();
    state[QStringLiteral("gameRunning")] = game_running;
    state[QStringLiteral("game")] = SelectedGame();
    return state;
}
bool CouchMode::Dirty() const {
    return settings.Dirty() || (source.external_dirty && source.external_dirty());
}
QString CouchMode::clipboardText() const {
    return QApplication::clipboard()->text();
}
void CouchMode::SetProfile(const QString& name, const QImage& avatar) {
    auto* provider = static_cast<CouchImages*>(images);
    {
        QMutexLocker lock(&provider->mutex);
        provider->profile = avatar;
    }
    ui[QStringLiteral("profileName")] = name.isEmpty() ? tr("Profile") : name;
    ui[QStringLiteral("profileImage")] =
        avatar.isNull() ? QString{}
                        : QString(QStringLiteral("image://couch/profile?revision=") +
                                  QString::number(++image_serial));
    Notify();
}
void CouchMode::ResetSourceAfterOperation(std::function<void()> operation) {
    ReleaseSettings(true);
    operation();
    ReloadPreferences();
    EnsureSettings();
    emit ConfigurationSaved();
}
void CouchMode::SetGameRunning(bool running) {
    game_running = running;
    Notify();
}
void CouchMode::closeEvent(QCloseEvent* event) {
    event->ignore();
    emit ExitRequested();
}
void CouchMode::PreviewPreferences() {
    if (!source.dialog)
        return;
    if (auto* choice = source.dialog->findChild<QComboBox*>(QStringLiteral("couchTheme"))) {
        const QString id = choice->currentData().toString();
        if (theme.Data().value(QStringLiteral("id")).toString() != id)
            theme.Load(id);
    }
    if (auto* widget = source.dialog->findChild<QWidget*>(QStringLiteral("couchTextScale")))
        ui[QStringLiteral("textScale")] = widget->property("value").toDouble() / 100.0;
    if (auto* widget =
            source.dialog->findChild<QAbstractButton*>(QStringLiteral("couchReducedMotion")))
        ui[QStringLiteral("reducedMotion")] = widget->isChecked();
    if (auto* widget = source.dialog->findChild<QComboBox*>(QStringLiteral("couchPrompts")))
        ui[QStringLiteral("promptPreference")] = widget->currentIndex();
}
void CouchMode::Notify() {
    emit stateChanged();
}
void CouchMode::Toast(const QString& text) {
    ui[QStringLiteral("toast")] = text;
    Notify();
    QTimer::singleShot(3500, this, [this, text] {
        if (ui.value(QStringLiteral("toast")).toString() == text) {
            ui[QStringLiteral("toast")] = QString{};
            Notify();
        }
    });
}
void CouchMode::ReloadPreferences() {
    engine()->retranslate();
    QString error;
    if (!theme.Load(QString::fromStdString(UISettings::values.couch_theme.GetValue()), &error)) {
        theme.Load(QStringLiteral("horizon-dark"));
        LOG_WARNING(Frontend, "Couch theme could not be loaded: {}", error.toStdString());
    }
    ui[QStringLiteral("reducedMotion")] = UISettings::values.couch_reduced_motion.GetValue();
    ui[QStringLiteral("textScale")] = UISettings::values.couch_text_scale.GetValue() / 100.0;
    const int family = UISettings::values.couch_prompts.GetValue();
    ui[QStringLiteral("prompts")] = family == 2   ? QStringLiteral("nintendo")
                                    : family == 3 ? QStringLiteral("playstation")
                                                  : QStringLiteral("xbox");
    ui[QStringLiteral("promptPreference")] = family;
    Notify();
}

void CouchMode::RebuildGames() {
    if (!library)
        return;
    QVariantList next;
    QHash<QString, CoverCache> next_cache;
    QHash<QString, QImage> art;
    QSet<QString> seen;
    std::function<void(const QModelIndex&)> visit = [&](const QModelIndex& root) {
        for (int row = 0; row < library->rowCount(root); ++row) {
            const auto index = library->index(row, 0, root);
            if (index.data(GameListItem::TypeRole).value<GameListItemType>() ==
                GameListItemType::Game) {
                const QString path = index.data(GameListItemPath::FullPathRole).toString();
                const auto id = index.data(GameListItemPath::ProgramIdRole).toULongLong();
                const QString identity = id ? QString::number(id, 16) : path;
                if (seen.contains(identity))
                    continue;
                seen.insert(identity);
                QString name = index.data(GameListItemPath::TitleRole).toString();
                if (name.isEmpty())
                    name = QFileInfo(path).completeBaseName();
                const QByteArray encoded =
                    index.data(GameListItemPath::CoverDataRole).toByteArray();
                CoverCache cached = cover_cache.value(identity);
                if (cached.encoded != encoded || !cover_cache.contains(identity)) {
                    cached.encoded = encoded;
                    cached.image =
                        encoded.isEmpty()
                            ? QImage{}
                            : QImage::fromData(encoded).scaled(768, 768, Qt::KeepAspectRatio,
                                                               Qt::SmoothTransformation);
                    cached.revision = QString::number(++image_serial);
                }
                const QImage image = cached.image;
                next_cache.insert(identity, cached);
                const QString key = QStringLiteral("game/") + QString::number(qHash(identity), 16);
                art.insert(key, image);
                next.push_back(QVariantMap{
                    {QStringLiteral("id"), identity},
                    {QStringLiteral("programId"), QVariant::fromValue<qulonglong>(id)},
                    {QStringLiteral("name"), name},
                    {QStringLiteral("path"), path},
                    {QStringLiteral("image"),
                     image.isNull() ? QString{}
                                    : QString(QStringLiteral("image://couch/") + key +
                                              QStringLiteral("?revision=") + cached.revision)},
                    {QStringLiteral("hasArt"), !encoded.isEmpty()},
                    {QStringLiteral("favorite"), UISettings::values.favorited_ids.contains(id)}});
            }
            if (library->hasChildren(index))
                visit(index);
        }
    };
    visit({});
    {
        auto* provider = static_cast<CouchImages*>(images);
        QMutexLocker lock(&provider->mutex);
        provider->games = std::move(art);
    }
    cover_cache = std::move(next_cache);
    games = std::move(next);
    bool selected = false;
    for (const auto& item : games)
        selected |=
            item.toMap().value(QStringLiteral("id")) == ui.value(QStringLiteral("selected"));
    if (!selected)
        ui[QStringLiteral("selected")] =
            games.isEmpty() ? QString{} : games.front().toMap().value(QStringLiteral("id"));
    ++image_serial;
    emit gamesChanged();
    Notify();
}
QVariantMap CouchMode::SelectedGame() const {
    for (const auto& game : games)
        if (game.toMap().value(QStringLiteral("id")) == ui.value(QStringLiteral("selected")))
            return game.toMap();
    return {};
}
QString CouchMode::SelectedPath() const {
    return SelectedGame().value(QStringLiteral("path")).toString();
}
void CouchMode::refreshLibrary() {
    RebuildGames();
}

void CouchMode::EnsureSettings() {
    if (source.dialog)
        return;
    source = factory(ui.value(QStringLiteral("scope")).toString(),
                     ui.value(QStringLiteral("scopePath")).toString());
    if (!source.dialog) {
        Toast(tr("Settings could not be opened."));
        return;
    }
    source.dialog->setProperty("couchSource", true);
    settings.SetActivationGate(
        [this](std::function<void()> action) { pending_activation = std::move(action); });
    settings.SetPages(source.pages);
    SelectSection(ui.value(QStringLiteral("section")).toString());
}
void CouchMode::ReleaseSettings(bool revert) {
    if (source.mapping && source.mapping() && source.mapping_key)
        source.mapping_key(Qt::Key_Escape);
    pending_activation = {};
    pending_binding = -1;
    settings.Clear();
    if (source.dialog && revert && source.discard)
        source.discard();
    auto cleanup = std::move(source.cleanup);
    source = {};
    if (cleanup)
        cleanup();
}
void CouchMode::SelectSection(const QString& id) {
    QString found = id;
    const auto sections = settings.Sections();
    bool valid = false;
    for (const auto& page : sections)
        if (page.toMap().value(QStringLiteral("id")).toString() == found) {
            valid = true;
            ui[QStringLiteral("group")] = page.toMap().value(QStringLiteral("group"));
            break;
        }
    if (!valid && !sections.isEmpty()) {
        found = sections.front().toMap().value(QStringLiteral("id")).toString();
        ui[QStringLiteral("group")] = sections.front().toMap().value(QStringLiteral("group"));
    }
    ui[QStringLiteral("section")] = found;
    ui[QStringLiteral("query")] = QString{};
    settings.Select(found, ui.value(QStringLiteral("player")).toInt());
    if (source.select_page)
        source.select_page(found);
    Notify();
}
void CouchMode::setSearch(const QString& text) {
    EnsureSettings();
    ui[QStringLiteral("query")] = text.left(160);
    settings.Select(ui.value(QStringLiteral("section")).toString(),
                    ui.value(QStringLiteral("player")).toInt(), text.left(160));
    Notify();
}
bool CouchMode::stage(int id, const QVariant& value) {
    QString error;
    const bool result = settings.Stage(id, value, &error);
    if (!result)
        Toast(error.isEmpty() ? tr("This setting cannot be changed right now.") : error);
    Notify();
    return result;
}
void CouchMode::reset(int id, bool inherit) {
    settings.Reset(id, inherit);
    Notify();
}
void CouchMode::edit(int id) {
    const auto row = settings.Describe(id);
    if (row.isEmpty())
        return;
    if (row.value(QStringLiteral("kind")).toString() == QStringLiteral("binding")) {
        pending_binding = id;
        Toast(tr("Release the controller buttons, then press the input to bind."));
    } else if (row.value(QStringLiteral("kind")).toString() == QStringLiteral("action"))
        settings.Activate(id);
    else {
        ui[QStringLiteral("editing")] = id;
        Notify();
    }
}
bool CouchMode::SaveSettings() {
    if (!source.dialog)
        return true;
    if (source.apply && !source.apply()) {
        Toast(tr("The settings could not be saved. Check the highlighted values."));
        return false;
    }
    settings.MarkSaved();
    emit ConfigurationSaved();
    ReloadPreferences();
    return true;
}
void CouchMode::save() {
    if (SaveSettings())
        Toast(tr("Settings saved."));
}
void CouchMode::discard() {
    ReleaseSettings(true);
    ReloadPreferences();
    EnsureSettings();
    Notify();
}
void CouchMode::Guard(std::function<void()> continuation) {
    if (Dirty()) {
        pending = std::move(continuation);
        ShowModal({{QStringLiteral("kind"), QStringLiteral("unsaved")},
                   {QStringLiteral("title"), tr("Save your settings?")}});
    } else
        continuation();
}
void CouchMode::ChangeTab(const QString& tab) {
    if (!MainTabs().contains(tab) || ui.value(QStringLiteral("tab")).toString() == tab)
        return;
    Guard([this, tab] {
        if (tab != QStringLiteral("settings") && tab != QStringLiteral("themes"))
            ReleaseSettings(false);
        ui[QStringLiteral("tab")] = tab;
        if (tab == QStringLiteral("settings"))
            EnsureSettings();
        Notify();
    });
}
bool CouchMode::RequestClose() {
    for (const auto& presentation : native_presentations)
        if (presentation && presentation->isVisible()) {
            presentation->parentWidget()->activateWindow();
            return false;
        }
    if (!Dirty())
        return true;
    Guard([this] {
        EnsureSettings();
        emit CloseApproved();
    });
    return false;
}
void CouchMode::PrepareShutdown() {
    ReleaseSettings(false);
    SetActive(false);
    hide();
}
void CouchMode::ShowModal(QVariantMap data, std::function<void(const QVariant&)> accept) {
    modals.push_back({std::move(data), std::move(accept)});
    Notify();
}
void CouchMode::closeModal() {
    if (modals.empty())
        return;
    if (modals.back().data.value(QStringLiteral("kind")).toString() == QStringLiteral("unsaved"))
        pending = {};
    const bool menu =
        modals.back().data.value(QStringLiteral("kind")).toString() == QStringLiteral("choices");
    modals.pop_back();
    Notify();
    if (menu)
        if (auto* popup = QApplication::activePopupWidget())
            popup->close();
}
void CouchMode::openKeyboard(const QString& target, const QString& text, const QString& title,
                             bool secret) {
    ShowModal({{QStringLiteral("kind"), QStringLiteral("keyboard")},
               {QStringLiteral("target"), target},
               {QStringLiteral("text"), text},
               {QStringLiteral("title"), title},
               {QStringLiteral("secret"), secret}});
}
QVariantMap CouchMode::editText(const QString& given, int position, const QString& operation,
                                const QString& inserted) const {
    QString text = given;
    position = std::clamp(position, 0, static_cast<int>(text.size()));
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
    finder.setPosition(position);
    if (operation == QStringLiteral("backspace")) {
        const int start = finder.toPreviousBoundary();
        if (start >= 0) {
            text.remove(start, position - start);
            position = start;
        }
    } else if (operation == QStringLiteral("left")) {
        const int next = finder.toPreviousBoundary();
        if (next >= 0)
            position = next;
    } else if (operation == QStringLiteral("right")) {
        const int next = finder.toNextBoundary();
        if (next >= 0)
            position = next;
    } else if (operation == QStringLiteral("insert") && text.size() + inserted.size() <= 4096) {
        text.insert(position, inserted);
        position += inserted.size();
    }
    return {{QStringLiteral("text"), text}, {QStringLiteral("caret"), position}};
}
void CouchMode::completeText(const QString& target, const QString& text) {
    closeModal();
    if (target == QStringLiteral("search"))
        setSearch(text);
    else if (target == QStringLiteral("library-search")) {
        Guard([this, text] {
            ui[QStringLiteral("libraryQuery")] = text.left(160);
            ui[QStringLiteral("tab")] = QStringLiteral("library");
            Notify();
        });
    } else if (target == QStringLiteral("new-collection")) {
        if (text.trimmed().isEmpty()) {
            Toast(tr("Enter a collection name."));
            return;
        }
        collection_data.push_back(
            QVariantMap{{QStringLiteral("id"), QUuid::createUuid().toString(QUuid::Id128)},
                        {QStringLiteral("name"), text.trimmed().left(160)},
                        {QStringLiteral("games"), QVariantList{}}});
        SaveCollections();
        emit gamesChanged();
    } else if (target.startsWith(QStringLiteral("rename-collection:"))) {
        if (text.trimmed().isEmpty()) {
            Toast(tr("Enter a collection name."));
            return;
        }
        const QString id = target.mid(18);
        for (auto& collection : collection_data) {
            auto data = collection.toMap();
            if (data.value(QStringLiteral("id")).toString() == id) {
                data[QStringLiteral("name")] = text.trimmed().left(160);
                collection = data;
            }
        }
        SaveCollections();
        emit gamesChanged();
    }
}
void CouchMode::chooseTheme(const QString& id) {
    EnsureSettings();
    if (ui.value(QStringLiteral("scope")).toString() != QStringLiteral("global")) {
        Guard([this, id] {
            ReleaseSettings(false);
            ui[QStringLiteral("scope")] = QStringLiteral("global");
            EnsureSettings();
            chooseTheme(id);
        });
        return;
    }
    auto* combo = source.dialog ? source.dialog->findChild<QComboBox*>(QStringLiteral("couchTheme"))
                                : nullptr;
    if (!combo)
        return;
    const int index = combo->findData(id);
    if (index < 0)
        return;
    combo->setCurrentIndex(index);
    QString error;
    if (!theme.Load(id, &error))
        Toast(error);
    Notify();
}

QVariantList CouchMode::Collections() const {
    QVariantList result{QVariantMap{{QStringLiteral("id"), QStringLiteral("favorites")},
                                    {QStringLiteral("name"), tr("Favorites")}}};
    result.append(collection_data);
    return result;
}
void CouchMode::LoadCollections() {
    QFile file(QDir(config_directory).filePath(QStringLiteral("couch-library.json")));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
        return;
    const auto value = QJsonDocument::fromJson(file.readAll()).object();
    for (const auto& item : value.value(QStringLiteral("collections")).toArray()) {
        auto data = item.toObject().toVariantMap();
        if (!data.value(QStringLiteral("id")).toString().isEmpty() &&
            data.value(QStringLiteral("name")).toString().size() <= 160)
            collection_data.push_back(data);
    }
}
bool CouchMode::SaveCollections() {
    QSaveFile file(QDir(config_directory).filePath(QStringLiteral("couch-library.json")));
    const QByteArray data =
        QJsonDocument(QJsonObject{{QStringLiteral("version"), 1},
                                  {QStringLiteral("collections"),
                                   QJsonArray::fromVariantList(collection_data)}})
            .toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        Toast(tr("Collections could not be saved."));
        return false;
    }
    return true;
}

void CouchMode::action(const QString& name, const QVariant& value) {
    if (name == QStringLiteral("resume")) {
        Guard([this] {
            ReleaseSettings(false);
            emit ResumeRequested();
        });
        return;
    }
    if (name == QStringLiteral("stop")) {
        Guard([this] {
            ReleaseSettings(false);
            emit StopRequested();
        });
        return;
    }
    if (name == QStringLiteral("exit")) {
        emit ExitRequested();
        return;
    }
    if (name == QStringLiteral("share")) {
        EnsureSettings();
        if (!source.dialog)
            return;
        for (auto* button : source.dialog->findChildren<QPushButton*>()) {
            if (!button->menu() || button->objectName() != QStringLiteral("couchShareSettings"))
                continue;
            auto* menu = button->menu();
            emit menu->aboutToShow();
            QVariantList choices;
            for (auto* item : menu->actions())
                if (!item->isSeparator())
                    choices.push_back(QVariantMap{
                        {QStringLiteral("label"), item->text().remove(QLatin1Char('&'))},
                        {QStringLiteral("enabled"), item->isEnabled()},
                        {QStringLiteral("value"), choices.size()}});
            ShowModal({{QStringLiteral("kind"), QStringLiteral("choices")},
                       {QStringLiteral("title"), tr("Share settings")},
                       {QStringLiteral("choices"), choices}},
                      [menu](const QVariant& selected) {
                          int index = 0;
                          for (auto* item : menu->actions())
                              if (!item->isSeparator()) {
                                  if (index++ == selected.toInt()) {
                                      item->trigger();
                                      break;
                                  }
                              }
                      });
            break;
        }
        return;
    }
    if (name == QStringLiteral("modal-choice")) {
        if (modals.empty())
            return;
        auto accept = std::move(modals.back().accept);
        modals.pop_back();
        Notify();
        if (accept)
            accept(value);
        return;
    }
    if (name == QStringLiteral("tab")) {
        ChangeTab(value.toString());
        return;
    }
    if (name == QStringLiteral("desktop")) {
        Guard([this] {
            ReleaseSettings(false);
            emit DesktopRequested();
        });
        return;
    }
    if (name == QStringLiteral("selected")) {
        ui[QStringLiteral("selected")] = value;
        Notify();
        return;
    }
    if (name == QStringLiteral("section")) {
        EnsureSettings();
        SelectSection(value.toString());
        return;
    }
    if (name == QStringLiteral("group")) {
        EnsureSettings();
        for (const auto& page : settings.Sections())
            if (page.toMap().value(QStringLiteral("group")).toString() == value.toString()) {
                SelectSection(page.toMap().value(QStringLiteral("id")).toString());
                break;
            }
        return;
    }
    if (name == QStringLiteral("player")) {
        ui[QStringLiteral("player")] = std::clamp(value.toInt(), 0, 7);
        settings.Select(ui.value(QStringLiteral("section")).toString(),
                        ui.value(QStringLiteral("player")).toInt(),
                        ui.value(QStringLiteral("query")).toString());
        Notify();
        return;
    }
    if (name == QStringLiteral("scope")) {
        const QString destination = value.toString();
        Guard([this, destination] {
            ReleaseSettings(false);
            ui[QStringLiteral("scope")] = destination;
            if (destination != QStringLiteral("global")) {
                ui[QStringLiteral("scopePath")] = SelectedPath();
                ui[QStringLiteral("scopeName")] = SelectedGame().value(QStringLiteral("name"));
            }
            EnsureSettings();
            Notify();
        });
        return;
    }
    if (name == QStringLiteral("search")) {
        Guard([this] {
            ui[QStringLiteral("tab")] = QStringLiteral("settings");
            EnsureSettings();
            openKeyboard(QStringLiteral("search"), ui.value(QStringLiteral("query")).toString(),
                         tr("Search all settings"));
        });
        return;
    }
    if (name == QStringLiteral("library-search")) {
        openKeyboard(QStringLiteral("library-search"),
                     ui.value(QStringLiteral("libraryQuery")).toString(), tr("Search games"));
        return;
    }
    if (name == QStringLiteral("clear-search")) {
        setSearch({});
        return;
    }
    if (name == QStringLiteral("review-save")) {
        if (SaveSettings()) {
            closeModal();
            Toast(tr("Settings saved."));
        }
        return;
    }
    if (name == QStringLiteral("save")) {
        save();
        return;
    }
    if (name == QStringLiteral("review")) {
        ShowModal({{QStringLiteral("kind"), QStringLiteral("review")},
                   {QStringLiteral("title"), tr("Review changes")}});
        return;
    }
    if (name == QStringLiteral("discard")) {
        discard();
        modals.clear();
        Notify();
        return;
    }
    if (name == QStringLiteral("help")) {
        ShowModal({{QStringLiteral("kind"), QStringLiteral("help")},
                   {QStringLiteral("row"), settings.Describe(value.toInt())}});
        return;
    }
    if (name == QStringLiteral("close")) {
        closeModal();
        return;
    }
    if (name == QStringLiteral("leave-save") || name == QStringLiteral("leave-discard")) {
        if (!pending)
            return;
        if (name == QStringLiteral("leave-save") && !SaveSettings())
            return;
        if (name == QStringLiteral("leave-discard")) {
            ReleaseSettings(true);
            ReloadPreferences();
        }
        modals.clear();
        auto continuation = std::move(pending);
        pending = {};
        continuation();
        Notify();
        return;
    }
    if (name == QStringLiteral("play")) {
        if (SelectedPath().isEmpty())
            return;
        Guard([this] {
            ReleaseSettings(false);
            emit LaunchGame(SelectedPath());
        });
        return;
    }
    if (name == QStringLiteral("game-settings")) {
        Guard([this] {
            ReleaseSettings(false);
            ui[QStringLiteral("scope")] = QString(
                QStringLiteral("game:") + SelectedGame().value(QStringLiteral("id")).toString());
            ui[QStringLiteral("scopePath")] = SelectedPath();
            ui[QStringLiteral("scopeName")] = SelectedGame().value(QStringLiteral("name"));
            ui[QStringLiteral("tab")] = QStringLiteral("settings");
            EnsureSettings();
            SelectSection(QStringLiteral("graphics"));
        });
        return;
    }
    if (name == QStringLiteral("favorite")) {
        emit FavoriteRequested(SelectedGame().value(QStringLiteral("programId")).toULongLong());
        return;
    }
    if (name == QStringLiteral("add-directory")) {
        emit AddDirectoryRequested();
        return;
    }
    if (name == QStringLiteral("rename-collection")) {
        for (const auto& item : collection_data) {
            const auto data = item.toMap();
            if (data.value(QStringLiteral("id")) == value)
                openKeyboard(QStringLiteral("rename-collection:") + value.toString(),
                             data.value(QStringLiteral("name")).toString(),
                             tr("Rename collection"));
        }
        return;
    }
    if (name == QStringLiteral("delete-collection")) {
        ShowModal({{QStringLiteral("kind"), QStringLiteral("choices")},
                   {QStringLiteral("title"), tr("Delete this collection? Games are kept.")},
                   {QStringLiteral("choices"),
                    QVariantList{QVariantMap{{QStringLiteral("label"), tr("Keep collection")},
                                             {QStringLiteral("value"), false}},
                                 QVariantMap{{QStringLiteral("label"), tr("Delete collection")},
                                             {QStringLiteral("value"), true}}}}},
                  [this, value](const QVariant& yes) {
                      if (!yes.toBool())
                          return;
                      collection_data.removeIf([&value](const QVariant& item) {
                          return item.toMap().value(QStringLiteral("id")) == value;
                      });
                      SaveCollections();
                      emit gamesChanged();
                  });
        return;
    }
    if (name == QStringLiteral("new-collection")) {
        openKeyboard(name, {}, tr("Collection name"));
        return;
    }
    if (name == QStringLiteral("collection")) {
        Guard([this, value] {
            ui[QStringLiteral("collection")] = value;
            ui[QStringLiteral("libraryQuery")] = QString{};
            ui[QStringLiteral("tab")] = QStringLiteral("library");
            Notify();
        });
        return;
    }
    if (name == QStringLiteral("collection-toggle")) {
        const QString id = value.toString();
        for (auto& collection : collection_data) {
            auto data = collection.toMap();
            if (data.value(QStringLiteral("id")).toString() != id)
                continue;
            auto members = data.value(QStringLiteral("games")).toList();
            const QVariant selected = ui.value(QStringLiteral("selected"));
            if (members.contains(selected))
                members.removeAll(selected);
            else
                members.push_back(selected);
            data[QStringLiteral("games")] = members;
            collection = data;
        }
        SaveCollections();
        emit gamesChanged();
        return;
    }
    if (name == QStringLiteral("import-theme")) {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Import Couch theme"), QDir::homePath(), tr("Couch theme (theme.json)"));
        if (path.isEmpty())
            return;
        QString id, error;
        if (!theme.Import(path, &id, &error))
            Toast(error);
        else {
            EnsureSettings();
            if (source.dialog)
                if (auto* choice =
                        source.dialog->findChild<QComboBox*>(QStringLiteral("couchTheme")))
                    if (choice->findData(id) < 0)
                        for (const auto& installed : theme.Installed()) {
                            const auto data = installed.toMap();
                            if (data.value(QStringLiteral("id")).toString() == id)
                                choice->addItem(data.value(QStringLiteral("name")).toString(), id);
                        }
            chooseTheme(id);
            Toast(tr("Theme imported. Save to keep it."));
        }
        return;
    }
    if (name == QStringLiteral("input-test")) {
        ShowModal({{QStringLiteral("kind"), QStringLiteral("input-test")},
                   {QStringLiteral("title"), tr("Test controller inputs")}});
        return;
    }
    if (name == QStringLiteral("controllers") || name == QStringLiteral("profiles")) {
        Guard([this, name] {
            if (ui.value(QStringLiteral("scope")).toString() != QStringLiteral("global")) {
                ReleaseSettings(false);
                ui[QStringLiteral("scope")] = QStringLiteral("global");
            }
            ui[QStringLiteral("tab")] = QStringLiteral("settings");
            EnsureSettings();
            SelectSection(name == QStringLiteral("profiles") ? QStringLiteral("profiles")
                                                             : QStringLiteral("bindings"));
        });
        return;
    }
    if (name == QStringLiteral("tree")) {
        ShowModal({{QStringLiteral("kind"), QStringLiteral("tree")},
                   {QStringLiteral("id"), value},
                   {QStringLiteral("rows"), settings.TreeRows(value.toInt())},
                   {QStringLiteral("title"),
                    settings.Describe(value.toInt()).value(QStringLiteral("label"))}});
        return;
    }
    if (name == QStringLiteral("cancel-mapping") && source.mapping_key) {
        source.mapping_key(Qt::Key_Escape);
        Notify();
        return;
    }
}

void CouchMode::treeAction(int row, int column, bool edit_value) {
    if (modals.empty())
        return;
    const int id = modals.back().data.value(QStringLiteral("id")).toInt();
    settings.ActivateTree(id, row, column, edit_value);
    modals.back().data[QStringLiteral("rows")] = settings.TreeRows(id);
    Notify();
}

void CouchMode::RefreshControllerImage() {
    if (!source.controller_image)
        return;
    QImage image = source.controller_image(ui.value(QStringLiteral("player")).toInt());
    if (image.isNull())
        return;
    {
        auto* provider = static_cast<CouchImages*>(images);
        QMutexLocker lock(&provider->mutex);
        provider->controller = std::move(image);
    }
    ui[QStringLiteral("controllerImage")] = QString(
        QStringLiteral("image://couch/controller?frame=") + QString::number(++image_serial));
    Notify();
}

void CouchMode::PollController() {
    if (!active)
        return;
    const bool owns_modal =
        (QApplication::activeModalWidget() &&
         QApplication::activeModalWidget()->property("couchDialogPrepared").toBool()) ||
        QApplication::activePopupWidget();
    if (isVisible() && !window()->isActiveWindow() && !owns_modal)
        return;
    const qint64 now = clock.elapsed();
    const bool mapping = source.mapping && source.mapping();
    if (ui.value(QStringLiteral("mapping")).toBool() != mapping) {
        ui[QStringLiteral("mapping")] = mapping;
        settings.Refresh();
        for (const auto& presentation : native_presentations)
            if (presentation)
                if (auto* session =
                        presentation->parentWidget()->findChild<QtCommon::Couch::NativeDialog*>())
                    session->Settings()->Refresh();
        Notify();
    }
    const bool test =
        !modals.empty() &&
        modals.back().data.value(QStringLiteral("kind")).toString() == QStringLiteral("input-test");
    if (source.dialog &&
        (ui.value(QStringLiteral("section")).toString() == QStringLiteral("bindings") || test ||
         mapping) &&
        now - last_image >= 33) {
        last_image = now;
        RefreshControllerImage();
        if (mapping)
            settings.Refresh();
    }
    SDL_UpdateGamepads();
    int count = 0;
    SDL_JoystickID* devices = SDL_GetGamepads(&count);
    QSet<int> connected;
    for (int i = 0; i < count; ++i) {
        const int id = static_cast<int>(devices[i]);
        connected.insert(id);
        if (!pads.contains(id)) {
            if (auto* pad = SDL_OpenGamepad(devices[i]))
                pads.insert(id, pad);
        }
    }
    SDL_free(devices);
    for (auto it = pads.begin(); it != pads.end();) {
        if (!connected.contains(it.key())) {
            SDL_CloseGamepad(it.value());
            it = pads.erase(it);
        } else
            ++it;
    }
    if (!pads.contains(navigation_pad))
        navigation_pad = pads.isEmpty() ? 0 : pads.begin().key();
    for (auto it = pads.cbegin(); it != pads.cend(); ++it) {
        quint64 state = 0;
        for (int button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; ++button)
            if (SDL_GetGamepadButton(it.value(), static_cast<SDL_GamepadButton>(button)))
                state |= quint64{1} << button;
        const auto x = SDL_GetGamepadAxis(it.value(), SDL_GAMEPAD_AXIS_LEFTX);
        const auto y = SDL_GetGamepadAxis(it.value(), SDL_GAMEPAD_AXIS_LEFTY);
        if (x < -18000)
            state |= quint64{1} << 40;
        if (x > 18000)
            state |= quint64{1} << 41;
        if (y < -18000)
            state |= quint64{1} << 42;
        if (y > 18000)
            state |= quint64{1} << 43;
        if ((state & ~pad_state.value(it.key())) != 0)
            navigation_pad = it.key();
        pad_state[it.key()] = state;
    }
    SDL_Gamepad* pad = pads.value(navigation_pad, nullptr);
    if (!pad) {
        if (ui.value(QStringLiteral("controllerName")).toString() != tr("No controller detected")) {
            ui[QStringLiteral("controllerName")] = tr("No controller detected");
            Notify();
        }
        held.clear();
        if (pending_binding >= 0) {
            const int id = std::exchange(pending_binding, -1);
            settings.Activate(id);
        } else if (pending_activation) {
            auto action = std::move(pending_activation);
            pending_activation = {};
            action();
        }
        return;
    }
    const QString device_name = QString::fromUtf8(SDL_GetGamepadName(pad));
    if (ui.value(QStringLiteral("controllerName")).toString() != device_name) {
        ui[QStringLiteral("controllerName")] = device_name;
        Notify();
    }
    const int family = ui.value(QStringLiteral("promptPreference"),
                                static_cast<int>(UISettings::values.couch_prompts.GetValue()))
                           .toInt();
    const auto type = SDL_GetGamepadType(pad);
    const bool nintendo =
        family == 2 || (family == 0 && (type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO ||
                                        type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT ||
                                        type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT ||
                                        type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR));
    const bool playstation = family == 3 || (family == 0 && (type == SDL_GAMEPAD_TYPE_PS3 ||
                                                             type == SDL_GAMEPAD_TYPE_PS4 ||
                                                             type == SDL_GAMEPAD_TYPE_PS5));
    const QString prompts = nintendo      ? QStringLiteral("nintendo")
                            : playstation ? QStringLiteral("playstation")
                                          : QStringLiteral("xbox");
    if (ui.value(QStringLiteral("prompts")).toString() != prompts) {
        ui[QStringLiteral("prompts")] = prompts;
        Notify();
    }
    const auto down = [pad](SDL_GamepadButton button) { return SDL_GetGamepadButton(pad, button); };
    QHash<QString, bool> current{
        {QStringLiteral("left"), down(SDL_GAMEPAD_BUTTON_DPAD_LEFT) ||
                                     SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) < -18000},
        {QStringLiteral("right"), down(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) ||
                                      SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) > 18000},
        {QStringLiteral("up"), down(SDL_GAMEPAD_BUTTON_DPAD_UP) ||
                                   SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY) < -18000},
        {QStringLiteral("down"), down(SDL_GAMEPAD_BUTTON_DPAD_DOWN) ||
                                     SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY) > 18000},
        {QStringLiteral("accept"),
         down(nintendo ? SDL_GAMEPAD_BUTTON_EAST : SDL_GAMEPAD_BUTTON_SOUTH)},
        {QStringLiteral("back"),
         down(nintendo ? SDL_GAMEPAD_BUTTON_SOUTH : SDL_GAMEPAD_BUTTON_EAST)},
        {QStringLiteral("search"), down(SDL_GAMEPAD_BUTTON_WEST)},
        {QStringLiteral("menu"), down(SDL_GAMEPAD_BUTTON_START)},
        {QStringLiteral("more"), down(SDL_GAMEPAD_BUTTON_NORTH)},
        {QStringLiteral("tab-prev"), down(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)},
        {QStringLiteral("tab-next"), down(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)},
        {QStringLiteral("group-prev"),
         SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16000},
        {QStringLiteral("group-next"),
         SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000}};
    if (!isVisible() && !owns_modal) {
        const bool chord = down(SDL_GAMEPAD_BUTTON_BACK) && down(SDL_GAMEPAD_BUTTON_START);
        if (chord && !held.value(QStringLiteral("pause")))
            emit PauseRequested();
        held[QStringLiteral("pause")] = chord;
        return;
    }
    if (pending_binding >= 0 || pending_activation) {
        bool any = false;
        for (int button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; ++button)
            any |= down(static_cast<SDL_GamepadButton>(button));
        any |= std::abs(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX)) > 16000 ||
               std::abs(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY)) > 16000 ||
               std::abs(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX)) > 16000 ||
               std::abs(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY)) > 16000;
        any |= SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16000 ||
               SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000;
        if (!any) {
            if (pending_binding >= 0) {
                const int id = std::exchange(pending_binding, -1);
                settings.Activate(id);
            } else {
                auto action = std::move(pending_activation);
                pending_activation = {};
                action();
            }
        }
        held = current;
        return;
    }
    if (test) {
        if (current.value(QStringLiteral("back"))) {
            if (!test_exit)
                test_exit = now;
            if (now - test_exit >= 1000) {
                closeModal();
                test_exit = 0;
            }
        } else
            test_exit = 0;
        held = current;
        return;
    }
    if (mapping) {
        held = current;
        return;
    }
    for (auto it = current.cbegin(); it != current.cend(); ++it) {
        const QString key = it.key();
        if (!it.value()) {
            held_since.remove(key);
            continue;
        }
        const bool fresh = !held.value(key);
        if (fresh)
            held_since[key] = now;
        const bool direction = key == QStringLiteral("up") || key == QStringLiteral("down") ||
                               key == QStringLiteral("left") || key == QStringLiteral("right");
        if (fresh || (direction && now - held_since.value(key) > 330 &&
                      now - repeated_at.value(key) > 110)) {
            Deliver(key);
            repeated_at[key] = now;
        }
    }
    held = current;
}

void CouchMode::Deliver(const QString& key) {
    while (!native_presentations.empty() &&
           (!native_presentations.back() || !native_presentations.back()->isVisible()))
        native_presentations.pop_back();
    if (!native_presentations.empty()) {
        if (auto* root = native_presentations.back()->rootObject())
            QMetaObject::invokeMethod(root, "controller", Q_ARG(QVariant, QVariant(key)));
    } else
        emit input(key);
}
void CouchMode::keyPressEvent(QKeyEvent* event) {
    if (source.mapping && source.mapping() && source.mapping_key) {
        source.mapping_key(event->key());
        event->accept();
        return;
    }
    QQuickWidget::keyPressEvent(event);
}
bool CouchMode::eventFilter(QObject* object, QEvent* event) {
    if (!active)
        return QQuickWidget::eventFilter(object, event);
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (source.mapping && source.mapping() && source.mapping_key) {
            source.mapping_key(key->key());
            return true;
        }
        auto* widget = qobject_cast<QWidget*>(object);
        auto* quick = widget ? qobject_cast<QQuickWidget*>(widget) : nullptr;
        if (!quick && widget && widget->window()->property("couchDialogPrepared").toBool())
            return true;
        if (quick && quick->property("couchPresentation").toBool()) {
            const auto* focus = quick->quickWindow()->activeFocusItem();
            const bool typing = focus && focus->property("cursorPosition").isValid();
            QString action;
            switch (key->key()) {
            case Qt::Key_Escape:
                action = QStringLiteral("back");
                break;
            case Qt::Key_Left:
                if (!typing)
                    action = QStringLiteral("left");
                break;
            case Qt::Key_Right:
                if (!typing)
                    action = QStringLiteral("right");
                break;
            case Qt::Key_Up:
                if (!typing)
                    action = QStringLiteral("up");
                break;
            case Qt::Key_Down:
                if (!typing)
                    action = QStringLiteral("down");
                break;
            case Qt::Key_Return:
            case Qt::Key_Enter:
                if (!typing)
                    action = QStringLiteral("accept");
                break;
            case Qt::Key_Q:
                if (!typing)
                    action = QStringLiteral("tab-prev");
                break;
            case Qt::Key_E:
                if (!typing)
                    action = QStringLiteral("tab-next");
                break;
            case Qt::Key_PageUp:
                if (!typing)
                    action = QStringLiteral("group-prev");
                break;
            case Qt::Key_PageDown:
                if (!typing)
                    action = QStringLiteral("group-next");
                break;
            case Qt::Key_F3:
                action = QStringLiteral("search");
                break;
            case Qt::Key_F10:
                action = QStringLiteral("menu");
                break;
            }
            if (!action.isEmpty()) {
                Deliver(action);
                return true;
            }
        }
    }
    if (auto* menu = qobject_cast<QMenu*>(object)) {
        if (event->type() == QEvent::Polish && isVisible())
            menu->setAttribute(Qt::WA_DontShowOnScreen);
        if (event->type() == QEvent::Show && isVisible() &&
            !menu->property("couchMenuPrepared").toBool()) {
            menu->setProperty("couchMenuPrepared", true);
            QPointer<QMenu> safe = menu;
            QTimer::singleShot(0, this, [this, safe] {
                if (safe)
                    ShowNativeMenu(safe);
            });
        }
    }
    auto* dialog = qobject_cast<QDialog*>(object);
    if (dialog && event->type() == QEvent::Polish && !dialog->property("couchSource").toBool()) {
        bool owned = false;
        for (auto* ancestor = dialog->parentWidget(); ancestor; ancestor = ancestor->parentWidget())
            owned |= ancestor == this || ancestor->property("couchSource").toBool() ||
                     ancestor->property("couchOwner").toBool();
        if (owned)
            dialog->setWindowFlag(Qt::FramelessWindowHint);
    }
    if (dialog && event->type() == QEvent::Show && !dialog->property("couchSource").toBool() &&
        !dialog->property("couchDialogPrepared").toBool() && !preparing_dialog) {
        bool owned = dialog->parentWidget() == this;
        for (auto* ancestor = dialog->parentWidget(); ancestor; ancestor = ancestor->parentWidget())
            owned |= ancestor == this || ancestor->property("couchOwner").toBool() ||
                     (source.dialog && ancestor == source.dialog.get());
        if (owned) {
            dialog->setProperty("couchDialogPrepared", true);
            QPointer<QDialog> safe = dialog;
            QTimer::singleShot(0, this, [this, safe] {
                if (safe)
                    ShowNativeDialog(safe);
            });
        }
    }
    if (dialog && event->type() == QEvent::Resize &&
        dialog->property("couchDialogPrepared").toBool())
        if (auto* presentation = dialog->findChild<QQuickWidget*>(
                QStringLiteral("CouchNativePresentation"), Qt::FindDirectChildrenOnly))
            presentation->setGeometry(dialog->rect());
    return QQuickWidget::eventFilter(object, event);
}
void CouchMode::resizeEvent(QResizeEvent* event) {
    QQuickWidget::resizeEvent(event);
    for (const auto& presentation : native_presentations)
        if (presentation) {
            auto* dialog = presentation->parentWidget();
            dialog->setFixedSize(event->size());
            dialog->move(window()->pos());
            presentation->setGeometry(dialog->rect());
        }
}
void CouchMode::ShowNativeDialog(QDialog* dialog) {
    preparing_dialog = true;
    auto* presentation = new QQuickWidget(dialog);
    presentation->setObjectName(QStringLiteral("CouchNativePresentation"));
    presentation->setProperty("couchPresentation", true);
    auto* session = new QtCommon::Couch::NativeDialog(dialog, &theme, presentation);
    session->Settings()->SetActivationGate(
        [this](std::function<void()> action) { pending_activation = std::move(action); });
    presentation->setResizeMode(QQuickWidget::SizeRootObjectToView);
    presentation->rootContext()->setContextProperty(QStringLiteral("Native"), session);
    presentation->rootContext()->setContextProperty(QStringLiteral("Couch"), this);
    presentation->setSource(QUrl(QStringLiteral("qrc:/couch/NativeDialog.qml")));
    if (dialog->layout())
        dialog->layout()->setSizeConstraint(QLayout::SetNoConstraint);
    dialog->setMinimumSize(0, 0);
    dialog->setFixedSize(window()->size());
    dialog->setGeometry(window()->geometry());
    presentation->setGeometry(dialog->rect());
    presentation->show();
    presentation->raise();
    presentation->setFocus();
    native_presentations.push_back(presentation);
    emit NavigationOwnerChanged(true);
    connect(dialog, &QDialog::finished, this, [this, presentation] {
        std::erase_if(native_presentations, [presentation](const QPointer<QQuickWidget>& item) {
            return !item || item == presentation;
        });
        settings.Refresh();
        emit NavigationOwnerChanged(isVisible() || !native_presentations.empty());
        setFocus();
    });
    preparing_dialog = false;
}
void CouchMode::ShowNativeMenu(QMenu* menu) {
    QVariantList choices;
    const auto actions = menu->actions();
    for (int index = 0; index < actions.size(); ++index) {
        const auto* item = actions[index];
        if (item->isSeparator())
            continue;
        choices.push_back(
            QVariantMap{{QStringLiteral("label"),
                         QString((item->isChecked() ? QStringLiteral("✓ ") : QString{}) +
                                 item->text().remove(QLatin1Char('&')))},
                        {QStringLiteral("value"), index},
                        {QStringLiteral("enabled"), item->isEnabled()}});
    }
    QPointer<QMenu> safe = menu;
    ShowModal(
        {{QStringLiteral("kind"), QStringLiteral("choices")},
         {QStringLiteral("title"), menu->title().isEmpty() ? tr("More options") : menu->title()},
         {QStringLiteral("choices"), choices}},
        [this, safe](const QVariant& choice) {
            if (!safe)
                return;
            const auto entries = safe->actions();
            const int index = choice.toInt();
            if (index >= 0 && index < entries.size()) {
                if (auto* submenu = entries[index]->menu()) {
                    ShowNativeMenu(submenu);
                    return;
                }
                entries[index]->trigger();
            }
            for (auto* popup = QApplication::activePopupWidget(); popup;
                 popup = QApplication::activePopupWidget())
                popup->close();
        });
}
