// SPDX-License-Identifier: GPL-3.0-or-later
#include <memory>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QInputDialog>
#include <QIntValidator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWidget>
#include <QSignalSpy>
#include <QSettings>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTemporaryDir>
#include <QTest>
#include "qt_common/couch/native_dialog.h"
#include "qt_common/couch/theme.h"
#include "qt_common/couch/widget_model.h"
#include "qt_common/couch/welcome.h"

static void InitializeCouchResources() {
    Q_INIT_RESOURCE(couch);
}

namespace {
using QtCommon::Couch::WidgetModel;
class Harness final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap state READ State NOTIFY stateChanged)
    Q_PROPERTY(QVariantList games READ Games NOTIFY gamesChanged)
    Q_PROPERTY(QVariantList collections READ Games NOTIFY gamesChanged)
    Q_PROPERTY(QtCommon::Couch::WidgetModel* settings READ Settings CONSTANT)
    Q_PROPERTY(QtCommon::Couch::Theme* theme READ Theme CONSTANT)
public:
    Harness() : settings(this), theme(QString{}, this) {
        page.setObjectName(QStringLiteral("CouchTestPage"));
        auto* layout = new QFormLayout(&page);
        for (int i = 0; i < 35; ++i) {
            auto* spin = new QSpinBox(&page);
            spin->setObjectName(QStringLiteral("option_%1").arg(i));
            spin->setRange(0, 200);
            layout->addRow(QStringLiteral("Option %1").arg(i), spin);
        }
        auto* date = new QDateTimeEdit(&page);
        date->setObjectName(QStringLiteral("clock"));
        date->setDateTime(QDateTime(QDate(2028, 2, 29), QTime(12, 0)));
        date->setDateRange(QDate(2000, 1, 1), QDate(2100, 12, 31));
        layout->addRow(QStringLiteral("Date and time"), date);
        std::vector<QtCommon::Couch::SettingsPage> pages{{&page, QStringLiteral("general"),
                                                          QStringLiteral("General"),
                                                          QStringLiteral("Application")}};
        for (int i = 0; i < 15; ++i) {
            auto extra = std::make_unique<QWidget>();
            auto* form = new QFormLayout(extra.get());
            form->addRow(new QCheckBox(QStringLiteral("Extra option"), extra.get()));
            pages.push_back({extra.get(), QStringLiteral("extra-%1").arg(i),
                             QStringLiteral("Extra section %1").arg(i),
                             QStringLiteral("Application")});
            extra_pages.push_back(std::move(extra));
        }
        settings.SetPages(pages);
        settings.Select(QStringLiteral("general"), 0);
        ui = {{QStringLiteral("tab"), QStringLiteral("settings")},
              {QStringLiteral("section"), QStringLiteral("general")},
              {QStringLiteral("group"), QStringLiteral("Application")},
              {QStringLiteral("player"), 0},
              {QStringLiteral("scope"), QStringLiteral("global")},
              {QStringLiteral("version"), QStringLiteral("test")},
              {QStringLiteral("modal"), QVariantMap{}},
              {QStringLiteral("textScale"), 1.0},
              {QStringLiteral("prompts"), QStringLiteral("xbox")}};
        connect(&settings, &WidgetModel::changed, this, &Harness::stateChanged);
    }
    QVariantMap State() const {
        auto result = ui;
        result[QStringLiteral("dirty")] = settings.Dirty();
        result[QStringLiteral("changeCount")] = settings.Changes().size();
        return result;
    }
    QVariantList Games() const {
        return {};
    }
    WidgetModel* Settings() {
        return &settings;
    }
    QtCommon::Couch::Theme* Theme() {
        return &theme;
    }
    Q_INVOKABLE void action(const QString& name, const QVariant& value = {}) {
        if (name == QStringLiteral("tab"))
            ui[QStringLiteral("tab")] = value;
        else if (name == QStringLiteral("section")) {
            ui[QStringLiteral("section")] = value;
            settings.Select(value.toString(), 0);
        } else if (name == QStringLiteral("group"))
            ui[QStringLiteral("group")] = value;
        else if (name == QStringLiteral("save"))
            settings.MarkSaved();
        emit stateChanged();
    }
    Q_INVOKABLE void edit(int id) {
        settings.Activate(id);
    }
    Q_INVOKABLE void chooseTheme(const QString& id) {
        theme.Load(id);
    }
    Q_INVOKABLE void closeModal() {
        ui[QStringLiteral("modal")] = QVariantMap{};
        emit stateChanged();
    }
    Q_INVOKABLE void completeText(const QString&, const QString&) {
        closeModal();
    }
    Q_INVOKABLE QString clipboardText() const {
        return {};
    }
    Q_INVOKABLE QVariantMap editText(const QString& text, int caret, const QString&,
                                     const QString&) const {
        return {{QStringLiteral("text"), text}, {QStringLiteral("caret"), caret}};
    }
    Q_INVOKABLE void save() {
        settings.MarkSaved();
    }
    Q_INVOKABLE void treeAction(int, int, bool) {}
signals:
    void stateChanged();
    void gamesChanged();
    void input(const QString& action);

public:
    QWidget page;
    WidgetModel settings;
    QtCommon::Couch::Theme theme;
    QVariantMap ui;
    std::vector<std::unique_ptr<QWidget>> extra_pages;
};

class CouchTests final : public QObject {
    Q_OBJECT
private slots:
    void invitationChoicesPersistOnlyOnAcceptanceOrNever() {
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("invitation.ini"));
        QSettings saved(path, QSettings::IniFormat);
        int writes = 0;
        const auto remember = [&] {
            ++writes;
            saved.setValue(QStringLiteral("dismissed"), true);
            saved.sync();
        };
        {
            QtCommon::Couch::WelcomeDialog dialog(remember);
            dialog.show();
            QTest::mouseClick(dialog.findChild<QPushButton*>(QStringLiteral("couchWelcomeLater")),
                              Qt::LeftButton);
            QCOMPARE(dialog.result(), int(QDialog::Rejected));
            QCOMPARE(writes, 0);
            QVERIFY(!saved.value(QStringLiteral("dismissed"), false).toBool());
        }
        {
            QtCommon::Couch::WelcomeDialog dialog(remember);
            QSignalSpy accepted(&dialog, &QDialog::accepted);
            dialog.show();
            QTest::mouseClick(dialog.findChild<QPushButton*>(QStringLiteral("couchWelcomeNow")),
                              Qt::LeftButton);
            QCOMPARE(accepted.count(), 1);
            QCOMPARE(writes, 1);
            QVERIFY(saved.value(QStringLiteral("dismissed"), false).toBool());
        }
        saved.remove(QStringLiteral("dismissed"));
        {
            QtCommon::Couch::WelcomeDialog dialog(remember);
            dialog.show();
            auto* never = dialog.findChild<QCheckBox*>(QStringLiteral("couchWelcomeNeverAsk"));
            never->setFocus();
            dialog.Navigate(Qt::Key_Return);
            QVERIFY(never->isChecked());
            QVERIFY(dialog.isVisible());
            QTest::mouseClick(dialog.findChild<QPushButton*>(QStringLiteral("couchWelcomeLater")),
                              Qt::LeftButton);
            QCOMPARE(writes, 2);
        }
        QSettings reopened(path, QSettings::IniFormat);
        QVERIFY(reopened.value(QStringLiteral("dismissed"), false).toBool());
        {
            QtCommon::Couch::WelcomeDialog dialog(remember);
            dialog.show();
            dialog.close();
            QCOMPARE(writes, 2);
        }
    }

    void realWidgetValidation() {
        QWidget root;
        QFormLayout layout(&root);
        auto* spin = new QSpinBox(&root);
        spin->setRange(1, 120);
        spin->setValue(60);
        layout.addRow(QStringLiteral("Frame rate"), spin);
        auto* line = new QLineEdit(&root);
        line->setValidator(new QIntValidator(10, 99, line));
        line->setText(QStringLiteral("20"));
        layout.addRow(QStringLiteral("Numeric text"), line);
        auto* check = new QCheckBox(QStringLiteral("Enabled"), &root);
        layout.addRow(check);
        auto* combo = new QComboBox(&root);
        combo->addItems({QStringLiteral("One"), QStringLiteral("Two")});
        layout.addRow(QStringLiteral("Choice"), combo);
        WidgetModel model;
        model.SetPages({{&root, QStringLiteral("general"), QStringLiteral("General"),
                         QStringLiteral("Application")}});
        model.Select(QStringLiteral("general"), 0);
        QCOMPARE(model.Rows().size(), 4);
        QVERIFY(!model.Dirty());
        int spin_id = -1, line_id = -1, check_id = -1, combo_id = -1;
        for (const auto& item : model.Rows()) {
            const auto data = item.toMap();
            const int id = data.value(QStringLiteral("id")).toInt();
            if (model.Control(id) == spin)
                spin_id = id;
            else if (model.Control(id) == line)
                line_id = id;
            else if (model.Control(id) == check)
                check_id = id;
            else if (model.Control(id) == combo)
                combo_id = id;
        }
        QVERIFY(!model.Stage(spin_id, 121));
        QCOMPARE(spin->value(), 60);
        QVERIFY(!model.Stage(spin_id, 3.5));
        QVERIFY(model.Stage(spin_id, 120));
        QVERIFY(model.Dirty());
        QCOMPARE(spin->value(), 120);
        QVERIFY(!model.Stage(line_id, QStringLiteral("abc")));
        QCOMPARE(line->text(), QStringLiteral("20"));
        QVERIFY(model.Stage(line_id, QStringLiteral("42")));
        QSignalSpy clicked(check, &QCheckBox::clicked);
        QVERIFY(model.Stage(check_id, true));
        QCOMPARE(clicked.size(), 1);
        QVERIFY(!model.Stage(combo_id, 4));
        QVERIFY(model.Stage(combo_id, 1));
        QCOMPARE(combo->currentText(), QStringLiteral("Two"));
        model.MarkSaved();
        QVERIFY(!model.Dirty());
        model.Select(QStringLiteral("general"), 0, QStringLiteral("Frame rate"));
        QCOMPARE(model.Rows().size(), 1);
    }
    void nativeSecretsAndDates() {
        QWidget root;
        QFormLayout layout(&root);
        auto* secret = new QLineEdit(&root);
        secret->setEchoMode(QLineEdit::Password);
        secret->setText(QStringLiteral("hidden"));
        layout.addRow(QStringLiteral("Password"), secret);
        auto* date = new QDateTimeEdit(&root);
        date->setDateRange(QDate(2000, 1, 1), QDate(2100, 12, 31));
        date->setDateTime(QDateTime(QDate(2028, 2, 29), QTime(12, 0)));
        layout.addRow(QStringLiteral("Clock"), date);
        WidgetModel model;
        model.SetPages({{&root, QStringLiteral("general"), QStringLiteral("General"),
                         QStringLiteral("Application")}});
        model.Select(QStringLiteral("general"), 0);
        for (const auto& value : model.Rows()) {
            const auto item = value.toMap();
            const int id = item.value(QStringLiteral("id")).toInt();
            if (model.Control(id) == secret) {
                QVERIFY(item.value(QStringLiteral("secret")).toBool());
                QVERIFY(!item.value(QStringLiteral("display"))
                             .toString()
                             .contains(QStringLiteral("hidden")));
            } else {
                QVERIFY(!model.Stage(id, QStringLiteral("2028-02-30T12:00:00.000")));
                QVERIFY(model.Stage(id, QStringLiteral("2028-02-29T23:59:00.000")));
            }
        }
    }
    void themeValidation() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVariantMap output;
        QString error;
        QByteArray valid =
            R"({"version":1,"id":"test-theme","name":"Test","colours":{"focus":"#ff3344"},"metrics":{"rowHeight":96},"layers":[]})";
        QVERIFY(QtCommon::Couch::Theme::Validate(valid, directory.path(), &output, &error));
        QCOMPARE(output.value(QStringLiteral("id")).toString(), QStringLiteral("test-theme"));
        QVERIFY(!QtCommon::Couch::Theme::Validate(R"({"version":1,"id":"../escape"})",
                                                  directory.path(), &output));
        const QByteArray remote_theme =
            R"({"version":1,"id":"test","assets":{"background":"https://example.com/a.png"}})";
        QVERIFY(!QtCommon::Couch::Theme::Validate(remote_theme, directory.path(), &output));
        QVERIFY(!QtCommon::Couch::Theme::Validate(
            R"({"version":1,"id":"test","metrics":{"rowHeight":2}})", directory.path(), &output));
        QVERIFY(!QtCommon::Couch::Theme::Validate(
            R"({"version":1,"id":"test","layers":[{"type":"script","x":0,"y":0,"width":1,"height":1}]})",
            directory.path(), &output));
        QtCommon::Couch::Theme theme(directory.path());
        QVERIFY(theme.Load(QStringLiteral("horizon-light")));
        QCOMPARE(theme.Data().value(QStringLiteral("id")).toString(),
                 QStringLiteral("horizon-light"));
    }
    void nativeSubdialogs() {
        InitializeCouchResources();
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        QFile content(folder.filePath(QStringLiteral("test.json")));
        QVERIFY(content.open(QIODevice::WriteOnly));
        content.write("{}");
        content.close();
        QtCommon::Couch::Theme theme(folder.path());
        QFileDialog dialog;
        dialog.setOption(QFileDialog::DontUseNativeDialog);
        dialog.setDirectory(folder.path());
        dialog.setNameFilter(QStringLiteral("JSON (*.json)"));
        dialog.setFileMode(QFileDialog::ExistingFiles);
        QtCommon::Couch::NativeDialog native(&dialog, &theme);
        QVERIFY(native.State().value(QStringLiteral("files")).toList().size() >= 1);
        native.action(QStringLiteral("file"), 0);
        QCOMPARE(native.State().value(QStringLiteral("selected")).toStringList().size(), 1);
        native.action(QStringLiteral("file"), 0);
        QCOMPARE(native.State().value(QStringLiteral("selected")).toStringList().size(), 0);
        Harness harness;
        QQuickWidget view;
        view.rootContext()->setContextProperty(QStringLiteral("Couch"), &harness);
        view.rootContext()->setContextProperty(QStringLiteral("Native"), &native);
        view.setResizeMode(QQuickWidget::SizeRootObjectToView);
        view.resize(1280, 800);
        view.setSource(QUrl(QStringLiteral("qrc:/couch/NativeDialog.qml")));
        for (const auto& error : view.errors())
            qWarning() << error.toString();
        QCOMPARE(view.status(), QQuickWidget::Ready);
        view.show();
        QTest::qWait(40);
        QInputDialog input;
        input.setLabelText(QStringLiteral("Paste settings code"));
        input.setTextValue(QStringLiteral("LX1."));
        QtCommon::Couch::NativeDialog input_native(&input, &theme);
        QVERIFY(!input_native.Settings()->Rows().isEmpty());
        QMessageBox message(QMessageBox::Warning, QStringLiteral("Confirm"),
                            QStringLiteral("Save changes?"),
                            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        message.setDefaultButton(QMessageBox::Cancel);
        QtCommon::Couch::NativeDialog confirmation(&message, &theme);
        bool default_cancel = false;
        for (const auto& button : confirmation.State().value(QStringLiteral("buttons")).toList()) {
            const auto row = button.toMap();
            if (row.value(QStringLiteral("default")).toBool())
                default_cancel = row.value(QStringLiteral("label"))
                                     .toString()
                                     .contains(QStringLiteral("Cancel"));
        }
        QVERIFY(default_cancel);
        dialog.setDirectory(QDir::homePath());
        QTest::qWait(30);
    }
    void controllerEditorsAndKeyboard() {
        InitializeCouchResources();
        Harness harness;
        QQuickWidget view;
        view.setResizeMode(QQuickWidget::SizeRootObjectToView);
        view.rootContext()->setContextProperty(QStringLiteral("Couch"), &harness);
        view.resize(1920, 1080);
        view.setSource(QUrl(QStringLiteral("qrc:/couch/Horizon.qml")));
        QCOMPARE(view.status(), QQuickWidget::Ready);
        view.show();
        QTest::qWait(30);
        auto* editor =
            view.rootObject()->findChild<QQuickItem*>(QStringLiteral("CouchSettingEditor"));
        QVERIFY(editor);
        const auto send = [](QObject* target, const QString& action) {
            return QMetaObject::invokeMethod(target, "controller",
                                             Q_ARG(QVariant, QVariant(action)));
        };
        QVERIFY(QMetaObject::invokeMethod(editor, "open",
                                          Q_ARG(QVariant, QVariant::fromValue(harness.Settings())),
                                          Q_ARG(QVariant, QVariant(harness.settings.Describe(0)))));
        QTest::qWait(20);
        QVERIFY(send(editor, QStringLiteral("right")));
        QVERIFY(send(editor, QStringLiteral("right")));
        QVERIFY(send(editor, QStringLiteral("accept")));
        QVERIFY(send(editor, QStringLiteral("accept")));
        QCOMPARE(harness.settings.Describe(0).value(QStringLiteral("value")).toInt(), 2);
        int date_id = -1;
        for (const auto& item : harness.settings.Rows())
            if (item.toMap().value(QStringLiteral("kind")) == QStringLiteral("date"))
                date_id = item.toMap().value(QStringLiteral("id")).toInt();
        QVERIFY(date_id >= 0);
        QVERIFY(QMetaObject::invokeMethod(
            editor, "open", Q_ARG(QVariant, QVariant::fromValue(harness.Settings())),
            Q_ARG(QVariant, QVariant(harness.settings.Describe(date_id)))));
        QTest::qWait(20);
        QVERIFY(send(editor, QStringLiteral("up")));
        QVERIFY(send(editor, QStringLiteral("accept")));
        QVERIFY(send(editor, QStringLiteral("accept")));
        QVERIFY(harness.settings.Describe(date_id)
                    .value(QStringLiteral("value"))
                    .toString()
                    .startsWith(QStringLiteral("2029-02-28")));
        auto* keyboard = editor->findChild<QQuickItem*>(QStringLiteral("CouchKeyboard"));
        QVERIFY(keyboard);
        QVERIFY(QMetaObject::invokeMethod(keyboard, "open", Q_ARG(QVariant, QVariant(QString{})),
                                          Q_ARG(QVariant, QVariant(QStringLiteral("Test"))),
                                          Q_ARG(QVariant, QVariant(false)),
                                          Q_ARG(QVariant, QVariant(32))));
        QTest::qWait(20);
        QVERIFY(send(keyboard, QStringLiteral("accept")));
        QCOMPARE(keyboard->property("text").toString(), QStringLiteral("q"));
        for (const auto& mode : {QStringLiteral("123"), QStringLiteral("symbols")}) {
            keyboard->setProperty("mode", mode);
            QTest::qWait(20);
            QVERIFY(QMetaObject::invokeMethod(keyboard, "focusKey", Q_ARG(QVariant, QVariant(1)),
                                              Q_ARG(QVariant, QVariant(0))));
            QTest::qWait(5);
            QVERIFY(send(keyboard, QStringLiteral("accept")));
        }
        QCOMPARE(keyboard->property("text").toString(), QStringLiteral("q1["));
        keyboard->setProperty("mode",QStringLiteral("abc"));keyboard->setProperty("shifted",true);QTest::qWait(20);
        QVERIFY(QMetaObject::invokeMethod(keyboard,"focusKey",Q_ARG(QVariant,QVariant(1)),Q_ARG(QVariant,QVariant(0))));QTest::qWait(5);QVERIFY(send(keyboard,QStringLiteral("accept")));
        QCOMPARE(keyboard->property("text").toString(),QStringLiteral("q1[Q"));
        QVERIFY(QMetaObject::invokeMethod(keyboard,"focusKey",Q_ARG(QVariant,QVariant(4)),Q_ARG(QVariant,QVariant(4))));QTest::qWait(5);QVERIFY(send(keyboard,QStringLiteral("accept")));QVERIFY(keyboard->property("text").toString().isEmpty());
    }
    void productionSceneAndScaling() {
        InitializeCouchResources();
        Harness harness;
        QQuickWidget view;
        view.setResizeMode(QQuickWidget::SizeRootObjectToView);
        view.rootContext()->setContextProperty(QStringLiteral("Couch"), &harness);
        view.resize(1920, 1080);
        view.setSource(QUrl(QStringLiteral("qrc:/couch/Horizon.qml")));
        if (view.status() != QQuickWidget::Ready) {
            for (const auto& error : view.errors())
                qWarning() << error.toString();
        }
        QCOMPARE(view.status(), QQuickWidget::Ready);
        view.show();
        QTest::qWait(150);
        auto* root = view.rootObject();
        QVERIFY(root);
        auto* canvas = root->findChild<QQuickItem*>(QStringLiteral("CouchDesignCanvas"));
        QVERIFY(canvas);
        QCOMPARE(canvas->width(), 1920.0);
        QCOMPARE(canvas->height(), 1080.0);
        QCOMPARE(canvas->scale(), 1.0);
        for (const auto& name :
             {QStringLiteral("CouchTopNext"), QStringLiteral("CouchGroupNext")}) {
            auto* button = root->findChild<QQuickItem*>(name);
            QVERIFY(button);
            auto* text = button->property("contentItem").value<QQuickItem*>();
            QVERIFY(text);
            QVERIFY(!text->property("truncated").toBool());
        }
        QVERIFY(root->findChild<QQuickItem*>(QStringLiteral("CouchSettingsSearch")));
        auto* review = root->findChild<QQuickItem*>(QStringLiteral("CouchReview"));
        auto* save = root->findChild<QQuickItem*>(QStringLiteral("CouchSave"));
        QVERIFY(review && save);
        QVERIFY(review->x() < save->x());
        QMetaObject::invokeMethod(root, "focusZone",
                                  Q_ARG(QVariant, QVariant(QStringLiteral("settings"))),
                                  Q_ARG(QVariant, QVariant(0)));
        QTest::qWait(10);
        for (int i = 0; i < 20; ++i)
            QMetaObject::invokeMethod(root, "controller",
                                      Q_ARG(QVariant, QVariant(QStringLiteral("down"))));
        QTest::qWait(20);
        auto* list = root->findChild<QQuickItem*>(QStringLiteral("CouchSettingsRows"));
        QVERIFY(list);
        QVERIFY(list->property("contentY").toDouble() > 0);
        view.grabFramebuffer().save(
            QDir::current().filePath(QStringLiteral("couch-settings-1080.png")));
        view.resize(1280, 800);
        QTest::qWait(50);
        QVERIFY(qAbs(canvas->scale() - 2.0 / 3.0) < 0.001);
        const auto top = canvas->mapToItem(root, QPointF(0, 0));
        QVERIFY(qAbs(top.y() - 40) < 0.01);
        view.grabFramebuffer().save(
            QDir::current().filePath(QStringLiteral("couch-settings-800.png")));
    }
};
} // namespace
QTEST_MAIN(CouchTests)
#include "couch_mode.moc"
