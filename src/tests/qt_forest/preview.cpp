// SPDX-License-Identifier: GPL-3.0-or-later
// Standalone visual review of real .ui forms. Does not link or start emulation.

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <memory>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QSplitter>
#include <QToolBar>
#include <QTreeWidget>
#include <QUiLoader>
#include <QVBoxLayout>
#include "qt_common/forest_theme.h"
#include "yuzu/configuration/settings_navigation.h"

namespace Forest = QtCommon::ForestTheme;

static QWidget* LoadForm(const QString& path) {
    QFile file(path);
    if (!file.open(QFile::ReadOnly)) {
        return nullptr;
    }
    QUiLoader loader;
    return loader.load(&file);
}

static bool Capture(QWidget& widget, const QString& path) {
    widget.show();
    qApp->processEvents();
    const bool saved = widget.grab().save(path);
    widget.hide();
    return saved;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc != 3) {
        return 2;
    }
    const QString source = QString::fromLocal8Bit(argv[1]);
    const QString output = QString::fromLocal8Bit(argv[2]);
    QDir().mkpath(output);
    for (const QString& theme : {QStringLiteral("dark"), QStringLiteral("light")}) {
        Forest::Apply(QStringLiteral("forest_lantern_") + theme);
        QMainWindow window;
        window.setWindowTitle(QStringLiteral("Lichii-tgm-v1.0.2 · UI preview"));
        for (const QString& menu : {QStringLiteral("File"), QStringLiteral("Emulation"),
                                    QStringLiteral("View"), QStringLiteral("Multiplayer"),
                                    QStringLiteral("Tools"), QStringLiteral("Help")}) {
            window.menuBar()->addMenu(menu);
        }
        auto* toolbar = window.addToolBar(QStringLiteral("Playback"));
        toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        for (const QString& action : {QStringLiteral("Open"), QStringLiteral("Play"),
                                      QStringLiteral("Pause"), QStringLiteral("Stop"),
                                      QStringLiteral("Restart"), QStringLiteral("Fullscreen"),
                                      QStringLiteral("Settings")}) {
            toolbar->addAction(Forest::Icon(action.toLower()), action);
        }
        auto* central = new QWidget(&window);
        auto* layout = new QVBoxLayout(central);
        layout->addWidget(Forest::CreateHeader(central, QStringLiteral("Your library"),
                                             QStringLiteral("A little adventure starts here.")));
        auto* split = new QSplitter(central);
        auto* tree = new QTreeWidget(split);
        tree->setHeaderLabels({QStringLiteral("Game"), QStringLiteral("Format"), QStringLiteral("Play time")});
        // Deliberately no fabricated gameplay statistics or bundled game art.
        auto* item = new QTreeWidgetItem(tree, {QStringLiteral("Your games appear here"), {}, {}});
        item->setToolTip(0, QStringLiteral("Preview placeholder"));
        tree->setColumnWidth(0, 420);
        split->addWidget(tree);
        split->addWidget(new Forest::LibraryPanel(split));
        split->setSizes({740, 340});
        layout->addWidget(split, 1);
        layout->addWidget(Forest::CreateFooter(central));
        window.setCentralWidget(central);
        window.resize(1180, 800);
        if (!Capture(window, output + QStringLiteral("/library-") + theme + QStringLiteral(".png"))) return 3;

        QDialog settings;
        auto* settings_layout = new QVBoxLayout(&settings);
        settings_layout->addWidget(Forest::CreateHeader(&settings, QStringLiteral("Settings"),
                                                       QStringLiteral("Make Lichii-tgm feel at home.")));
        auto* navigation = new ConfigurationShared::SettingsNavigation(&settings);
        const QStringList forms{QStringLiteral("configure_general"), QStringLiteral("configure_ui"),
                                QStringLiteral("configure_hotkeys"), QStringLiteral("configure_system"),
                                QStringLiteral("configure_profile_manager"), QStringLiteral("configure_filesystem"),
                                QStringLiteral("configure_applets"), QStringLiteral("configure_cpu"),
                                QStringLiteral("configure_graphics"), QStringLiteral("configure_graphics_advanced"),
                                QStringLiteral("configure_graphics_extensions"), QStringLiteral("configure_audio"),
                                QStringLiteral("configure_input"), QStringLiteral("configure_network"),
                                QStringLiteral("configure_web"), QStringLiteral("configure_debug")};
        const QStringList titles{QStringLiteral("General"), QStringLiteral("Appearance and Library"),
                                 QStringLiteral("Hotkeys"), QStringLiteral("System"), QStringLiteral("Profiles"),
                                 QStringLiteral("Filesystem"), QStringLiteral("Applets"), QStringLiteral("CPU"),
                                 QStringLiteral("Graphics"), QStringLiteral("Advanced Graphics"),
                                 QStringLiteral("Graphics Extensions"), QStringLiteral("Audio"),
                                 QStringLiteral("Controls"), QStringLiteral("Network"), QStringLiteral("Web"),
                                 QStringLiteral("Debug")};
        std::vector<ConfigurationShared::SettingsNavigation::PageSpec> pages;
        for (int i = 0; i < forms.size(); ++i) {
            auto* page = LoadForm(source + QStringLiteral("/src/yuzu/configuration/") + forms[i] + QStringLiteral(".ui"));
            if (!page) page = new QLabel(QStringLiteral("This page is generated at runtime."));
            const QString group = i < 3 ? QStringLiteral("Application") :
                                  i < 8 ? QStringLiteral("Emulation") :
                                  i < 11 ? QStringLiteral("Video") :
                                  i < 13 ? QStringLiteral("Devices") : QStringLiteral("Services and Diagnostics");
            pages.push_back({page, titles[i], group, {}});
        }
        navigation->SetPages(std::move(pages));
        settings_layout->addWidget(navigation, 1);
        settings_layout->addWidget(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &settings));
        settings.resize(1180, 850);
        if (!Capture(settings, output + QStringLiteral("/settings-") + theme + QStringLiteral(".png"))) return 4;

        const QString gallery = output + QLatin1Char('/') + theme;
        QDir().mkpath(gallery);
        QDirIterator files(source + QStringLiteral("/src/yuzu/configuration"), {QStringLiteral("*.ui")},
                           QDir::Files, QDirIterator::Subdirectories);
        while (files.hasNext()) {
            const QString path = files.next();
            std::unique_ptr<QWidget> form(LoadForm(path));
            if (!form) return 5;
            QDialog dialog;
            dialog.setWindowTitle(QFileInfo(path).baseName());
            auto* form_layout = new QVBoxLayout(&dialog);
            form_layout->addWidget(form.release());
            form_layout->addWidget(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog));
            Forest::PrepareDialog(&dialog);
            dialog.resize(1100, 800);
            if (!Capture(dialog, gallery + QLatin1Char('/') + QFileInfo(path).baseName() + QStringLiteral(".png"))) return 6;
        }
    }
    return 0;
}
