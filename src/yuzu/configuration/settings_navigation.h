// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <utility>
#include <vector>

#include <QComboBox>
#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTextDocumentFragment>
#include <QTimer>
#include <QTreeWidgetItemIterator>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace ConfigurationShared {

/// Navigation owns the scroll containers; the settings pages themselves are never reconstructed.
class SettingsNavigation final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(SettingsNavigation)

public:
    struct PageSpec {
        QWidget* widget;
        QString title;
        QString group;
        QString aliases;
    };

    explicit SettingsNavigation(QWidget* parent = nullptr, int target_height = 48)
        : QWidget(parent), target_size(std::max(1, target_height)) {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        drawer_bar = new QWidget(this);
        drawer_bar->setProperty("touchUiNavigation", true);
        auto* drawer_layout = new QHBoxLayout(drawer_bar);
        drawer_layout->setContentsMargins(0, 0, 0, 0);
        sections = new QPushButton(drawer_bar);
        sections->setObjectName(QStringLiteral("TouchSettingsSections"));
        sections->setCheckable(true);
        sections->setAutoDefault(false);
        sections->setMinimumSize(this->target_size, this->target_size);
        sections->installEventFilter(this);
        breadcrumb = new QLabel(drawer_bar);
        breadcrumb->setObjectName(QStringLiteral("TouchSettingsBreadcrumb"));
        breadcrumb->setWordWrap(true);
        breadcrumb->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        drawer_layout->addWidget(sections);
        drawer_layout->addWidget(breadcrumb, 1);
        layout->addWidget(drawer_bar);
        auto* body = new QHBoxLayout;
        body->setContentsMargins(0, 0, 0, 0);
        body->setSpacing(16);
        layout->addLayout(body, 1);
        sidebar = new QWidget(this);
        sidebar->setProperty("touchUiNavigation", true);
        sidebar->setObjectName(QStringLiteral("TouchSettingsDrawer"));
        auto* sidebar_layout = new QVBoxLayout(sidebar);
        sidebar_layout->setContentsMargins(0, 0, 0, 0);
        sidebar_layout->setSpacing(8);
        search = new QLineEdit(sidebar);
        search->setObjectName(QStringLiteral("ForestSearch"));
        search->setMinimumHeight(this->target_size);
        search->setClearButtonEnabled(true);
        search->installEventFilter(this);
        selection = new QListWidget(sidebar);
        selection->setObjectName(QStringLiteral("ForestSettingsNavigation"));
        selection->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        selection->setTextElideMode(Qt::ElideNone);
        selection->setWordWrap(true);
        selection->setSpacing(4);
        selection->installEventFilter(this);
        selection->viewport()->installEventFilter(this);
        sidebar_layout->addWidget(search);
        no_results = new QLabel(sidebar);
        no_results->setObjectName(QStringLiteral("TouchSettingsNoResults"));
        no_results->setWordWrap(true);
        no_results->hide();
        sidebar_layout->addWidget(no_results);
        sidebar_layout->addWidget(selection, 1);
        content = new QWidget(this);
        content->setObjectName(QStringLiteral("TouchSettingsContent"));
        auto* content_layout = new QVBoxLayout(content);
        content_layout->setContentsMargins(0, 0, 0, 0);
        title = new QLabel(content);
        title->setObjectName(QStringLiteral("ForestSettingsTitle"));
        title->setWordWrap(true);
        stack = new QStackedWidget(content);
        empty = new QLabel(stack);
        empty->setObjectName(QStringLiteral("ForestSettingsEmpty"));
        empty->setAlignment(Qt::AlignCenter);
        empty->setWordWrap(true);
        stack->addWidget(empty);
        content_layout->addWidget(title);
        content_layout->addWidget(stack, 1);
        body->addWidget(sidebar, 1);
        body->addWidget(content, 3);
        connect(search, &QLineEdit::textChanged, this, [this] { Filter(); });
        connect(selection, &QListWidget::currentItemChanged, this,
                [this](QListWidgetItem* item) {
                    if (item) {
                        Select(item->data(Qt::UserRole).toInt());
                    }
                });
        connect(selection, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
            const int index = item->data(Qt::UserRole).toInt();
            if (index >= 0) {
                Select(index);
                SetDrawerOpen(false);
            } else {
                ToggleGroup(item->data(Qt::UserRole + 1).toInt());
            }
        });
        connect(sections, &QPushButton::toggled, this, [this](bool open) {
            SetDrawerOpen(open);
            if (open) selection->setFocus(Qt::ShortcutFocusReason);
        });
        auto* find = new QShortcut(QKeySequence::Find, this);
        connect(find, &QShortcut::activated, this, [this] {
            SetDrawerOpen(true);
            search->setFocus(Qt::ShortcutFocusReason);
            search->selectAll();
        });
        Retranslate();
        Reflow();
    }

    void SetPages(std::vector<PageSpec> specifications) {
        const bool initialized = !pages.empty();
        const QPointer<QWidget> expanded = expanded_group >= 0 &&
            static_cast<size_t>(expanded_group) < groups.size()
            ? pages[groups[expanded_group].first].spec.widget : nullptr;
        const QSignalBlocker blocker(selection);
        std::vector<Page> updated;
        updated.reserve(specifications.size());
        for (auto& specification : specifications) {
            const auto old = std::ranges::find_if(pages, [&](const Page& page) {
                return page.spec.widget == specification.widget;
            });
            if (old != pages.end()) {
                updated.push_back(std::move(*old));
            } else {
                auto* scroll = new QScrollArea(stack);
                scroll->setObjectName(QStringLiteral("ForestSettingsPage"));
                scroll->setFrameShape(QFrame::NoFrame);
                scroll->setWidgetResizable(true);
                scroll->setFocusPolicy(Qt::StrongFocus);
                scroll->setWidget(specification.widget);
                stack->addWidget(scroll);
                updated.push_back({{}, scroll, nullptr, -1, true});
            }
            updated.back().spec = std::move(specification);
        }
        pages = std::move(updated);
        selection->clear();
        groups.clear();
        QString group;
        for (size_t index = 0; index < pages.size(); ++index) {
            auto& page = pages[index];
            if (index == 0 || page.spec.group != group) {
                group = page.spec.group;
                auto* heading = new QListWidgetItem(group, selection);
                heading->setData(Qt::UserRole, -1);
                heading->setData(Qt::UserRole + 1, static_cast<int>(groups.size()));
                auto font = selection->font();
                font.setBold(true);
                heading->setFont(font);
                groups.push_back({group, heading, index, index, nullptr});
            }
            page.group = static_cast<int>(groups.size() - 1);
            groups.back().end = index + 1;
            page.item = new QListWidgetItem(page.spec.title, selection);
            page.item->setToolTip(page.spec.title);
            page.item->setData(Qt::UserRole, static_cast<int>(index));
        }
        expanded_group = initialized || pages.empty() ? -1 : 0;
        if (expanded) {
            for (const auto& page : pages) {
                if (page.spec.widget == expanded) expanded_group = page.group;
            }
        }
        Retranslate();
        Filter();
    }

    QWidget* CurrentPage() const {
        return current;
    }

    void Retranslate() {
        search->setPlaceholderText(tr("Search settings…"));
        search->setAccessibleName(tr("Search settings"));
        search->setToolTip(tr("Find a settings page by its name, labels, or help text. Ctrl+F"));
        selection->setAccessibleName(tr("Settings pages"));
        sections->setText(tr("Sections"));
        sections->setAccessibleName(tr("Show or hide settings sections"));
        empty->setText(tr("No settings match your search."));
        no_results->setText(empty->text());
        // Children receive their own LanguageChange events after the dialog. Search their
        // translated labels once those callbacks have run, keeping the same page instances.
        QTimer::singleShot(0, this, [this] { Filter(); });
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        Reflow();
        ScheduleRowSizes();
    }

    bool eventFilter(QObject* object, QEvent* event) override {
        if (object == selection->viewport() && event->type() == QEvent::Resize) {
            ScheduleRowSizes();
        }
        if ((object == search || object == selection || object == sections) &&
            event->type() == QEvent::KeyPress) {
            const auto key = static_cast<QKeyEvent*>(event)->key();
            if (key == Qt::Key_Escape && narrow && drawer_open &&
                (object != search || search->text().isEmpty())) {
                SetDrawerOpen(false);
                return true;
            }
        }
        if (object == sections && event->type() == QEvent::KeyPress) {
            const auto key = static_cast<QKeyEvent*>(event)->key();
            if (key == Qt::Key_Return || key == Qt::Key_Enter) {
                sections->click();
                return true;
            }
        }
        if (object == selection && event->type() == QEvent::KeyPress) {
            const auto key = static_cast<QKeyEvent*>(event)->key();
            auto* item = selection->currentItem();
            if (item && (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Space)) {
                const int index = item->data(Qt::UserRole).toInt();
                if (index < 0) ToggleGroup(item->data(Qt::UserRole + 1).toInt());
                else {
                    Select(index);
                    SetDrawerOpen(false);
                }
                return true;
            }
            if (item && terms.empty() && (key == Qt::Key_Left || key == Qt::Key_Right)) {
                const int index = item->data(Qt::UserRole).toInt();
                const int group = index < 0 ? item->data(Qt::UserRole + 1).toInt()
                                           : pages[index].group;
                if ((key == Qt::Key_Left && expanded_group == group) ||
                    (key == Qt::Key_Right && expanded_group != group)) ToggleGroup(group);
                return true;
            }
        }
        if (object == search && event->type() == QEvent::KeyPress) {
            const auto key = static_cast<QKeyEvent*>(event)->key();
            if (key == Qt::Key_Escape && !search->text().isEmpty()) {
                search->clear();
                return true;
            }
            if (key == Qt::Key_Down || key == Qt::Key_Return || key == Qt::Key_Enter) {
                if (auto* item = selection->currentItem(); item && !item->isHidden()) {
                    selection->setFocus(Qt::ShortcutFocusReason);
                }
                return true; // Enter in search navigates instead of accepting the dialog.
            }
        }
        return QWidget::eventFilter(object, event);
    }

private:
    struct Page {
        PageSpec spec;
        QScrollArea* scroll;
        QListWidgetItem* item;
        int group;
        bool matches;
    };

    struct Group {
        QString label;
        QListWidgetItem* item;
        size_t first;
        size_t end;
        QPointer<QWidget> last;
    };

    void Reflow() {
        const bool next = width() < 900;
        if (next != narrow) {
            drawer_open = next && (search->hasFocus() || selection->hasFocus());
            narrow = next;
        }
        drawer_bar->setVisible(narrow);
        sidebar->setMinimumWidth(narrow ? 0 : 240);
        sidebar->setMaximumWidth(narrow ? QWIDGETSIZE_MAX : 300);
        sidebar->setVisible(!narrow || drawer_open);
        content->setVisible(!narrow || !drawer_open);
        const QSignalBlocker blocker(sections);
        sections->setChecked(narrow && drawer_open);
    }

    void SetDrawerOpen(bool open) {
        if (narrow && open && !drawer_open) focus_before_drawer = QApplication::focusWidget();
        drawer_open = narrow && open;
        Reflow();
        if (narrow && !drawer_open) {
            if (focus_before_drawer && focus_before_drawer->isVisible()) {
                focus_before_drawer->setFocus(Qt::ShortcutFocusReason);
            } else if (auto* scroll = qobject_cast<QScrollArea*>(stack->currentWidget())) {
                scroll->setFocus(Qt::ShortcutFocusReason);
            }
        }
    }

    void ScheduleRowSizes() {
        if (row_sizes_pending) return;
        row_sizes_pending = true;
        QTimer::singleShot(0, this, [this] {
            row_sizes_pending = false;
            const int width = std::max(80, selection->viewport()->width() - 48);
            for (int row = 0; row < selection->count(); ++row) {
                auto* item = selection->item(row);
                const bool heading = item->data(Qt::UserRole).toInt() < 0;
                const QFontMetrics metrics(heading ? item->font() : selection->font());
                const int text_height = metrics.boundingRect(
                    QRect(0, 0, width, 0), Qt::TextWordWrap, item->text()).height() + 16;
                item->setSizeHint(QSize(0, std::max(target_size + (heading ? 8 : 0), text_height)));
            }
        });
    }

    void RefreshVisibility() {
        for (size_t index = 0; index < groups.size(); ++index) {
            auto& group = groups[index];
            bool visible = false;
            const bool expanded = !terms.empty() || expanded_group == static_cast<int>(index);
            for (size_t page = group.first; page < group.end; ++page) {
                visible |= pages[page].matches;
                pages[page].item->setHidden(!pages[page].matches || !expanded);
            }
            group.item->setHidden(!visible);
            group.item->setText((expanded ? QStringLiteral("▾ ") : QStringLiteral("▸ ")) +
                                group.label);
        }
        ScheduleRowSizes();
    }

    void ToggleGroup(int index) {
        if (index < 0 || static_cast<size_t>(index) >= groups.size()) return;
        auto& group = groups[index];
        if (terms.empty() && expanded_group == index) {
            expanded_group = -1;
            const QSignalBlocker blocker(selection);
            selection->setCurrentItem(group.item);
            RefreshVisibility();
            return;
        }
        expanded_group = index;
        for (size_t page = group.first; page < group.end; ++page) {
            if (pages[page].matches && (!group.last || pages[page].spec.widget == group.last)) {
                const QSignalBlocker blocker(selection);
                selection->setCurrentItem(pages[page].item);
                Select(static_cast<int>(page), false);
                RefreshVisibility();
                return;
            }
        }
        // A search may exclude the previously selected page in this category.
        for (size_t page = group.first; page < group.end; ++page) {
            if (pages[page].matches) {
                const QSignalBlocker blocker(selection);
                selection->setCurrentItem(pages[page].item);
                Select(static_cast<int>(page), false);
                return;
            }
        }
    }

    static QString SearchText(QString text) {
        if (text.trimmed().startsWith(QLatin1Char('<'))) {
            text = QTextDocumentFragment::fromHtml(text).toPlainText();
        }
        return text.remove(QLatin1Char('&')).toCaseFolded();
    }

    static QString WidgetText(const QWidget* widget) {
        QString result;
        for (const char* property : {"text", "title", "toolTip", "whatsThis", "accessibleName",
                                     "accessibleDescription", "placeholderText"}) {
            result += widget->property(property).toString() + QLatin1Char(' ');
        }
        if (const auto* combo = qobject_cast<const QComboBox*>(widget)) {
            for (int index = 0; index < combo->count(); ++index) {
                result += combo->itemText(index) + QLatin1Char(' ');
                result += combo->itemData(index, Qt::ToolTipRole).toString() + QLatin1Char(' ');
            }
        }
        if (const auto* tabs = qobject_cast<const QTabWidget*>(widget)) {
            for (int index = 0; index < tabs->count(); ++index) {
                result += tabs->tabText(index) + QLatin1Char(' ');
                result += tabs->tabToolTip(index) + QLatin1Char(' ');
            }
        }
        if (const auto* tree = qobject_cast<const QTreeWidget*>(widget)) {
            for (QTreeWidgetItemIterator it(const_cast<QTreeWidget*>(tree)); *it; ++it) {
                for (int column = 0; column < tree->columnCount(); ++column) {
                    result += (*it)->text(column) + QLatin1Char(' ');
                    result += (*it)->toolTip(column) + QLatin1Char(' ');
                }
            }
        }
        return SearchText(result);
    }

    bool Matches(const QString& text) const {
        return std::ranges::all_of(terms, [&](const QString& term) { return text.contains(term); });
    }

    void Filter() {
        const bool was_searching = !terms.empty();
        terms = SearchText(search->text()).split(QRegularExpression(QStringLiteral("\\s+")),
                                                  Qt::SkipEmptyParts);
        const QSignalBlocker blocker(selection);
        int first = -1;
        int selected = -1;
        for (size_t index = 0; index < pages.size(); ++index) {
            auto& page = pages[index];
            QString text = SearchText(page.spec.title + QLatin1Char(' ') + page.spec.group +
                                      QLatin1Char(' ') + page.spec.aliases);
            if (!terms.empty()) {
                text += QLatin1Char(' ') + WidgetText(page.spec.widget);
                for (const auto* child : page.spec.widget->findChildren<QWidget*>()) {
                    text += QLatin1Char(' ') + WidgetText(child);
                }
            }
            const bool matches = Matches(text);
            page.matches = matches;
            if (matches) {
                if (first == -1) {
                    first = static_cast<int>(index);
                }
                if (page.spec.widget == current) {
                    selected = static_cast<int>(index);
                }
            }
        }
        if (selected == -1) {
            selected = first;
        }
        if (selected >= 0) {
            if (was_searching && terms.empty()) expanded_group = pages[selected].group;
            Select(selected, false);
            selection->setCurrentItem(pages[selected].item->isHidden()
                ? groups[pages[selected].group].item : pages[selected].item);
        } else {
            RefreshVisibility();
            selection->setCurrentRow(-1);
            title->clear();
            stack->setCurrentWidget(empty);
        }
        no_results->setVisible(first < 0);
    }

    void Select(int index, bool expand = true) {
        if (index < 0 || static_cast<size_t>(index) >= pages.size()) {
            return;
        }
        auto& page = pages[index];
        current = page.spec.widget;
        groups[page.group].last = current;
        if (expand || !terms.empty()) expanded_group = page.group;
        title->setText(page.spec.title);
        breadcrumb->setText(page.spec.group + QStringLiteral(" › ") + page.spec.title);
        stack->setCurrentWidget(page.scroll);
        RefreshVisibility();
        if (terms.empty()) {
            return;
        }
        for (auto* tabs : page.spec.widget->findChildren<QTabWidget*>()) {
            for (int tab = 0; tab < tabs->count(); ++tab) {
                if (Matches(SearchText(tabs->tabText(tab) + QLatin1Char(' ') +
                                       tabs->tabToolTip(tab)))) {
                    tabs->setCurrentIndex(tab);
                    break;
                }
            }
        }
        for (auto* child : page.spec.widget->findChildren<QWidget*>()) {
            if (qobject_cast<QTabWidget*>(child)) {
                continue;
            }
            if (Matches(WidgetText(child))) {
                // Reveal matches inside existing Controls/Debug tabs without moving a tab page.
                for (auto* tabs : page.spec.widget->findChildren<QTabWidget*>()) {
                    for (int tab = 0; tab < tabs->count(); ++tab) {
                        if (tabs->widget(tab) == child || tabs->widget(tab)->isAncestorOf(child)) {
                            tabs->setCurrentIndex(tab);
                        }
                    }
                }
                const QPointer<QWidget> target(child);
                const QPointer<QScrollArea> scroll(page.scroll);
                QTimer::singleShot(0, this, [scroll, target] {
                    if (scroll && target) {
                        scroll->ensureWidgetVisible(target, 16, 24);
                    }
                });
                break;
            }
        }
    }

    QLineEdit* search;
    QListWidget* selection;
    QLabel* title;
    QLabel* empty;
    QLabel* no_results;
    QLabel* breadcrumb;
    QWidget* drawer_bar;
    QWidget* sidebar;
    QWidget* content;
    QPushButton* sections;
    QStackedWidget* stack;
    QPointer<QWidget> current;
    QPointer<QWidget> focus_before_drawer;
    QStringList terms;
    std::vector<Page> pages;
    std::vector<Group> groups;
    const int target_size;
    int expanded_group{-1};
    bool narrow{};
    bool drawer_open{};
    bool row_sizes_pending{};
};

} // namespace ConfigurationShared
