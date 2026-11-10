// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>
#include <QScreen>
#include "common/logging.h"
#include "common/settings.h"
#include "common/settings_enums.h"
#include "core/core.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/touch_ui.h"
#include "qt_common/util/vk.h"
#include "ui_configure.h"
#include "yuzu/configuration/configure_applets.h"
#include "yuzu/configuration/configure_audio.h"
#include "yuzu/configuration/configure_cpu.h"
#include "yuzu/configuration/configure_debug_tab.h"
#include "yuzu/configuration/configure_dialog.h"
#include "yuzu/configuration/configure_filesystem.h"
#include "yuzu/configuration/configure_general.h"
#include "yuzu/configuration/configure_graphics.h"
#include "yuzu/configuration/configure_graphics_advanced.h"
#include "yuzu/configuration/configure_graphics_extensions.h"
#include "yuzu/configuration/configure_hotkeys.h"
#include "yuzu/configuration/configure_input.h"
#include "yuzu/configuration/configure_input_player.h"
#include "yuzu/configuration/configure_network.h"
#include "yuzu/configuration/configure_profile_manager.h"
#include "yuzu/configuration/configure_system.h"
#include "yuzu/configuration/configure_ui.h"
#include "yuzu/configuration/configure_web.h"
#include "yuzu/configuration/settings_navigation.h"
#include "yuzu/hotkeys.h"

ConfigureDialog::ConfigureDialog(QWidget* parent, HotkeyRegistry& registry_,
                                 InputCommon::InputSubsystem* input_subsystem,
                                 std::vector<VkDeviceInfo::Record>& vk_device_records,
                                 Core::System& system_, bool enable_web_config)
    : QDialog(parent), ui{std::make_unique<Ui::ConfigureDialog>()}, registry(registry_),
      system{system_},
      builder{std::make_unique<ConfigurationShared::Builder>(this, !system_.IsPoweredOn())},
      applets_tab{std::make_unique<ConfigureApplets>(system_, nullptr, *builder, this)},
      audio_tab{std::make_unique<ConfigureAudio>(system_, nullptr, *builder, this)},
      cpu_tab{std::make_unique<ConfigureCpu>(system_, nullptr, *builder, this)},
      debug_tab_tab{std::make_unique<ConfigureDebugTab>(system_, this)},
      filesystem_tab{std::make_unique<ConfigureFilesystem>(this)},
      general_tab{std::make_unique<ConfigureGeneral>(system_, nullptr, *builder, this)},
      graphics_advanced_tab{
          std::make_unique<ConfigureGraphicsAdvanced>(system_, nullptr, *builder, this)},
      graphics_extensions_tab{
          std::make_unique<ConfigureGraphicsExtensions>(system_, nullptr, *builder, this)},
      ui_tab{std::make_unique<ConfigureUi>(system_, this)},
      graphics_tab{std::make_unique<ConfigureGraphics>(
          system_, vk_device_records, [&]() { graphics_advanced_tab->ExposeComputeOption(); },
          [this](Settings::AspectRatio ratio, Settings::ResolutionSetup setup) {
              ui_tab->UpdateScreenshotInfo(ratio, setup);
          },
          nullptr, *builder, this)},
      hotkeys_tab{std::make_unique<ConfigureHotkeys>(system_.HIDCore(), this)},
      input_tab{std::make_unique<ConfigureInput>(system_, this)},
      network_tab{std::make_unique<ConfigureNetwork>(system_, this)},
      profile_tab{std::make_unique<ConfigureProfileManager>(system_, this)},
      system_tab{std::make_unique<ConfigureSystem>(system_, nullptr, *builder, this)},
      web_tab{std::make_unique<ConfigureWeb>(this)} {
    Settings::SetConfiguringGlobal(true);

    ui->setupUi(this);
    setProperty("forestDialogPrepared", true);

    // Keep the generated form's footer and its accepted/rejected connections. All original
    // settings widgets move once into the new navigation, with their child tabs left intact.
    ui->horizontalLayout->removeWidget(ui->selectorList);
    ui->selectorList->hide();
    ui->horizontalLayout->removeWidget(ui->tabWidget);
    ui->tabWidget->hide();
    navigation = new ConfigurationShared::SettingsNavigation(this, QtCommon::TouchUi::TargetSize());
    ui->horizontalLayout->addWidget(navigation, 1);
    ui->label->setWordWrap(true);

    web_tab->SetWebServiceConfigEnabled(enable_web_config);
    hotkeys_tab->Populate(registry);

    input_tab->Initialize(input_subsystem);

    general_tab->SetResetCallback([&] { this->close(); });

    SetConfiguration();
    PopulateSelectionList();

    connect(ui_tab.get(), &ConfigureUi::LanguageChanged, this, &ConfigureDialog::OnLanguageChanged);
    connect(general_tab.get(), &ConfigureGeneral::ExternalContentDirsChanged, this,
            &ConfigureDialog::ExternalContentDirsChanged);

    QPushButton* apply_button = ui->buttonBox->addButton(QDialogButtonBox::Apply);
    connect(apply_button, &QAbstractButton::clicked, this,
            &ConfigureDialog::HandleApplyButtonClicked);

    setSizeGripEnabled(true);
    const QSize available = screen()->availableGeometry().size() - QSize(48, 64);
    setMinimumSize(QSize(480, 320).boundedTo(available));
    resize(QSize(1120, 800).boundedTo(available));
    UpdateSettingsShell();

    // Selects the leftmost button on the bottom bar (Cancel as of writing)
    ui->buttonBox->setFocus();
}

ConfigureDialog::~ConfigureDialog() = default;

void ConfigureDialog::SetConfiguration() {}

void ConfigureDialog::ApplyConfiguration() {
    general_tab->ApplyConfiguration();
    ui_tab->ApplyConfiguration();
    system_tab->ApplyConfiguration();
    profile_tab->ApplyConfiguration();
    filesystem_tab->ApplyConfiguration();
    input_tab->ApplyConfiguration();
    hotkeys_tab->ApplyConfiguration(registry);
    cpu_tab->ApplyConfiguration();
    graphics_tab->ApplyConfiguration();
    graphics_advanced_tab->ApplyConfiguration();
    graphics_extensions_tab->ApplyConfiguration();
    audio_tab->ApplyConfiguration();
    debug_tab_tab->ApplyConfiguration();
    web_tab->ApplyConfiguration();
    network_tab->ApplyConfiguration();
    applets_tab->ApplyConfiguration();
    system.ApplySettings();
    Settings::LogSettings();
}

void ConfigureDialog::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    QDialog::changeEvent(event);
}

void ConfigureDialog::RetranslateUI() {
    ui->retranslateUi(this);
    PopulateSelectionList();
    UpdateSettingsShell();
}

void ConfigureDialog::UpdateSettingsShell() {
    QtCommon::TouchUi::InstallSettingsShell(this, tr("Settings"),
                                           tr("Make Lichii-tgm feel at home."));
}

void ConfigureDialog::HandleApplyButtonClicked() {
    UISettings::values.configuration_applied = true;
    ApplyConfiguration();
    QtCommon::TouchUi::MarkApplied(this);
}

void ConfigureDialog::PopulateSelectionList() {
    navigation->SetPages({
        {general_tab.get(), tr("General"), tr("Application"),
         tr("language updates interface directories reset")},
        {ui_tab.get(), tr("Appearance and Library"), tr("Application"),
         tr("appearance theme screenshots library folders icons")},
        {hotkeys_tab.get(), tr("Hotkeys"), tr("Application"),
         tr("keyboard shortcuts key bindings")},
        {system_tab.get(), tr("System"), tr("Emulation"),
         tr("console region time clock language memory")},
        {profile_tab.get(), tr("Profiles"), tr("Emulation"), tr("users accounts avatars")},
        {filesystem_tab.get(), tr("Filesystem"), tr("Emulation"),
         tr("storage NAND SD card paths directories cache")},
        {applets_tab.get(), tr("Applets"), tr("Emulation"), tr("keyboard software controller")},
        {cpu_tab.get(), tr("CPU"), tr("Emulation"), tr("processor accuracy multicore emulation")},
        {graphics_tab.get(), tr("Graphics"), tr("Video"),
         tr("GPU renderer Vulkan OpenGL resolution DLSS DLAA FSR HDR frame generation")},
        {graphics_advanced_tab.get(), tr("Advanced Graphics"), tr("Video"),
         tr("GPU shaders cache accuracy textures performance asynchronous")},
        {graphics_extensions_tab.get(), tr("Graphics Extensions"), tr("Video"),
         tr("GPU Vulkan extensions drivers compatibility")},
        {audio_tab.get(), tr("Audio"), tr("Devices"), tr("sound volume output latency backend")},
        {input_tab.get(), tr("Controls"), tr("Devices"),
         tr("input controllers gamepad player joystick keyboard mouse microphone mic voice")},
        {network_tab.get(), tr("Network"), tr("Services"),
         tr("internet multiplayer LAN interface")},
        {web_tab.get(), tr("Web"), tr("Services"),
         tr("services telemetry account privacy")},
        {debug_tab_tab.get(), tr("Debug"), tr("Diagnostics"),
         tr("logging diagnostics CPU debugging advanced")},
    });
}

void ConfigureDialog::OnLanguageChanged(const QString& locale) {
    emit LanguageChanged(locale);
    //  Reloading the game list is needed to force retranslation.
    UISettings::values.is_game_list_reload_pending = true;
    // first apply the configuration, and then restore the display
    ApplyConfiguration();
    RetranslateUI();
    SetConfiguration();
    QtCommon::TouchUi::MarkApplied(this);
}
