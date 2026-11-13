// SPDX-License-Identifier: GPL-3.0-or-later
#include <chrono>
#include <functional>
#include <limits>
#include <QCheckBox>
#include <QDateTimeEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QFile>
#include <QImage>
#include <QJSEngine>
#include <QTemporaryDir>
#include <QtTest>
#include "common/fs/path_util.h"
#include "common/fs/profile_migration.h"
#include "common/scope_exit.h"
#include "common/settings.h"
#include "common/game_presets.h"
#include "core/core.h"
#include "qt_common/config/qt_config.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/util/game.h"
#include "yuzu/configuration/configure_system.h"
#include "yuzu/configuration/shared_widget.h"
#include "yuzu/configuration/settings_share.h"
#include "yuzu/render/adventure_script.h"

class SystemSettingsTests : public QObject {
    Q_OBJECT
    std::vector<std::function<void()>> restore;
    std::vector<std::unique_ptr<ConfigurationShared::Builder>> builders;
    bool was_global{};

    template<class Setting> void Save(Setting& setting) {
        const bool using_global = setting.UsingGlobal();
        const auto global = setting.GetValue(true);
        setting.SetGlobal(false);
        const auto local = setting.GetValue();
        restore.push_back([&setting, using_global, global, local] {
            setting.SetGlobal(false);
            setting.SetValue(local);
            setting.SetGlobal(true);
            setting.SetValue(global);
            setting.SetGlobal(using_global);
        });
        setting.SetGlobal(true);
    }

    std::unique_ptr<ConfigureSystem> Make(Core::System& system, QWidget& parent) {
        builders.push_back(std::make_unique<ConfigurationShared::Builder>(&parent, true));
        return std::make_unique<ConfigureSystem>(system,
            std::make_shared<std::vector<ConfigurationShared::Tab*>>(), *builders.back(), &parent);
    }

private slots:
    void adventureScriptBoundsNativeReportLoops() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile module(directory.filePath(QStringLiteral("0400486024DEC000.js")));
        QVERIFY(module.open(QIODevice::WriteOnly));
        const QByteArray code = QByteArrayLiteral(R"(
            var game={folder:'0400486024DEC000',file:'save.bin'};
            function read() {
                let rows={length:1000000000};
                let views={length:1000000000};
                for(let i=0;i<64;i++) views[i]={name:'view',rows:rows};
                return {keys:rows,tabs:{length:1000000000,0:{name:'tab',views:views}}};
            }
        )");
        QCOMPARE(module.write(code), qint64{code.size()});
        module.close();
        QFile save(directory.filePath(QStringLiteral("save.bin")));
        QVERIFY(save.open(QIODevice::WriteOnly));
        QCOMPARE(save.write("x"), qint64{1});
        save.close();
        NotesScript script;
        QString error;
        QVERIFY2(script.Load(module.fileName(), {}, error), qPrintable(error));
        const auto report = script.Read(save.fileName(), error);
        QVERIFY2(report.has_value(), qPrintable(error));
        QCOMPARE(report->keys.size(), size_t{4096});
        QCOMPARE(report->tabs.size(), size_t{64});
        QCOMPARE(report->tabs[0].views.size(), size_t{64});
        size_t total = report->keys.size();
        for (const auto& tab : report->tabs)
            for (const auto& view : tab.views) total += view.rows.size();
        QCOMPARE(total, size_t{20000});
    }
    void adventureHostUsesItsStateFolderAndRejectsAnUnassignedSave() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto current = QDir::currentPath();
        SCOPE_EXIT { QDir::setCurrent(current); };
        QVERIFY(QDir::setCurrent(directory.path()));
        QFile unrelated(QStringLiteral("save.bin"));
        QVERIFY(unrelated.open(QIODevice::WriteOnly));
        QCOMPARE(unrelated.write("unrelated working-directory file"), qint64{32});
        unrelated.close();
        QJSEngine engine;
        NotesHost host(&engine, {directory.filePath(QStringLiteral("notes/lib"))});
        QVERIFY(host.file(QStringLiteral("save.bin")).isEmpty());
        QVERIFY(engine.hasError());
        host.store(QStringLiteral("notes.txt"), QStringLiteral("retained state"));
        QFile state(directory.filePath(QStringLiteral("notes/state/notes.txt")));
        QVERIFY(state.open(QIODevice::ReadOnly));
        QCOMPARE(state.readAll(), QByteArrayLiteral("retained state"));
        QCOMPARE(host.load(QStringLiteral("notes.txt")), QStringLiteral("retained state"));
        QVERIFY(!QFile::exists(directory.filePath(QStringLiteral("notes/lib/state/notes.txt"))));
    }
    void adventureTickTimeoutStopsUntilReloaded() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile module(directory.filePath(QStringLiteral("0400486024DEC000.js")));
        const auto write = [&](const QByteArray& tick) {
            if (!module.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
            const QByteArray code = QByteArrayLiteral(
                "var game={folder:'0400486024DEC000',file:'save.bin'};function read(){return {}};") + tick;
            const bool good = module.write(code) == code.size();
            module.close();
            return good;
        };
        QVERIFY(write(QByteArrayLiteral("function tick(){while(true){}}")));
        NotesScript script;
        QString error;
        QVERIFY2(script.Load(module.fileName(), {}, error), qPrintable(error));
        QVERIFY(!script.Tick(error));
        QVERIFY(error.contains(QStringLiteral("over 5 seconds")));
        QElapsedTimer elapsed;
        elapsed.start();
        QVERIFY(!script.Tick(error));
        QVERIFY(elapsed.elapsed() < 1000);
        QVERIFY(write(QByteArrayLiteral("function tick(){return true}")));
        QVERIFY2(script.Load(module.fileName(), {}, error), qPrintable(error));
        QVERIFY(script.Tick(error));
    }
    void gamePromptChoicesMigratePersistAndCanBeCleared() {
        try {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto previous_path = Common::FS::GetEdenPath(Common::FS::EdenPath::ConfigDir);
        const auto previous_root = Common::FS::GetEdenPath(Common::FS::EdenPath::EdenDir);
        const auto previous_skipped = UISettings::values.skipped_recommended_settings;
        const bool players_global = Settings::values.players.UsingGlobal();
        SCOPE_EXIT {
            Common::FS::SetEdenPath(Common::FS::EdenPath::ConfigDir, previous_path);
            Common::FS::SetEdenPath(Common::FS::EdenPath::EdenDir, previous_root);
            UISettings::values.skipped_recommended_settings = previous_skipped;
            Settings::values.players.SetGlobal(players_global);
        };
        QVERIFY(QDir(directory.path()).mkdir(QStringLiteral("profile")));
        Common::FS::SetEdenPath(Common::FS::EdenPath::EdenDir,
                               std::filesystem::path{directory.filePath(QStringLiteral("profile")).toStdString()});
        Common::FS::ProfileMigration::MarkFresh(
            std::filesystem::path{directory.filePath(QStringLiteral("profile.migration")).toStdString()});
        Common::FS::SetEdenPath(Common::FS::EdenPath::ConfigDir,
                               std::filesystem::path{directory.path().toStdString()});
        QFile legacy(directory.filePath(QStringLiteral("preset-choice.ini")));
        QVERIFY(legacy.open(QIODevice::WriteOnly));
        const QByteArray content = QByteArrayLiteral(
            "[UI]\nskip_oot_settings_prompt=true\nskip_mkw_settings_prompt=true\n");
        QCOMPARE(legacy.write(content), qint64{content.size()});
        legacy.close();
        {
            QtConfig config("preset-choice");
            QVERIFY(UISettings::values.skipped_recommended_settings.contains(
                Settings::OcarinaOfTimeProgramId));
            QVERIFY(UISettings::values.skipped_recommended_settings.contains(
                Settings::MarioKartWorldProgramId));
            UISettings::values.skipped_recommended_settings.erase(Settings::OcarinaOfTimeProgramId);
            config.SaveAllValues();
        }
        {
            QtConfig config("preset-choice");
            QVERIFY(!UISettings::values.skipped_recommended_settings.contains(
                Settings::OcarinaOfTimeProgramId));
            QVERIFY(UISettings::values.skipped_recommended_settings.contains(
                Settings::MarioKartWorldProgramId));
            UISettings::values.skipped_recommended_settings.clear();
            config.SaveAllValues();
        }
        {
            QtConfig config("preset-choice");
            QVERIFY(UISettings::values.skipped_recommended_settings.empty());
        }
        } catch (const std::exception& error) {
            QFAIL(error.what());
        }
    }
    void sharingCodesPreserveValuesAndRejectDamagedPayloads() {
        const std::vector<SettingsShare::Entry> entries{
            {"volume", "42"}, {"label", "space ; equal= and Unicode: \xc3\xa9"}};
        QString error;
        const auto code = SettingsShare::Encode(QStringLiteral("aud"), entries);
        const auto decoded = SettingsShare::Decode(code, error);
        QVERIFY(decoded);
        QCOMPARE(decoded->section, QStringLiteral("aud"));
        QCOMPARE(decoded->entries.size(), entries.size());
        for (size_t i = 0; i < entries.size(); ++i) {
            QCOMPARE(decoded->entries[i].key, entries[i].key);
            QCOMPARE(decoded->entries[i].value, entries[i].value);
        }
        const auto original = Settings::values.volume.ToString();
        for (const auto& invalid : std::vector<QString>{
                 QStringLiteral("LX99.aud.volume=1"), QStringLiteral("LX1.aud.broken"),
                 QStringLiteral("LX1z.aud.////"),
                 QStringLiteral("LX1z.aud.") + QString::fromLatin1(QByteArray::fromHex("7fffffff").toBase64()),
                 QString(300000, QLatin1Char('x'))}) {
            QVERIFY(!SettingsShare::Decode(invalid, error));
        }
        QCOMPARE(Settings::values.volume.ToString(), original);
        std::vector<SettingsShare::Entry> repeated;
        for (int i = 0; i < 100; ++i) repeated.push_back({"option_" + std::to_string(i), "repeated-value"});
        const auto packed = SettingsShare::Encode(QStringLiteral("gfx"), repeated);
        QVERIFY(packed.startsWith(QStringLiteral("LX1z.")));
        const auto unpacked = SettingsShare::Decode(packed, error);
        QVERIFY(unpacked);
        QCOMPARE(unpacked->entries.size(), repeated.size());
    }
    void customLibraryMetadataDoesNotRewriteTheGame() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto previous = Common::FS::GetEdenPath(Common::FS::EdenPath::EdenDir);
        SCOPE_EXIT { Common::FS::SetEdenPath(Common::FS::EdenPath::EdenDir, previous); };
        Common::FS::SetEdenPath(Common::FS::EdenPath::EdenDir,
                               std::filesystem::path{directory.path().toStdString()});
        QFile game(directory.filePath(QStringLiteral("game.nup")));
        const QByteArray original = QByteArrayLiteral("synthetic read-only game contents");
        QVERIFY(game.open(QIODevice::WriteOnly));
        QCOMPARE(game.write(original), qint64{original.size()});
        game.close();
        constexpr u64 program = 0x0400486024DEC000;
        QImage artwork(32, 16, QImage::Format_ARGB32);
        artwork.fill(Qt::green);
        QVERIFY(QtCommon::Game::SaveCustomArtwork(program, artwork));
        QVERIFY(QtCommon::Game::SaveCustomTitle(program, "  My  OOT game  "));
        QCOMPARE(QtCommon::Game::ReadCustomTitle(program), std::string{"My OOT game"});
        QVERIFY(QtCommon::Game::ReadCustomTitle(program + 1).empty());
        const auto png = QtCommon::Game::ReadCustomArtwork(program);
        QVERIFY(!png.empty());
        QCOMPARE(QImage::fromData(png.data(), static_cast<int>(png.size())).size(), artwork.size());
        QVERIFY(QtCommon::Game::RemoveCustomArtwork(program));
        QVERIFY(QtCommon::Game::SaveCustomTitle(program, ""));
        QVERIFY(QtCommon::Game::ReadCustomArtwork(program).empty());
        QVERIFY(QtCommon::Game::ReadCustomTitle(program).empty());
        QVERIFY(game.open(QIODevice::ReadOnly));
        QCOMPARE(game.readAll(), original);
    }
    void init() {
        was_global = Settings::IsConfiguringGlobal();
        Settings::SetConfiguringGlobal(true);
        Save(Settings::values.custom_rtc);
        Save(Settings::values.custom_rtc_offset);
        Save(Settings::values.custom_rtc_enabled);
        Settings::values.custom_rtc_enabled.SetValue(true);
        Settings::values.custom_rtc_offset.SetValue(0);
    }
    void cleanup() {
        for (auto& action : restore) action();
        restore.clear();
        builders.clear();
        Settings::SetConfiguringGlobal(was_global);
    }
    void numericOffsetRetainsFullRange() {
        Core::System system;
        QWidget parent;
        for (const s64 value : {s64{5'000'000'000}, s64{-5'000'000'000},
                               (std::numeric_limits<s64>::min)(), (std::numeric_limits<s64>::max)()}) {
            Settings::values.custom_rtc_offset.SetValue(value);
            auto form = Make(system, parent);
            auto* edit = form->findChild<QLineEdit*>(QStringLiteral("customRtcOffsetSeconds"));
            QVERIFY(edit);
            QCOMPARE(edit->text(), QString::number(value));
            form->ApplyConfiguration();
            QCOMPARE(Settings::values.custom_rtc_offset.GetValue(), value);
            edit->setText(QStringLiteral("9223372036854775808"));
            QVERIFY(QMetaObject::invokeMethod(edit, "editingFinished", Qt::DirectConnection));
            form->ApplyConfiguration();
            QCOMPARE(Settings::values.custom_rtc_offset.GetValue(), value);
        }
    }
    void dateEditKeepsOffsetWithoutFeedback() {
        Core::System system;
        QWidget parent;
        auto form = Make(system, parent);
        auto* date = form->findChild<QDateTimeEdit*>(QStringLiteral("customRtcDate"));
        QVERIFY(date);
        const auto target = QDateTime::currentSecsSinceEpoch() - 123456;
        date->setDateTime(QDateTime::fromSecsSinceEpoch(target));
        form->ApplyConfiguration();
        QVERIFY(std::abs(Settings::values.custom_rtc_offset.GetValue() + 123456) <= 2);
        QCOMPARE(date->dateTime().toSecsSinceEpoch(), target);
    }
    void perGameRestoreRestoresValueAndEnableState() {
        Settings::values.custom_rtc_enabled.SetValue(false);
        Settings::values.custom_rtc_offset.SetValue(123);
        Settings::values.custom_rtc_offset.SetGlobal(false);
        Settings::values.custom_rtc_offset.SetValue(5'000'000'000LL);
        Settings::values.custom_rtc_enabled.SetGlobal(false);
        Settings::values.custom_rtc_enabled.SetValue(true);
        Settings::values.custom_rtc.SetGlobal(false);
        Settings::SetConfiguringGlobal(false);
        Core::System system;
        QWidget parent;
        auto form = Make(system, parent);
        auto* edit = form->findChild<QLineEdit*>(QStringLiteral("customRtcOffsetSeconds"));
        QVERIFY(edit);
        QCOMPARE(edit->text(), QStringLiteral("5000000000"));
        auto* button = edit->parentWidget()->findChild<QPushButton*>();
        QVERIFY(button);
        QVERIFY(button->isEnabled());
        button->click();
        form->ApplyConfiguration();
        QVERIFY(Settings::values.custom_rtc_offset.UsingGlobal());
        QVERIFY(Settings::values.custom_rtc_enabled.UsingGlobal());
        QCOMPARE(Settings::values.custom_rtc_offset.GetValue(), s64{123});
        QVERIFY(!Settings::values.custom_rtc_enabled.GetValue());
    }
    void viewingPerGameDoesNotCreateAnOverride() {
        Settings::values.custom_rtc_offset.SetValue(5'000'000'000LL);
        Settings::SetConfiguringGlobal(false);
        Core::System system;
        QWidget parent;
        auto form = Make(system, parent);
        form->ApplyConfiguration();
        QVERIFY(Settings::values.custom_rtc_offset.UsingGlobal());
        QCOMPARE(Settings::values.custom_rtc_offset.GetValue(), s64{5'000'000'000});
    }
};
QTEST_MAIN(SystemSettingsTests)
#include "system_settings.moc"
