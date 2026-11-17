// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QKeyEvent>
#include <QMenu>
#include <QPushButton>
#include <QQuickWidget>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTemporaryDir>
#include <QTest>
#include "qt_common/abstract/frontend.h"
#include "qt_common/game_list/game_list_p.h"
#include "yuzu/couch/configuration_snapshot.h"
#include "yuzu/couch/couch_mode.h"

// The standalone shell must never invoke an emulator operation.
QtCommon::Frontend::StandardButton QtCommon::Frontend::ShowMessage(Icon, const QString&,
                                                                   const QString&, StandardButtons,
                                                                   QObject*) {
    qFatal("An emulator operation escaped the standalone Couch test.");
    return Cancel;
}

class ShellTests final : public QObject {
    Q_OBJECT
private slots:
    void transactionsAndLeaveGuard() {
        QTemporaryDir directory;
        QStandardItemModel library;
        bool committed = false;
        CouchMode shell(&library, directory.path(), [&](const QString&, const QString&) {
            CouchSettingsSource source;
            source.dialog = std::make_unique<QDialog>();
            auto* layout = new QFormLayout(source.dialog.get());
            auto* check = new QCheckBox(QStringLiteral("Enable option"), source.dialog.get());
            check->setChecked(committed);
            layout->addRow(check);
            source.pages = {{source.dialog.get(), QStringLiteral("general"),
                             QStringLiteral("General"), QStringLiteral("Application")}};
            source.apply = [&committed, check] {
                committed = check->isChecked();
                return true;
            };
            return source;
        });
        QCOMPARE(shell.status(), QQuickWidget::Ready);
        shell.action(QStringLiteral("tab"), QStringLiteral("settings"));
        const auto row = shell.Settings()->Rows().front().toMap();
        const int id = row.value(QStringLiteral("id")).toInt();
        QVERIFY(shell.stage(id, true));
        QVERIFY(shell.State().value(QStringLiteral("dirty")).toBool());
        shell.action(QStringLiteral("tab"), QStringLiteral("library"));
        QCOMPARE(shell.State().value(QStringLiteral("tab")).toString(), QStringLiteral("settings"));
        QCOMPARE(shell.State()
                     .value(QStringLiteral("modal"))
                     .toMap()
                     .value(QStringLiteral("kind"))
                     .toString(),
                 QStringLiteral("unsaved"));
        shell.closeModal();
        QVERIFY(shell.Settings()->Dirty());
        QVERIFY(!committed);
        shell.action(QStringLiteral("tab"), QStringLiteral("library"));
        shell.action(QStringLiteral("leave-discard"));
        QVERIFY(!committed);
        QCOMPARE(shell.State().value(QStringLiteral("tab")).toString(), QStringLiteral("library"));
        shell.action(QStringLiteral("tab"), QStringLiteral("settings"));
        QVERIFY(shell.stage(0, true));
        shell.action(QStringLiteral("tab"), QStringLiteral("home"));
        shell.action(QStringLiteral("leave-save"));
        QVERIFY(committed);
        QCOMPARE(shell.State().value(QStringLiteral("tab")).toString(), QStringLiteral("home"));
        shell.action(QStringLiteral("tab"), QStringLiteral("settings"));
        QVERIFY(shell.stage(0, false));
        QSignalSpy close(&shell, &CouchMode::CloseApproved);
        QVERIFY(!shell.RequestClose());
        shell.action(QStringLiteral("leave-discard"));
        QCOMPARE(close.size(), 1);
    }
    void failedSaveKeepsDraft() {
        QTemporaryDir directory;
        QStandardItemModel library;
        CouchMode shell(&library, directory.path(), [](const QString&, const QString&) {
            CouchSettingsSource source;
            source.dialog = std::make_unique<QDialog>();
            auto* layout = new QFormLayout(source.dialog.get());
            layout->addRow(new QCheckBox(QStringLiteral("Option"), source.dialog.get()));
            source.pages = {{source.dialog.get(), QStringLiteral("general"),
                             QStringLiteral("General"), QStringLiteral("Application")}};
            source.apply = [] { return false; };
            return source;
        });
        shell.action(QStringLiteral("tab"), QStringLiteral("settings"));
        QVERIFY(shell.stage(0, true));
        shell.action(QStringLiteral("tab"), QStringLiteral("home"));
        shell.action(QStringLiteral("leave-save"));
        QCOMPARE(shell.State().value(QStringLiteral("tab")).toString(), QStringLiteral("settings"));
        QVERIFY(shell.Settings()->Dirty());
        QCOMPARE(shell.State()
                     .value(QStringLiteral("modal"))
                     .toMap()
                     .value(QStringLiteral("kind"))
                     .toString(),
                 QStringLiteral("unsaved"));
    }
    void nativeMenuAndLibrary() {
        QTemporaryDir directory;
        QStandardItemModel library;
        auto* item = new QStandardItem(QStringLiteral("Test title"));
        item->setData(static_cast<int>(GameListItemType::Game), GameListItem::TypeRole);
        item->setData(QStringLiteral("Test title"), GameListItemPath::TitleRole);
        item->setData(QStringLiteral("/test/title.nup"), GameListItemPath::FullPathRole);
        item->setData(QVariant::fromValue<qulonglong>(123), GameListItemPath::ProgramIdRole);
        library.appendRow(item);
        int chosen = 0;
        CouchMode shell(&library, directory.path(), [&](const QString&, const QString&) {
            CouchSettingsSource source;
            source.dialog = std::make_unique<QDialog>();
            auto* layout = new QFormLayout(source.dialog.get());
            auto* button = new QPushButton(QStringLiteral("More options"), source.dialog.get());
            auto* menu = new QMenu(button);
            menu->addAction(QStringLiteral("First"), [&] { chosen = 1; });
            menu->addAction(QStringLiteral("Second"), [&] { chosen = 2; });
            button->setMenu(menu);
            layout->addRow(button);
            source.pages = {{source.dialog.get(), QStringLiteral("general"),
                             QStringLiteral("General"), QStringLiteral("Application")}};
            return source;
        });
        QCOMPARE(shell.Games().size(), 1);
        QCOMPARE(shell.Games().front().toMap().value(QStringLiteral("name")).toString(),
                 QStringLiteral("Test title"));
        QSignalSpy launch(&shell, &CouchMode::LaunchGame);
        shell.action(QStringLiteral("play"));
        QCOMPARE(launch.size(), 1);
        QCOMPARE(launch.front().front().toString(), QStringLiteral("/test/title.nup"));
        shell.resize(1280, 800);
        shell.show();
        shell.SetActive(true);
        shell.action(QStringLiteral("tab"), QStringLiteral("settings"));
        QTest::qWait(20);
        QTimer::singleShot(120, &shell, [&] { shell.action(QStringLiteral("modal-choice"), 1); });
        shell.edit(0);
        QTRY_COMPARE_WITH_TIMEOUT(chosen, 2, 1500);
        shell.SetActive(false);
    }
    void nativeDialogInputOwnership() {
        QTemporaryDir directory;
        QStandardItemModel library;
        int outcome = -1;
        bool close_allowed = true, quick_found = false, fallback = false;
        CouchMode* shell_ptr = nullptr;
        CouchMode shell(&library, directory.path(), [&](const QString&, const QString&) {
            CouchSettingsSource source;
            source.dialog = std::make_unique<QDialog>();
            auto* owner = source.dialog.get();
            auto* layout = new QFormLayout(owner);
            auto* button = new QPushButton(QStringLiteral("Open native dialog"), owner);
            layout->addRow(button);
            connect(button, &QPushButton::clicked, owner, [&, owner] {
                QDialog dialog(owner);
                dialog.setWindowTitle(QStringLiteral("Native confirmation"));
                auto* body = new QFormLayout(&dialog);
                auto* buttons =
                    new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
                body->addRow(buttons);
                connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
                connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
                QTimer::singleShot(100, &dialog, [&dialog] {
                    QKeyEvent legacy(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
                    QCoreApplication::sendEvent(&dialog, &legacy);
                });
                QTimer::singleShot(180, &dialog,
                                   [&] { close_allowed = shell_ptr->RequestClose(); });
                QTimer::singleShot(260, &dialog, [&] {
                    if (auto* quick = dialog.findChild<QQuickWidget*>(
                            QStringLiteral("CouchNativePresentation"))) {
                        quick_found = true;
                        QTest::keyClick(quick, Qt::Key_Escape);
                    }
                });
                QTimer::singleShot(500, &dialog, [&] {
                    fallback = true;
                    dialog.reject();
                });
                outcome = dialog.exec();
            });
            source.pages = {{owner, QStringLiteral("general"), QStringLiteral("General"),
                             QStringLiteral("Application")}};
            return source;
        });
        shell_ptr = &shell;
        shell.resize(1280, 800);
        shell.show();
        shell.SetActive(true);
        shell.action(QStringLiteral("tab"), QStringLiteral("settings"));
        shell.hide();
        shell.edit(0);
        QCOMPARE(outcome, int(QDialog::Rejected));
        QVERIFY(quick_found);
        QVERIFY(!fallback);
        QVERIFY(!close_allowed);
        shell.SetActive(false);
    }
    void nativeSnapshot() {
        CouchConfigurationSnapshot snapshot;
        const auto volume = Settings::values.volume.GetValue();
        Settings::values.volume.SetValue(volume > 1 ? volume - 1 : volume + 1);
        QVERIFY(snapshot.Changed());
        snapshot.Restore();
        QVERIFY(!snapshot.Changed());
        Settings::values.touch_from_button_maps.push_back({"Test", {}});
        QVERIFY(snapshot.Changed());
        snapshot.Restore();
        QVERIFY(!snapshot.Changed());
    }
    void unicodeKeyboardEdits() {
        QTemporaryDir directory;
        QStandardItemModel library;
        CouchMode shell(&library, directory.path(),
                        [](const QString&, const QString&) { return CouchSettingsSource{}; });
        const auto text = QString::fromUtf8("A👨‍👩‍👧‍👦é");
        auto result = shell.editText(text, text.size() - 1, QStringLiteral("backspace"));
        QCOMPARE(result.value(QStringLiteral("text")).toString(), QString::fromUtf8("Aé"));
        QCOMPARE(result.value(QStringLiteral("caret")).toInt(), 1);
        result = shell.editText(QStringLiteral("AB"), 1, QStringLiteral("insert"),
                                QStringLiteral("12!"));
        QCOMPARE(result.value(QStringLiteral("text")).toString(), QStringLiteral("A12!B"));
    }
};
QTEST_MAIN(ShellTests)
#include "couch_shell.moc"
