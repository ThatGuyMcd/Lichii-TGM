// SPDX-License-Identifier: GPL-3.0-or-later
#include "yuzu/configuration/configure_dialog.h"
#include "yuzu/configuration/configure_hotkeys.h"
#include "yuzu/configuration/configure_input_player.h"
#include "yuzu/configuration/configure_per_game.h"
#include "yuzu/configuration/configure_ringcon.h"
#include "yuzu/configuration/configure_touch_from_button.h"
#include "yuzu/couch/configuration_snapshot.h"
#include "yuzu/couch/couch_mode.h"
#include "yuzu/main_window.h"
#ifdef HAS_RESHADE
#include "video_core/post_processing/fx_chain.h"
#endif
#include <algorithm>
#include <QAction>
#include <QApplication>
#include <QFileDialog>
#include <QMenu>
#include <QPainter>
#include <QScreen>
#include <QWindow>
#include "common/fs/path_util.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/hle/service/acc/profile_manager.h"
#include "hid_core/hid_core.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/couch/welcome.h"
#include "qt_common/discord/discord.h"
#include "qt_common/gui_settings.h"
#include "qt_common/util/game.h"
#include "ui_main.h"
#include "yuzu/bootmanager.h"
#include "yuzu/debugger/controller.h"
#include "yuzu/game/game_list.h"
#include "yuzu/multiplayer/state.h"
#include "yuzu/util/controller_navigation.h"

void MainWindow::InitializeCouchMode() {
    setProperty("couchOwner", true);
    auto* action = ui->menu_View->addAction(tr("Couch Mode"));
    action->setObjectName(QStringLiteral("actionCouchMode"));
    action->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+C")));
    connect(action, &QAction::triggered, this, &MainWindow::ShowCouchMode);
    if (UISettings::values.couch_startup.GetValue() ||
        QApplication::arguments().contains(QStringLiteral("--couch"))) {
        QTimer::singleShot(0, this, [this] {
            if (!boot_in_progress) {
                const bool resume = emulation_running;
                ShowCouchMode();
                if (resume)
                    couch_mode->action(QStringLiteral("resume"));
            }
        });
    } else {
        QTimer::singleShot(0, this, &MainWindow::MaybeOfferCouchMode);
    }
}

void MainWindow::MaybeOfferCouchMode() {
    if (couch_invitation_shown || UISettings::values.couch_invitation_dismissed.GetValue() ||
        couch_enabled || boot_in_progress || emulation_running || !isVisible() || isMinimized())
        return;
    if (QApplication::activeModalWidget()) {
        QTimer::singleShot(500, this, &MainWindow::MaybeOfferCouchMode);
        return;
    }
    couch_invitation_shown = true;
    QtCommon::Couch::WelcomeDialog dialog([this] {
        UISettings::values.couch_invitation_dismissed = true;
        config->SaveAllValues();
    }, this);
    ControllerNavigation navigation(QtCommon::system->HIDCore());
    connect(&navigation, &ControllerNavigation::TriggerKeyboardEvent,
            &dialog, &QtCommon::Couch::WelcomeDialog::Navigate);
    const bool accepted = dialog.exec() == QDialog::Accepted;
    navigation.UnloadController();
    if (accepted)
        ShowCouchMode();
}

void MainWindow::ShowCouchMode() {
    if (!UISettings::values.couch_invitation_dismissed.GetValue()) {
        UISettings::values.couch_invitation_dismissed = true;
        config->SaveAllValues();
    }
    hotkey_registry.SetCouchNavigationActive(true);
    if (!couch_mode) {
        couch_mode = std::make_unique<CouchMode>(
            game_list->GetModel(),
            QString::fromStdString(Common::FS::GetEdenPathString(Common::FS::EdenPath::ConfigDir)),
            [this](const QString& scope, const QString& path) {
                return CreateCouchSettings(scope, path);
            });
        couch_mode->setWindowTitle(tr("Lichii-tgm — Couch Mode"));
        couch_mode->setWindowFlag(Qt::FramelessWindowHint);
        connect(couch_mode.get(), &CouchMode::NavigationOwnerChanged, this,
                [this](bool active) { hotkey_registry.SetCouchNavigationActive(active); });
        connect(couch_mode.get(), &CouchMode::DesktopRequested, this, &MainWindow::LeaveCouchMode);
        connect(couch_mode.get(), &CouchMode::LaunchGame, this, [this](const QString& path) {
            couch_mode->hide();
            hotkey_registry.SetCouchNavigationActive(false);
            show();
            if (!couch_previous_fullscreen)
                couch_previous_fullscreen = ui->action_Fullscreen->isChecked();
            ui->action_Fullscreen->setChecked(true);
            BootGameFromList(path, StartGameType::Normal);
            couch_mode->SetGameRunning(emulation_running);
            if (!emulation_running)
                ShowCouchMode();
        });
        connect(couch_mode.get(), &CouchMode::AddDirectoryRequested, this, [this] {
            const auto path =
                QFileDialog::getExistingDirectory(couch_mode.get(), tr("Add game directory"));
            if (path.isEmpty())
                return;
            // Use the same directory store and scanner as the desktop library.
            const auto found = std::find_if(
                UISettings::values.game_dirs.cbegin(), UISettings::values.game_dirs.cend(),
                [&path](const auto& item) { return item.path == path.toStdString(); });
            if (found == UISettings::values.game_dirs.cend())
                UISettings::values.game_dirs.push_back({path.toStdString(), true, true});
            config->SaveAllValues();
            game_list->PopulateAsync(UISettings::values.game_dirs);
        });
        connect(couch_mode.get(), &CouchMode::FavoriteRequested, this, [this](qulonglong id) {
            auto& favorites = UISettings::values.favorited_ids;
            if (favorites.contains(id))
                favorites.removeAll(id);
            else
                favorites.push_back(id);
            config->SaveAllValues();
            game_list->PopulateAsync(UISettings::values.game_dirs);
            couch_mode->refreshLibrary();
        });
        connect(couch_mode.get(), &CouchMode::ConfigurationSaved, this,
                &MainWindow::ApplyCouchConfiguration);
        connect(couch_mode.get(), &CouchMode::CloseApproved, this, [this] {
            couch_close_approved = true;
            close();
            couch_close_approved = false;
        });
        connect(couch_mode.get(), &CouchMode::ExitRequested, this, &QWidget::close);
        connect(couch_mode.get(), &CouchMode::PauseRequested, this, [this] {
            if (emulation_running && (isActiveWindow() || render_window->isActiveWindow())) {
                OnPauseGame();
                ShowCouchMode();
            }
        });
        connect(couch_mode.get(), &CouchMode::ResumeRequested, this, [this] {
            hotkey_registry.SetCouchNavigationActive(false);
            if (!emulation_running)
                return;
            couch_mode->hide();
            show();
            render_window->show();
            render_window->raise();
            raise();
            if (couch_previous_fullscreen)
                ShowFullscreen();
            OnStartGame();
        });
        connect(couch_mode.get(), &CouchMode::StopRequested, this, &MainWindow::OnStopGame);
    }
    if (!emulation_running)
        RestoreCouchFullscreen();
    if (emulation_running && !couch_previous_fullscreen) {
        couch_previous_fullscreen = ui->action_Fullscreen->isChecked();
        ui->action_Fullscreen->setChecked(true);
    }
    couch_enabled = true;
    if (emulation_running)
        OnPauseGame();
    couch_mode->SetGameRunning(emulation_running);
    UpdateCouchProfile();
    couch_mode->ReloadPreferences();
    couch_mode->SetActive(true);
    // Let QQuickWidget create its own surface; forcing winId() can break its compositor.
    if (windowHandle() && windowHandle()->screen())
        couch_mode->setGeometry(windowHandle()->screen()->geometry());
    couch_mode->showFullScreen();
    couch_mode->raise();
    couch_mode->activateWindow();
    hide();
    if (emulation_running)
        render_window->hide();
}

void MainWindow::LeaveCouchMode() {
    couch_enabled = false;
    hotkey_registry.SetCouchNavigationActive(false);
    RestoreCouchFullscreen();
    couch_mode->SetActive(false);
    couch_mode->hide();
    show();
    raise();
    activateWindow();
    if (emulation_running) {
        render_window->show();
        render_window->raise();
        OnStartGame();
    }
}

CouchSettingsSource MainWindow::CreateCouchSettings(const QString& scope, const QString& path) {
    auto snapshot = std::make_shared<CouchConfigurationSnapshot>();
    CouchSettingsSource result;
    if (scope == QStringLiteral("global")) {
        Settings::SetConfiguringGlobal(true);
        auto dialog = std::make_unique<ConfigureDialog>(
            couch_mode.get(), hotkey_registry, input_subsystem.get(), vk_device_records,
            *QtCommon::system, !multiplayer_state->IsHostingPublicRoom());
        auto* native = dialog.get();
        native->CouchSetResetCallback([this] {
            QTimer::singleShot(0, this, [this] {
                couch_mode->ResetSourceAfterOperation([this] {
                    ResetConfigurationDefaults();
                    config->SaveAllValues();
                });
            });
        });
        result.pages = native->CouchPages();
        result.select_page = [native](const QString& id) { native->CouchSelectPage(id); };
        result.apply = [this, native, snapshot] {
            native->ApplyConfiguration();
            hotkey_registry.SaveHotkeys();
            config->SaveAllValues();
            UISettings::values.configuration_applied = false;
            snapshot->Capture();
            return true;
        };
        result.dialog = std::move(dialog);
    } else {
        u64 program_id{};
        for (const auto& item : couch_mode->Games()) {
            const auto game = item.toMap();
            if (game.value(QStringLiteral("path")).toString() == path) {
                program_id = game.value(QStringLiteral("programId")).toULongLong();
                break;
            }
        }
        if (path.isEmpty())
            return {};
        Settings::SetConfiguringGlobal(false);
        auto dialog = std::make_unique<ConfigurePerGame>(
            couch_mode.get(), program_id, path.toStdString(), vk_device_records, *QtCommon::system);
        auto* native = dialog.get();
        native->LoadFromFile(Core::GetGameFileFromPath(QtCommon::vfs, path.toStdString()));
        result.pages = native->CouchPages();
        result.select_page = [native](const QString& id) { native->CouchSelectPage(id); };
        result.apply = [this, native, program_id, snapshot] {
            if (!native->CouchApply())
                return false;
            if (const auto renamed = native->RenamedTitle())
                game_list->SetGameTitle(program_id, *renamed);
            UISettings::values.configuration_applied = false;
            snapshot->Capture();
            return true;
        };
        const bool other_running_game =
            emulation_running && program_id != QtCommon::system->GetApplicationProcessProgramID();
        auto running_snapshot =
            other_running_game ? std::make_shared<CouchConfigurationSnapshot>(*snapshot) : snapshot;
        result.cleanup = [running_snapshot] {
            if (QtCommon::system->IsPoweredOn()) {
                running_snapshot->Restore();
                QtCommon::system->HIDCore().ReloadInputDevices();
                QtCommon::system->ApplySettings();
            }
            Settings::RestoreGlobalState(QtCommon::system->IsPoweredOn());
            Settings::SetConfiguringGlobal(true);
        };
        result.dialog = std::move(dialog);
    }
    result.dialog->setProperty("couchSource", true);
    result.discard = [snapshot, this] {
        snapshot->Restore();
        hotkey_registry.LoadHotkeys();
        QtCommon::system->HIDCore().ReloadInputDevices();
        QtCommon::system->ApplySettings();
#ifdef HAS_RESHADE
        VideoCore::FxChain::Instance().LoadFromSettings();
#endif
    };
    auto baseline = std::make_shared<CouchConfigurationSnapshot>();
    result.external_dirty = [baseline] { return baseline->Changed(); };
    auto apply = std::move(result.apply);
    result.apply = [apply = std::move(apply), baseline] {
        if (!apply())
            return false;
        baseline->Capture();
        return true;
    };
    QPointer<QDialog> owner = result.dialog.get();
    result.controller_image = [owner](int player) -> QImage {
        if (!owner)
            return {};
        for (auto* controls : owner->findChildren<ConfigureInputPlayer*>()) {
            if (controls->property("couchPlayer").toInt() != player)
                continue;
            auto* preview = controls->findChild<QWidget*>(QStringLiteral("controllerFrame"));
            if (!preview)
                return {};
            preview->ensurePolished();
            preview->resize(900, 560);
            QImage image(preview->size() * 2, QImage::Format_ARGB32_Premultiplied);
            image.setDevicePixelRatio(2);
            image.fill(Qt::transparent);
            preview->render(&image, QPoint{}, QRegion{}, QWidget::DrawChildren);
            return image;
        }
        return {};
    };
    result.mapping = [owner] {
        if (!owner)
            return false;
        for (auto* controls : owner->findChildren<ConfigureInputPlayer*>())
            if (controls->CouchIsMapping())
                return true;
        for (auto* hotkeys : owner->findChildren<ConfigureHotkeys*>())
            if (hotkeys->CouchIsMapping())
                return true;
        for (auto* ring : owner->findChildren<ConfigureRingController*>())
            if (ring->CouchIsMapping())
                return true;
        for (auto* touch : owner->findChildren<ConfigureTouchFromButton*>())
            if (touch->CouchIsMapping())
                return true;
        return false;
    };
    result.mapping_key = [owner](int key) {
        if (!owner)
            return;
        for (auto* controls : owner->findChildren<ConfigureInputPlayer*>())
            if (controls->CouchIsMapping())
                controls->CouchMappingKey(key);
        for (auto* ring : owner->findChildren<ConfigureRingController*>())
            if (ring->CouchIsMapping())
                ring->CouchMappingKey(key);
        for (auto* touch : owner->findChildren<ConfigureTouchFromButton*>())
            if (touch->CouchIsMapping())
                touch->CouchMappingKey(key);
        if (key == Qt::Key_Escape)
            for (auto* hotkeys : owner->findChildren<ConfigureHotkeys*>())
                hotkeys->CouchCancelMapping();
    };
    return result;
}

void MainWindow::ApplyCouchConfiguration() {
    UpdateCouchProfile();
    LoadTranslation();
#ifdef __unix__
    GraphicsBackend::SetForceX11(UISettings::values.gui_force_x11.GetValue());
#endif
    session_speed_limit.Reset();
    InitializeHotkeys();
    UpdateUITheme();
    UpdatePlaybackToolbar();
    SetDiscordEnabled(UISettings::values.enable_discord_presence.GetValue());
    SetGamemodeEnabled(UISettings::values.enable_gamemode.GetValue());
    if (!multiplayer_state->IsHostingPublicRoom())
        multiplayer_state->UpdateCredentials();
    emit UpdateThemedIcons();
    UISettings::values.is_game_list_reload_pending = false;
    game_list->PopulateAsync(UISettings::values.game_dirs);
    UpdateStatusButtons();
    controller_dialog->refreshConfiguration();
    QtCommon::system->HIDCore().ReloadInputDevices();
    QtCommon::system->ApplySettings();
}
void MainWindow::UpdateCouchProfile() {
    if (!couch_mode)
        return;
    auto& manager = QtCommon::system->GetProfileManager();
    const auto user = manager.GetUser(Settings::values.current_user.GetValue());
    QString name;
    QImage image;
    if (user) {
        Service::Account::ProfileBase profile{};
        if (manager.GetProfileBase(*user, profile))
            name = QString::fromStdString(Common::StringFromFixedZeroTerminatedBuffer(
                reinterpret_cast<const char*>(profile.username.data()), profile.username.size()));
        const auto root = Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir);
        const auto path =
            root / "system/save/8000000000000010/su/avators" / (user->FormattedString() + ".jpg");
        image.load(QString::fromStdString(Common::FS::PathToUTF8String(path)));
    }
    couch_mode->SetProfile(name, image);
}
void MainWindow::RestoreCouchFullscreen() {
    if (!couch_previous_fullscreen)
        return;
    const bool original = *couch_previous_fullscreen;
    if (!original && emulation_running)
        HideFullscreen();
    ui->action_Fullscreen->setChecked(original);
    UISettings::values.fullscreen = original;
    couch_previous_fullscreen.reset();
}
