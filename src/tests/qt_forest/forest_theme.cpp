// SPDX-License-Identifier: GPL-3.0-or-later

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>
#include <QVBoxLayout>
#include "qt_common/forest_theme.h"

class ForestThemeTests : public QObject {
    Q_OBJECT
private slots:
    void SettingsSurviveDecorationAndThemeSwitches() {
        using namespace QtCommon::ForestTheme;
        Apply(QStringLiteral("forest_lantern_dark"));
        QDialog dialog;
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
        for (const QString& path : {QStringLiteral(":/forest/header-bough.png"),
                                    QStringLiteral(":/forest/footer-fox.png"),
                                    QStringLiteral(":/forest/forest-glade.png")}) {
            QVERIFY2(!QPixmap(path).isNull(), qPrintable(path));
        }
        LibraryPanel panel;
        QSignalSpy play(&panel, &LibraryPanel::PlayRequested);
        const auto buttons = panel.findChildren<QPushButton*>();
        QCOMPARE(buttons.size(), 2);
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
};

QTEST_MAIN(ForestThemeTests)
#include "forest_theme.moc"
