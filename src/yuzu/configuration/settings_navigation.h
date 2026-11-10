// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <utility>
#include <vector>

#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QRegularExpression>
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

    explicit SettingsNavigation(QWidget* parent = nullptr) : QWidget(parent) {
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(20);
        auto* sidebar = new QWidget(this);
        sidebar->setMinimumWidth(210);
        sidebar->setMaximumWidth(260);
        auto* sidebar_layout = new QVBoxLayout(sidebar);
        sidebar_layout->setContentsMargins(0, 0, 0, 0);
        search = new QLineEdit(sidebar);
        search->setObjectName(QStringLiteral("ForestSearch"));
        search->setClearButtonEnabled(true);
        search->installEventFilter(this);
        selection = new QListWidget(sidebar);
        selection->setObjectName(QStringLiteral("ForestSettingsNavigation"));
        selection->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        selection->setTextElideMode(Qt::ElideRight);
        selection->setSpacing(2);
        sidebar_layout->addWidget(search);
        sidebar_layout->addWidget(selection, 1);
        auto* content = new QWidget(this);
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
        layout->addWidget(sidebar);
        layout->addWidget(content, 1);
        connect(search, &QLineEdit::textChanged, this, [this] { Filter(); });
        connect(selection, &QListWidget::currentItemChanged, this,
                [this](QListWidgetItem* item) {
                    if (item) {
                        Select(item->data(Qt::UserRole).toInt());
                    }
                });
        auto* find = new QShortcut(QKeySequence::Find, this);
        connect(find, &QShortcut::activated, this, [this] {
            search->setFocus(Qt::ShortcutFocusReason);
            search->selectAll();
        });
        Retranslate();
    }

    void SetPages(std::vector<PageSpec> specifications) {
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
                updated.push_back({{}, scroll, nullptr});
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
                heading->setFlags(Qt::NoItemFlags);
                heading->setData(Qt::UserRole, -1);
                auto font = heading->font();
                font.setBold(true);
                heading->setFont(font);
                groups.push_back(heading);
            }
            page.item = new QListWidgetItem(page.spec.title, selection);
            page.item->setToolTip(page.spec.title);
            page.item->setData(Qt::UserRole, static_cast<int>(index));
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
        empty->setText(tr("No settings match your search."));
        // Children receive their own LanguageChange events after the dialog. Search their
        // translated labels once those callbacks have run, keeping the same page instances.
        QTimer::singleShot(0, this, [this] { Filter(); });
    }

protected:
    bool eventFilter(QObject* object, QEvent* event) override {
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
    };

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
            page.item->setHidden(!matches);
            if (matches) {
                if (first == -1) {
                    first = static_cast<int>(index);
                }
                if (page.spec.widget == current) {
                    selected = static_cast<int>(index);
                }
            }
        }
        for (auto* heading : groups) {
            bool visible = false;
            for (int row = selection->row(heading) + 1; row < selection->count(); ++row) {
                const auto* item = selection->item(row);
                if (item->data(Qt::UserRole).toInt() < 0) {
                    break;
                }
                visible |= !item->isHidden();
            }
            heading->setHidden(!visible);
        }
        if (selected == -1) {
            selected = first;
        }
        if (selected >= 0) {
            selection->setCurrentItem(pages[selected].item);
            Select(selected);
        } else {
            selection->setCurrentRow(-1);
            title->clear();
            stack->setCurrentWidget(empty);
        }
    }

    void Select(int index) {
        if (index < 0 || static_cast<size_t>(index) >= pages.size()) {
            return;
        }
        auto& page = pages[index];
        current = page.spec.widget;
        title->setText(page.spec.title);
        stack->setCurrentWidget(page.scroll);
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
    QStackedWidget* stack;
    QPointer<QWidget> current;
    QStringList terms;
    std::vector<Page> pages;
    std::vector<QListWidgetItem*> groups;
};

} // namespace ConfigurationShared
