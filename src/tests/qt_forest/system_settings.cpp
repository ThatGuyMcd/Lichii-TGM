// SPDX-License-Identifier: GPL-3.0-or-later
#include <chrono>
#include <functional>
#include <limits>
#include <QCheckBox>
#include <QDateTimeEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QtTest>
#include "common/settings.h"
#include "core/core.h"
#include "yuzu/configuration/configure_system.h"
#include "yuzu/configuration/shared_widget.h"

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
