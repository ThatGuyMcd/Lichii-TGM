// SPDX-License-Identifier: GPL-3.0-or-later

#include <QAbstractItemView>
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSlider>
#include <QSpinBox>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include "qt_common/cover_art.h"
#include "qt_common/forest_theme.h"
#include "qt_common/touch_ui.h"

class ForestThemeTests : public QObject {
    Q_OBJECT
private slots:
    void SettingsSurviveDecorationAndThemeSwitches() {
        using namespace QtCommon::ForestTheme;
        Apply(QStringLiteral("forest_lantern_dark"));
        QDialog dialog;
        dialog.setProperty("touchFullscreenOptOut", true);
        dialog.setWindowTitle(QStringLiteral("Controller settings"));
        auto* layout = new QVBoxLayout(&dialog);
        auto* edit = new QLineEdit(QStringLiteral("saved controller profile"), &dialog);
        layout->addWidget(edit);
        for (int i = 0; i < 40; ++i) {
            layout->addWidget(new QLineEdit(QString::number(i), &dialog));
        }
        auto* buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, &dialog);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        QSignalSpy accepted(&dialog, &QDialog::accepted);
        QSignalSpy apply(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked);
        const auto edits = dialog.findChildren<QLineEdit*>();
        PrepareDialog(&dialog);
        auto* new_layout = dialog.layout();
        PrepareDialog(&dialog);
        QCOMPARE(dialog.layout(), new_layout);
        QCOMPARE(dialog.findChildren<QLineEdit*>().size(), edits.size());
        for (auto* original : edits) {
            QVERIFY(dialog.isAncestorOf(original));
        }
        dialog.resize(520, 400);
        dialog.show();
        QTest::qWait(30);
        auto* scroll = dialog.findChild<QScrollArea*>();
        QVERIFY(scroll);
        QVERIFY(scroll->verticalScrollBar()->maximum() > 0);
        QVERIFY(buttons->isVisible());
        QVERIFY(dialog.rect().contains(buttons->mapTo(&dialog, QPoint(0, 0))));
        QVERIFY(!scroll->isAncestorOf(buttons));
        edit->setText(QStringLiteral("new unsaved profile"));
        const QColor dark = qApp->palette().color(QPalette::Window);
        Apply(QStringLiteral("forest_lantern_light"));
        QVERIFY(qApp->palette().color(QPalette::Window).lightness() > dark.lightness());
        QCOMPARE(edit->text(), QStringLiteral("new unsaved profile"));
        QCOMPARE(dialog.findChildren<QLineEdit*>().size(), edits.size());
        buttons->button(QDialogButtonBox::Apply)->click();
        QCOMPARE(apply.size(), 1);
        QCOMPARE(accepted.size(), 0);
        buttons->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(accepted.size(), 1);
    }

    void ArtworkAndLibraryUsePackagedResources() {
        using namespace QtCommon::ForestTheme;
        Apply(QStringLiteral("forest_lantern_dark"));
        for (const QString& path : {QStringLiteral(":/forest/header-bough.png")}) {
            QVERIFY2(!QPixmap(path).isNull(), qPrintable(path));
        }
        QVERIFY(QPixmap(QStringLiteral(":/forest/footer-fox.png")).isNull());
        QVERIFY(QPixmap(QStringLiteral(":/forest/forest-glade.png")).isNull());
        LibraryPanel panel;
        QSignalSpy play(&panel, &LibraryPanel::PlayRequested);
        const auto buttons = panel.findChildren<QPushButton*>();
        QCOMPARE(buttons.size(), 3);
        for (auto* button : buttons) {
            QVERIFY(!button->isEnabled());
        }
        panel.SetGame(QStringLiteral("Example game"), QStringLiteral("/library/example.nsp"));
        for (auto* button : buttons) {
            QVERIFY(button->isEnabled());
        }
        buttons.front()->click();
        QCOMPARE(play.size(), 1);
        panel.SetGame({}, {});
        for (auto* button : buttons) {
            QVERIFY(!button->isEnabled());
        }
        QVERIFY(!qApp->styleSheet().contains(QLatin1Char('@')));
    }

    void CoversPreserveAspectAndRejectStaleResults() {
        auto encode = [](const QSize& size, QRgb color) {
            QImage image(size, QImage::Format_RGB32);
            image.fill(color);
            QByteArray bytes;
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::WriteOnly);
            image.save(&buffer, "PNG");
            return bytes;
        };
        QtCommon::CoverArt view;
        view.resize(400, 240);
        const auto square = encode(QSize(256, 256), qRgb(220, 10, 10));
        const auto wide = encode(QSize(512, 256), qRgb(10, 220, 10));
        view.SetSource(QStringLiteral("A"), square);
        view.SetSource(QStringLiteral("B"), wide);
        QTRY_COMPARE(view.CurrentImage().size(), QSize(512, 256));
        QCOMPARE(view.CurrentImage().pixel(0, 0), qRgb(10, 220, 10));
        QCOMPARE(view.ImageRect(), QRect(0, 20, 400, 200));
        view.SetSource(QStringLiteral("A"), square);
        QTRY_COMPARE(view.CurrentImage().size(), QSize(256, 256));
        QCOMPARE(view.ImageRect(), QRect(80, 0, 240, 240));
        view.SetSource(QStringLiteral("bad"), QByteArrayLiteral("broken image"));
        QTest::qWait(50);
        QVERIFY(view.CurrentImage().isNull());
        view.SetSource(QStringLiteral("A"), square);
        view.SetSource({}, {});
        QTest::qWait(50);
        QVERIFY(view.CurrentImage().isNull());
    }

    void TouchFieldsRetainRangesAndConnections() {
        using namespace QtCommon::TouchUi;
        Initialize();
        QtCommon::ForestTheme::Apply(QStringLiteral("forest_lantern_light"));
        QDialog dialog;
        dialog.setProperty("touchFullscreenOptOut", true);
        auto* layout = new QVBoxLayout(&dialog);
        auto* integer = new QSpinBox(&dialog);
        integer->setRange(5, 7);
        integer->setValue(6);
        auto* real = new QDoubleSpinBox(&dialog);
        real->setRange(0, 2);
        real->setSingleStep(0.25);
        real->setValue(1.25);
        auto* combo = new QComboBox(&dialog);
        combo->addItems({QStringLiteral("One"), QStringLiteral("Two"), QStringLiteral("Three")});
        layout->addWidget(integer);
        layout->addWidget(real);
        layout->addWidget(combo);
        auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply, &dialog);
        layout->addWidget(box);
        connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(box->button(QDialogButtonBox::Apply), &QPushButton::clicked, &dialog,
                [&dialog] { MarkApplied(&dialog); });
        QSignalSpy changes(integer, &QSpinBox::valueChanged);
        InstallSettingsShell(&dialog, QStringLiteral("Test settings"));
        dialog.resize(640, 400);
        dialog.show();
        QTest::qWait(40);
        QVERIFY(dialog.isAncestorOf(integer));
        QCOMPARE(integer->minimum(), 5);
        QCOMPARE(integer->maximum(), 7);
        auto buttons = integer->parentWidget()->findChildren<QToolButton*>();
        QCOMPARE(buttons.size(), 2);
        buttons.back()->click();
        QCOMPARE(integer->value(), 7);
        QCOMPARE(changes.size(), 1);
        QVERIFY(!buttons.back()->isEnabled());
        integer->setEnabled(false);
        QVERIFY(!buttons.front()->isEnabled());
        integer->setEnabled(true);
        QVERIFY(buttons.front()->isEnabled());
        for (auto* button : buttons) {
            QVERIFY(button->width() >= 48 && button->height() >= 48);
        }
        QVERIFY(combo->height() >= 48);
        combo->showPopup();
        QTest::qWait(20);
        QVERIFY(combo->view()->visualRect(combo->model()->index(1, 0)).height() >= 48);
        combo->hidePopup();
        auto* footer = dialog.findChild<QWidget*>(QStringLiteral("TouchSettingsFooter"));
        QVERIFY(footer && footer->isVisible());
        QVERIFY(dialog.rect().contains(footer->geometry()));
        box->button(QDialogButtonBox::Apply)->click();
        QVERIFY(dialog.isVisible());
        box->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(dialog.result(), QDialog::Accepted);
    }

    void BackPreservesGuardedCancelAndLiveSettings() {
        using namespace QtCommon::TouchUi;
        Initialize(true, false);
        QDialog dialog;
        auto* layout = new QVBoxLayout(&dialog);
        auto* slider = new QSlider(Qt::Horizontal, &dialog);
        slider->setRange(0, 10);
        slider->setValue(5);
        int live_value = slider->value();
        connect(slider, &QSlider::valueChanged, [&live_value](int value) { live_value = value; });
        layout->addWidget(slider);
        auto* box = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        layout->addWidget(box);
        bool can_close = false;
        int guarded_attempts = 0;
        connect(box, &QDialogButtonBox::rejected, &dialog, [&] {
            ++guarded_attempts;
            if (can_close)
                dialog.reject();
        });
        InstallSettingsShell(&dialog, QStringLiteral("Live settings"));
        dialog.show();
        QTest::qWait(30);
        slider->setFocus();
        QTest::keyClick(slider, Qt::Key_Right);
        QCOMPARE(live_value, 6);
        auto* back = dialog.findChild<QPushButton*>(QStringLiteral("TouchSettingsBack"));
        back->click();
        QCOMPARE(guarded_attempts, 1);
        QVERIFY(dialog.isVisible());
        QVERIFY(!qobject_cast<QMessageBox*>(QApplication::activeModalWidget()));
        can_close = true;
        back->click();
        QCOMPARE(guarded_attempts, 2);
        QVERIFY(!dialog.isVisible());
        QCOMPARE(live_value, 6);
    }

    void FullscreenAndKeyboardChangesUseOriginalSave() {
        using namespace QtCommon::TouchUi;
        Initialize(true, true);
        QDialog dialog;
        auto* layout = new QVBoxLayout(&dialog);
        auto* group = new QGroupBox(QStringLiteral("Player vibration"), &dialog);
        group->setCheckable(true);
        layout->addWidget(group);
        auto* slider = new QSlider(Qt::Horizontal, &dialog);
        slider->setRange(0, 10);
        slider->setValue(5);
        layout->addWidget(slider);
        auto* box = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, &dialog);
        layout->addWidget(box);
        connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        connect(box->button(QDialogButtonBox::Apply), &QPushButton::clicked, &dialog,
                [&] { MarkApplied(&dialog); });
        InstallSettingsShell(&dialog, QStringLiteral("Settings"));
        dialog.show();
        QTRY_VERIFY(dialog.isFullScreen());
        QVERIFY(slider->height() >= 48);
        auto* windowed = dialog.findChild<QPushButton*>(QStringLiteral("TouchSettingsWindowed"));
        windowed->click();
        QVERIFY(!dialog.isFullScreen());
        slider->setFocus();
        QTest::keyClick(slider, Qt::Key_Right);
        QCOMPARE(slider->value(), 6);
        auto* footer = dialog.findChild<QWidget*>(QStringLiteral("TouchSettingsFooter"));
        QVERIFY(footer->findChild<QLabel*>()->text().contains(QStringLiteral("pending")));
        box->button(QDialogButtonBox::Apply)->click();
        QCOMPARE(footer->findChild<QLabel*>()->text(), QStringLiteral("Ready"));
        QTest::mouseClick(group, Qt::LeftButton, Qt::NoModifier, QPoint(group->width() - 8, 20));
        QVERIFY(!group->isChecked());
        QVERIFY(footer->findChild<QLabel*>()->text().contains(QStringLiteral("pending")));
        auto* back = dialog.findChild<QPushButton*>(QStringLiteral("TouchSettingsBack"));
        QTimer::singleShot(0, [] {
            auto* question = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            QVERIFY(question);
            for (auto* button : question->buttons()) {
                if (question->buttonRole(button) == QMessageBox::RejectRole) {
                    button->click();
                    return;
                }
            }
        });
        back->click();
        QVERIFY(dialog.isVisible());
        QTimer::singleShot(0, [] {
            auto* question = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            QVERIFY(question);
            for (auto* button : question->buttons()) {
                if (question->buttonRole(button) == QMessageBox::AcceptRole) {
                    button->click();
                    return;
                }
            }
        });
        back->click();
        QCOMPARE(dialog.result(), QDialog::Accepted);
        QCOMPARE(slider->value(), 6);
        QVERIFY(!group->isChecked());
        Initialize(true, false);
    }
};

QTEST_MAIN(ForestThemeTests)
#include "forest_theme.moc"
