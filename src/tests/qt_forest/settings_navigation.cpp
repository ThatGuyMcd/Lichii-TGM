// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QTest>

#include "yuzu/configuration/settings_navigation.h"

namespace {
int checks{};
void Check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
void Events() {
    QApplication::processEvents();
    QApplication::processEvents();
}
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    try {
        QDialog dialog;
        dialog.resize(1000, 700);
        auto* layout = new QVBoxLayout(&dialog);
        layout->addWidget(new QLabel(QStringLiteral("Settings — Forest Lantern")));
        auto* navigation = new ConfigurationShared::SettingsNavigation(&dialog);
        layout->addWidget(navigation, 1);
        auto* footer = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
                                            QDialogButtonBox::Apply, &dialog);
        layout->addWidget(footer);
        QObject::connect(footer, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        QObject::connect(footer, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        std::array<QWidget*, 16> pages{};
        std::array<QLineEdit*, 16> values{};
        std::array<QCheckBox*, 16> toggles{};
        std::vector<ConfigurationShared::SettingsNavigation::PageSpec> specifications;
        QLabel* shader_help{};
        QTabWidget* controls{};
        std::array<QWidget*, 10> control_pages{};
        std::array<QLineEdit*, 10> controller_values{};
        const std::array<QString, 6> group_names{
            QStringLiteral("Application"), QStringLiteral("Emulation"), QStringLiteral("Video"),
            QStringLiteral("Devices"), QStringLiteral("Services"), QStringLiteral("Diagnostics")};
        constexpr std::array<size_t, 16> group_for_page{0, 0, 0, 1, 1, 1, 1, 1,
                                                       2, 2, 2, 3, 3, 4, 4, 5};
        for (size_t index = 0; index < pages.size(); ++index) {
            auto* page = new QWidget;
            page->setMinimumHeight(1200);
            auto* fields = new QVBoxLayout(page);
            auto* label = new QLabel(QStringLiteral("Original option %1").arg(index), page);
            fields->addWidget(label);
            values[index] = new QLineEdit(QStringLiteral("pending-%1").arg(index), page);
            toggles[index] = new QCheckBox(QStringLiteral("Keep option %1").arg(index), page);
            toggles[index]->setChecked(index % 2 == 0);
            fields->addWidget(values[index]);
            fields->addWidget(toggles[index]);
            if (index == 9) {
                shader_help = label;
                label->setText(QStringLiteral("Asynchronous shader compilation"));
                label->setToolTip(QStringLiteral("<p>Bounded cache pruning preserves hot shaders.</p>"));
            }
            if (index == 8) label->setMinimumWidth(1600);
            if (index == 12) {
                controls = new QTabWidget(page);
                for (size_t player = 0; player < control_pages.size(); ++player) {
                    auto* subpage = new QWidget;
                    auto* subpage_fields = new QVBoxLayout(subpage);
                    const auto text = player < 8 ? QStringLiteral("Player %1").arg(player + 1)
                                      : player == 8 ? QStringLiteral("Advanced")
                                                    : QStringLiteral("Microphone");
                    auto* name = new QLabel(text, subpage);
                    if (player == 9) {
                        name->setText(QStringLiteral("Microphone capture sensitivity"));
                        name->setToolTip(QStringLiteral("Noise gate threshold"));
                    }
                    subpage_fields->addWidget(name);
                    controller_values[player] = new QLineEdit(
                        QStringLiteral("controller-pending-%1").arg(player), subpage);
                    subpage_fields->addWidget(controller_values[player]);
                    controls->addTab(subpage, text);
                    control_pages[player] = subpage;
                }
                fields->addWidget(controls);
            }
            fields->addStretch();
            pages[index] = page;
            specifications.push_back({page, QStringLiteral("Page %1").arg(index),
                group_names[group_for_page[index]],
                index == 10 ? QStringLiteral("Vulkan descriptor compatibility") : QString{}});
        }
        navigation->SetPages(specifications);
        auto* search = navigation->findChild<QLineEdit*>(QStringLiteral("ForestSearch"));
        auto* list = navigation->findChild<QListWidget*>(QStringLiteral("ForestSettingsNavigation"));
        dialog.show();
        Events();
        Check(search && list, "search and navigation names exposed");
        Check(navigation->findChildren<QScrollArea*>().size() == 16, "all sixteen pages wrapped once");
        Check(list->count() == 22, "sixteen pages plus six categories");
        const auto visible_pages = [&] {
            int visible{};
            for (int row = 0; row < list->count(); ++row) {
                const auto* item = list->item(row);
                visible += item->data(Qt::UserRole).toInt() >= 0 && !item->isHidden();
            }
            return visible;
        };
        const auto heading = [&](int group) {
            for (int row = 0; row < list->count(); ++row) {
                auto* item = list->item(row);
                if (item->data(Qt::UserRole).toInt() < 0 &&
                    item->data(Qt::UserRole + 1).toInt() == group) return item;
            }
            throw std::runtime_error("Missing category");
        };
        const auto tap = [&](QListWidgetItem* item) {
            list->scrollToItem(item);
            Events();
            QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                              list->visualItemRect(item).center());
            Events();
        };
        const auto check_targets = [&] {
            for (int row = 0; row < list->count(); ++row) {
                const auto* item = list->item(row);
                if (!item->isHidden()) {
                    Check(list->visualItemRect(item).height() >= 48,
                          "visible navigation target remains at least48 high");
                }
            }
        };
        for (int row = 0; row < list->count(); ++row) {
            auto* item = list->item(row);
            if (item->data(Qt::UserRole).toInt() < 0) {
                Check(item->flags().testFlag(Qt::ItemIsSelectable), "category is keyboard selectable");
            }
        }
        Check(visible_pages() == 3, "only the current Application category starts expanded");
        check_targets();
        constexpr std::array<int, 6> category_counts{3, 5, 3, 2, 2, 1};
        for (const int group : {1, 2, 3, 4, 5, 0}) {
            tap(heading(group));
            Check(visible_pages() == category_counts[group], "all six category page counts are retained");
            check_targets();
        }
        tap(heading(1));
        Check(visible_pages() == 5, "Emulation exposes all five original pages");
        tap(heading(2));
        Check(visible_pages() == 3, "Video expands while other categories collapse");
        tap(heading(2));
        Check(visible_pages() == 0, "current category can be collapsed without destroying its page");
        QTest::keyClick(list, Qt::Key_Right);
        Events();
        Check(visible_pages() == 3, "Right expands the selected category");
        QTest::keyClick(list, Qt::Key_Left);
        Events();
        Check(visible_pages() == 0, "Left collapses the current category");
        QTest::keyClick(list, Qt::Key_Return);
        Events();
        Check(visible_pages() == 3 && dialog.isVisible(), "Enter expands category without accepting dialog");
        const auto query = [&](const QString& text) { search->setText(text); Events(); };
        query(QStringLiteral("pruning"));
        Check(navigation->CurrentPage() == pages[9], "tooltip words find original graphics page");
        query(QStringLiteral("option 15"));
        Check(navigation->CurrentPage() == pages[15], "search reaches collapsed Diagnostics page");
        Check(visible_pages() == 1, "search shows matches regardless of collapsed state");
        query(QStringLiteral("descriptor"));
        Check(navigation->CurrentPage() == pages[10], "aliases find their page");
        query(QStringLiteral("sensitivity"));
        Check(navigation->CurrentPage() == pages[12], "nested microphone setting is searchable");
        Check(controls->currentIndex() == 9, "search reveals existing Microphone tab");
        query(QStringLiteral("Player 8"));
        Check(controls->currentIndex() == 7, "tab-title search reveals player eight");
        query(QStringLiteral("no-such-setting"));
        Check(navigation->findChild<QLabel*>(QStringLiteral("ForestSettingsEmpty"))->isVisible(),
              "zero results have a visible explanation");
        Check(navigation->findChildren<QScrollArea*>().size() == 16, "filter never reconstructs pages");
        search->setFocus();
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(search, &escape);
        Events();
        Check(search->text().isEmpty() && dialog.isVisible(), "Escape clears search without closing dialog");
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(search, &enter);
        Events();
        Check(dialog.isVisible(), "Enter in search does not press OK");
        controls->setCurrentIndex(8);
        for (auto& specification : specifications) {
            specification.title.prepend(QStringLiteral("Translated "));
        }
        navigation->SetPages(specifications);
        navigation->SetPages(specifications);
        Events();
        Check(controls->currentIndex() == 8, "label refresh retains selected nested tab");
        Check(navigation->findChildren<QScrollArea*>().size() == 16, "translation reuses all containers");
        for (size_t index = 0; index < pages.size(); ++index) {
            Check(values[index]->text() == QStringLiteral("pending-%1").arg(index), "pending text retained");
            Check(toggles[index]->isChecked() == (index % 2 == 0), "pending checkbox retained");
        }
        for (size_t index = 0; index < control_pages.size(); ++index) {
            Check(controls->widget(static_cast<int>(index)) == control_pages[index], "all controls tab pointers retained");
            Check(controller_values[index]->text() == QStringLiteral("controller-pending-%1").arg(index),
                  "all controller edits retained");
        }
        shader_help->setToolTip(QStringLiteral("Nouvelle aide traduite"));
        navigation->Retranslate();
        query(QStringLiteral("traduite"));
        Check(navigation->CurrentPage() == pages[9], "search reads newly translated help text");
        query(QString{});
        auto found = list->findItems(QStringLiteral("Translated Page 8"), Qt::MatchExactly);
        Check(found.size() == 1, "translated page still selectable");
        list->setCurrentItem(found.front());
        values[8]->setFocus();
        dialog.resize(760, 480);
        Events();
        Check(footer->geometry().bottom() < dialog.height(), "footer remains within short dialog");
        QScrollArea* current_scroll{};
        for (auto* scroll : navigation->findChildren<QScrollArea*>()) {
            if (scroll->widget() == pages[8]) current_scroll = scroll;
        }
        Check(current_scroll && current_scroll->verticalScrollBar()->maximum() > 0,
              "long original page remains vertically scrollable");
        Check(current_scroll->horizontalScrollBar()->maximum() > 0,
              "wide original fields remain reachable");
        auto* sections = navigation->findChild<QPushButton*>(QStringLiteral("TouchSettingsSections"));
        auto* drawer = navigation->findChild<QWidget*>(QStringLiteral("TouchSettingsDrawer"));
        auto* content = navigation->findChild<QWidget*>(QStringLiteral("TouchSettingsContent"));
        Check(sections && sections->isVisible() && sections->height() >= 48,
              "narrow workspace has a large labelled Sections button");
        Check(!drawer->isVisible() && content->isVisible(), "narrow layout starts with full-width page");
        sections->click();
        Events();
        Check(drawer->isVisible() && !content->isVisible(), "Sections opens a full-width drawer");
        Check(drawer->width() >= navigation->width() - 8, "drawer does not squeeze the settings form");
        query(QStringLiteral("sensitivity"));
        Check(controls->currentIndex() == 9, "drawer search still reveals original microphone subtab");
        auto result = list->findItems(QStringLiteral("Translated Page 12"), Qt::MatchExactly);
        Check(result.size() == 1, "Controls result remains reachable in drawer");
        tap(result.front());
        Check(!drawer->isVisible() && content->isVisible(), "choosing a result returns to full-width page");
        Check(navigation->CurrentPage() == pages[12], "drawer keeps the selected original page");
        dialog.resize(640, 400);
        Events();
        sections->click();
        Events();
        Check(sections->height() >= 48 && search->height() >= 48,
              "smaller workspace does not shrink touch controls");
        check_targets();
        query(QStringLiteral("missing-handheld-setting"));
        Check(navigation->findChild<QLabel*>(QStringLiteral("TouchSettingsNoResults"))->isVisible(),
              "empty search has an explanation inside the drawer");
        search->setFocus();
        QTest::keyClick(search, Qt::Key_Escape);
        Events();
        Check(search->text().isEmpty() && drawer->isVisible(), "first Escape clears active drawer search");
        QTest::keyClick(search, Qt::Key_Escape);
        Events();
        Check(!drawer->isVisible() && dialog.isVisible(), "second Escape closes drawer before workspace");
        dialog.activateWindow();
        Events();
        QTest::keyClick(&dialog, Qt::Key_F, Qt::ControlModifier);
        Events();
        Check(drawer->isVisible() && search->hasFocus(), "Ctrl+F opens narrow drawer and focuses search");
        QTest::keyClick(search, Qt::Key_Escape);
        Events();
        Check(!drawer->isVisible(), "Escape returns from keyboard-opened drawer");
        Check(footer->geometry().bottom() < dialog.height(), "footer remains reachable at640x400");
        dialog.resize(1280, 800);
        Events();
        Check(!sections->isVisible() && drawer->isVisible() && content->isVisible(),
              "wide layout restores the persistent sidebar and original page");
        check_targets();
        Check(navigation->findChildren<QScrollArea*>().size() == 16,
              "reflow and category toggles never replace settings pages");
        for (size_t index = 0; index < pages.size(); ++index) {
            Check(values[index]->text() == QStringLiteral("pending-%1").arg(index),
                  "pending values survive handheld reflow");
        }
        int applied{};
        QObject::connect(footer->button(QDialogButtonBox::Apply), &QPushButton::clicked, [&] {
            for (size_t index = 0; index < pages.size(); ++index) {
                if (values[index]->text() == QStringLiteral("pending-%1").arg(index)) ++applied;
            }
        });
        footer->button(QDialogButtonBox::Apply)->click();
        Check(applied == 16, "Apply still reads all original page widgets");
        footer->button(QDialogButtonBox::Ok)->click();
        Check(dialog.result() == QDialog::Accepted, "OK connection is retained");
        footer->button(QDialogButtonBox::Cancel)->click();
        Check(dialog.result() == QDialog::Rejected, "Cancel connection is retained");
        std::cout << checks << " navigation/retention checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Failed after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
