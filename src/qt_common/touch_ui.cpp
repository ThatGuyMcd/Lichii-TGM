// SPDX-License-Identifier: GPL-3.0-or-later

#include "qt_common/touch_ui.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QInputMethod>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProxyStyle>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QScroller>
#include <QSlider>
#include <QSpinBox>
#include <QStyleFactory>
#include <QStyleOption>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QWindow>

#include "qt_common/forest_theme.h"

namespace QtCommon::TouchUi {
namespace {

class TouchStyle final : public QProxyStyle {
public:
    TouchStyle() : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))) {}

    void polish(QWidget* widget) override {
        QProxyStyle::polish(widget);
        if (IsComfortable()) {
            if (auto* area = qobject_cast<QAbstractScrollArea*>(widget)) {
                QScroller::grabGesture(area->viewport(), QScroller::TouchGesture);
            }
        }
    }

    void drawControl(ControlElement element, const QStyleOption* option, QPainter* painter,
                     const QWidget* widget) const override {
        if (IsComfortable() && (element == CE_CheckBoxLabel || element == CE_RadioButtonLabel)) {
            if (const auto* button = qstyleoption_cast<const QStyleOptionButton*>(option);
                button && button->icon.isNull()) {
                drawItemText(painter, button->rect,
                             Qt::AlignVCenter | Qt::TextWordWrap | Qt::TextShowMnemonic,
                             button->palette, button->state & State_Enabled, button->text,
                             QPalette::WindowText);
                return;
            }
        }
        QProxyStyle::drawControl(element, option, painter, widget);
    }

    QSize sizeFromContents(ContentsType type, const QStyleOption* option, const QSize& contents,
                           const QWidget* widget) const override {
        QSize result = QProxyStyle::sizeFromContents(type, option, contents, widget);
        if (IsComfortable()) {
            const auto* menu = qstyleoption_cast<const QStyleOptionMenuItem*>(option);
            switch (type) {
            case CT_PushButton:
            case CT_ToolButton:
            case CT_ComboBox:
            case CT_SpinBox:
            case CT_LineEdit:
            case CT_CheckBox:
            case CT_RadioButton:
            case CT_TabBarTab:
            case CT_MenuItem:
            case CT_MenuBarItem:
            case CT_ItemViewItem:
                if (type != CT_MenuItem || !menu ||
                    menu->menuItemType != QStyleOptionMenuItem::Separator) {
                    result.setHeight(qMax(result.height(), 48));
                    if (type == CT_ToolButton || type == CT_PushButton) {
                        result.setWidth(qMax(result.width(), 48));
                    }
                }
                break;
            default:
                break;
            }
            if (type == CT_CheckBox || type == CT_RadioButton) {
                if (const auto* button = qstyleoption_cast<const QStyleOptionButton*>(option)) {
                    const int width = qMin(420, result.width());
                    const QRect text = button->fontMetrics.boundingRect(
                        QRect(0, 0, qMax(120, width - 40), 10000), Qt::TextWordWrap, button->text);
                    result.setWidth(width);
                    result.setHeight(qMax(result.height(), text.height() + 16));
                }
            }
        }
        return result;
    }

    int pixelMetric(PixelMetric metric, const QStyleOption* option,
                    const QWidget* widget) const override {
        if (IsComfortable()) {
            switch (metric) {
            case PM_IndicatorWidth:
            case PM_IndicatorHeight:
            case PM_ExclusiveIndicatorWidth:
            case PM_ExclusiveIndicatorHeight:
            case PM_SmallIconSize:
            case PM_LineEditIconSize:
                return 24;
            case PM_LineEditIconMargin:
                return 12;
            case PM_ScrollBarExtent:
                return 24;
            case PM_ScrollBarSliderMin:
                return 48;
            case PM_SliderLength:
            case PM_SliderThickness:
                return 32;
            case PM_SplitterWidth:
                return 16;
            case PM_ToolBarExtensionExtent:
            case PM_TabBarScrollButtonWidth:
                return 48;
            case PM_LayoutHorizontalSpacing:
            case PM_LayoutVerticalSpacing:
                return 8;
            default:
                break;
            }
        }
        return QProxyStyle::pixelMetric(metric, option, widget);
    }

    QRect subElementRect(SubElement element, const QStyleOption* option,
                         const QWidget* widget) const override {
        if (IsComfortable() && widget &&
            (element == SE_CheckBoxClickRect || element == SE_RadioButtonClickRect)) {
            return widget->rect();
        }
        return QProxyStyle::subElementRect(element, option, widget);
    }

    int styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget,
                  QStyleHintReturn* data) const override {
        if (hint == SH_DialogButtonLayout && IsComfortable())
            return QDialogButtonBox::GnomeLayout;
        if (hint == SH_Menu_Scrollable) {
            return 1;
        }
        return QProxyStyle::styleHint(hint, option, widget, data);
    }
};

class Spinner final : public QWidget {
public:
    Spinner(QAbstractSpinBox* field, QWidget* parent) : QWidget(parent), spin(field) {
        setObjectName(QStringLiteral("TouchNumericEditor"));
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        minus = new QToolButton(this);
        plus = new QToolButton(this);
        minus->setText(QStringLiteral("−"));
        plus->setText(QStringLiteral("+"));
        minus->setAccessibleName(tr("Decrease value"));
        plus->setAccessibleName(tr("Increase value"));
        for (auto* button : {minus, plus}) {
            button->setAutoRepeat(true);
            button->setAutoRepeatDelay(500);
            button->setMinimumSize(48, 48);
            button->setProperty("touchNumericButton", true);
        }
        layout->addWidget(minus);
        layout->addWidget(spin, 1);
        layout->addWidget(plus);
        spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        spin->setMinimumWidth(qMin(spin->minimumWidth(), 64));
        spin->installEventFilter(this);
        connect(minus, &QToolButton::clicked, spin, &QAbstractSpinBox::stepDown);
        connect(plus, &QToolButton::clicked, spin, &QAbstractSpinBox::stepUp);
        if (auto* integer = qobject_cast<QSpinBox*>(spin)) {
            connect(integer, &QSpinBox::valueChanged, this, [this] { Refresh(); });
        } else if (auto* real = qobject_cast<QDoubleSpinBox*>(spin)) {
            connect(real, &QDoubleSpinBox::valueChanged, this, [this] { Refresh(); });
        }
        Refresh();
    }

protected:
    bool eventFilter(QObject* object, QEvent* event) override {
        if (object == spin &&
            (event->type() == QEvent::EnabledChange || event->type() == QEvent::Show)) {
            Refresh();
        }
        return QWidget::eventFilter(object, event);
    }

private:
    void Refresh() {
        bool lower = spin->isEnabled();
        bool upper = lower;
        if (const auto* integer = qobject_cast<QSpinBox*>(spin)) {
            lower &= integer->wrapping() || integer->value() > integer->minimum();
            upper &= integer->wrapping() || integer->value() < integer->maximum();
        } else if (const auto* real = qobject_cast<QDoubleSpinBox*>(spin)) {
            lower &= real->wrapping() || real->value() > real->minimum();
            upper &= real->wrapping() || real->value() < real->maximum();
        }
        minus->setEnabled(lower);
        plus->setEnabled(upper);
    }

    QAbstractSpinBox* spin;
    QToolButton* minus;
    QToolButton* plus;
};

class ResponsiveRow final : public QObject {
public:
    ResponsiveRow(QBoxLayout* layout, QWidget* page) : QObject(page), row(layout) {
        page->installEventFilter(this);
        Reflow(page);
    }

protected:
    bool eventFilter(QObject* object, QEvent* event) override {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Show) {
            Reflow(static_cast<QWidget*>(object));
        }
        return QObject::eventFilter(object, event);
    }

private:
    void Reflow(QWidget* page) {
        int available = page->width();
        for (auto* parent = page->parentWidget(); parent; parent = parent->parentWidget()) {
            if (auto* area = qobject_cast<QAbstractScrollArea*>(parent)) {
                available = qMin(available, area->viewport()->width());
                break;
            }
        }
        row->setDirection(IsComfortable() && available < 1100 ? QBoxLayout::TopToBottom
                                                              : QBoxLayout::LeftToRight);
        if (page->minimumWidth() > row->minimumSize().width()) {
            page->setMinimumWidth(row->minimumSize().width());
        }
    }
    QBoxLayout* row;
};

class SettingsHeader final : public QFrame {
public:
    explicit SettingsHeader(QWidget* parent)
        : QFrame(parent), bough(QStringLiteral(":/forest/header-bough.png")) {
        space = new QWidget(this);
        space->setObjectName(QStringLiteral("TouchHeaderArtSpace"));
        space->setFixedWidth(0);
        setMinimumHeight(64);
    }
    QWidget* ArtSpace() const {
        return space;
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        space->setFixedWidth(width() >= 1000 ? 190 : 0);
        QFrame::resizeEvent(event);
    }
    void paintEvent(QPaintEvent* event) override {
        QFrame::paintEvent(event);
        if (width() < 1000 || bough.isNull())
            return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawPixmap(QRect(width() - 340, -24, 340, 114), bough);
    }

private:
    QPixmap bough;
    QWidget* space;
};

class ResponsiveGrid final : public QObject {
public:
    ResponsiveGrid(QBoxLayout* original, QWidget* parent) : QObject(parent), page(parent) {
        grid = new QGridLayout;
        grid->setSpacing(8);
        while (original->count())
            items.append(original->takeAt(0));
        original->addLayout(grid);
        page->installEventFilter(this);
        for (auto* ancestor = page->parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
            if (auto* area = qobject_cast<QAbstractScrollArea*>(ancestor)) {
                viewport = area->viewport();
                viewport->installEventFilter(this);
                break;
            }
        }
        Reflow();
    }

protected:
    bool eventFilter(QObject*, QEvent* event) override {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Show)
            Reflow();
        return false;
    }

private:
    void Reflow() {
        const int width = viewport ? viewport->width() : page->width();
        int cell = 160;
        for (auto* item : items)
            cell = qMax(cell, item->minimumSize().width());
        const int count = qBound(1, qMax(1, width - 48) / (cell + 8), 4);
        if (count == columns)
            return;
        columns = count;
        for (auto* item : items)
            grid->removeItem(item);
        for (int i = 0; i < items.size(); ++i)
            grid->addItem(items[i], i / columns, i % columns);
        page->setMinimumWidth(0);
        for (auto* ancestor = page->parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
            if (qobject_cast<QAbstractScrollArea*>(ancestor))
                break;
            ancestor->setMinimumWidth(0);
        }
    }

    QWidget* page;
    QPointer<QWidget> viewport;
    QGridLayout* grid;
    QList<QLayoutItem*> items;
    int columns{};
};

void WrapNumbers(QLayout* layout) {
    if (!layout) {
        return;
    }
    for (int i = 0; i < layout->count(); ++i) {
        auto* item = layout->itemAt(i);
        if (auto* field = qobject_cast<QAbstractSpinBox*>(item->widget());
            field && !field->property("touchNumericPrepared").toBool() &&
            (qobject_cast<QSpinBox*>(field) || qobject_cast<QDoubleSpinBox*>(field))) {
            field->setProperty("touchNumericPrepared", true);
            auto* wrapper = new QWidget(field->parentWidget());
            auto* removed = layout->replaceWidget(field, wrapper);
            if (removed) {
                delete removed;
                auto* holder = new QVBoxLayout(wrapper);
                holder->setContentsMargins(0, 0, 0, 0);
                holder->addWidget(new Spinner(field, wrapper));
            } else {
                delete wrapper;
            }
        } else if (auto* nested = item->layout()) {
            WrapNumbers(nested);
        }
    }
}

class SettingsShell final : public QObject {
public:
    SettingsShell(QDialog* owner, const QString& title, const QString& subtitle)
        : QObject(owner), dialog(owner) {
        setObjectName(QStringLiteral("TouchSettingsShell"));
        auto* old_layout = dialog->layout();
        const auto buttons =
            dialog->findChildren<QDialogButtonBox*>(QString(), Qt::FindDirectChildrenOnly);
        for (auto* box : buttons) {
            RemoveWidget(old_layout, box);
        }
        auto* content = new QWidget;
        content->setLayout(old_layout);
        auto* layout = new QVBoxLayout(dialog);
        layout->setContentsMargins(12, 8, 12, 12);
        auto* decorated_header = new SettingsHeader(dialog);
        header = decorated_header;
        header->setObjectName(QStringLiteral("TouchSettingsHeader"));
        auto* heading_layout = new QHBoxLayout(header);
        heading_layout->setContentsMargins(0, 0, 0, 0);
        back = new QPushButton(tr("← Back"), header);
        back->setAutoDefault(false);
        back->setObjectName(QStringLiteral("TouchSettingsBack"));
        heading_layout->addWidget(back);
        title_label = new QLabel(title, header);
        title_label->setObjectName(QStringLiteral("ForestSettingsTitle"));
        title_label->setWordWrap(true);
        heading_layout->addWidget(title_label, 1);
        windowed = new QPushButton(tr("Windowed"), header);
        windowed->setAutoDefault(false);
        windowed->setObjectName(QStringLiteral("TouchSettingsWindowed"));
        heading_layout->addWidget(windowed);
        heading_layout->addWidget(decorated_header->ArtSpace());
        layout->addWidget(header);
        hint = new QLabel(subtitle, dialog);
        hint->setObjectName(QStringLiteral("ForestHint"));
        hint->setWordWrap(true);
        hint->setVisible(!subtitle.isEmpty());
        layout->addWidget(hint);
        // Navigation already provides a scroll container for every page. Child dialogs need
        // one outer scroll area instead, with their original footer kept outside it.
        if (content->findChild<QWidget*>(QStringLiteral("ForestSettingsNavigation"))) {
            layout->addWidget(content, 1);
        } else {
            auto* scroll = new QScrollArea(dialog);
            scroll->setObjectName(QStringLiteral("TouchSettingsScroll"));
            scroll->setWidgetResizable(true);
            scroll->setFrameShape(QFrame::NoFrame);
            scroll->setWidget(content);
            layout->addWidget(scroll, 1);
        }
        auto* footer = new QWidget(dialog);
        footer->setObjectName(QStringLiteral("TouchSettingsFooter"));
        auto* footer_layout = new QHBoxLayout(footer);
        footer_layout->setContentsMargins(0, 0, 0, 0);
        status = new QLabel(tr("Ready"), footer);
        status->setObjectName(QStringLiteral("ForestHint"));
        footer_layout->addWidget(status, 1);
        for (auto* box : buttons) {
            footer_layout->addWidget(box);
            for (auto* button : box->buttons()) {
                const auto role = box->buttonRole(button);
                if (role == QDialogButtonBox::AcceptRole || role == QDialogButtonBox::YesRole) {
                    accept_action = button;
                } else if (role == QDialogButtonBox::RejectRole) {
                    reject_action = button;
                }
            }
            if (auto* ok = box->button(QDialogButtonBox::Ok)) {
                ok->setText(tr("Save && close"));
            }
        }
        layout->addWidget(footer);
        dialog->setMinimumSize(480, 320);
        dialog->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        dialog->setSizeGripEnabled(true);
        dialog->installEventFilter(this);
        connect(back, &QPushButton::clicked, this, [this] { Leave(); });
        connect(windowed, &QPushButton::clicked, this, [this] {
            if (dialog->isFullScreen()) {
                dialog->showNormal();
                dialog->resize(
                    QSize(1120, 760).boundedTo(dialog->screen()->availableGeometry().size()));
            } else {
                dialog->showFullScreen();
            }
            UpdateText();
        });
        PreparePage(dialog);
        for (auto* field : dialog->findChildren<QLineEdit*>()) {
            if (!IsNavigation(field) && !field->isReadOnly()) {
                connect(field, &QLineEdit::textChanged, this, [this] { Edited(); });
            }
        }
        for (auto* field : dialog->findChildren<QComboBox*>()) {
            connect(field, &QComboBox::activated, this, [this] { Edited(); });
        }
        for (auto* button : dialog->findChildren<QAbstractButton*>()) {
            if (!IsNavigation(button) && !header->isAncestorOf(button) &&
                !footer->isAncestorOf(button) && !InsideButtonBox(button)) {
                connect(button, &QAbstractButton::clicked, this, [this] { Edited(); });
            }
        }
        for (auto* group : dialog->findChildren<QGroupBox*>()) {
            if (group->isCheckable())
                connect(group, &QGroupBox::toggled, this, [this] { Edited(); });
        }
        for (auto* field : dialog->findChildren<QKeySequenceEdit*>()) {
            connect(field, &QKeySequenceEdit::keySequenceChanged, this, [this] { Edited(); });
        }
        for (auto* view : dialog->findChildren<QAbstractItemView*>()) {
            if (IsNavigation(view) || !view->model())
                continue;
            connect(view->model(), &QAbstractItemModel::dataChanged, this,
                    [this](const QModelIndex&, const QModelIndex&, const QList<int>& roles) {
                        if (roles.isEmpty() || roles.contains(Qt::CheckStateRole) ||
                            roles.contains(Qt::EditRole))
                            Edited();
                    });
        }
        for (auto* field : dialog->findChildren<QSpinBox*>()) {
            connect(field, &QSpinBox::valueChanged, this, [this] { Edited(); });
        }
        for (auto* field : dialog->findChildren<QDoubleSpinBox*>()) {
            connect(field, &QDoubleSpinBox::valueChanged, this, [this] { Edited(); });
        }
        for (auto* field : dialog->findChildren<QSlider*>()) {
            connect(field, &QSlider::valueChanged, this, [this] { Edited(); });
        }
        for (auto* field : dialog->findChildren<QTextEdit*>()) {
            if (!field->isReadOnly())
                connect(field, &QTextEdit::textChanged, this, [this] { Edited(); });
        }
        for (auto* field : dialog->findChildren<QPlainTextEdit*>()) {
            if (!field->isReadOnly())
                connect(field, &QPlainTextEdit::textChanged, this, [this] { Edited(); });
        }
    }

    void SetText(const QString& title, const QString& subtitle) {
        title_label->setText(title);
        hint->setText(subtitle);
        hint->setVisible(!subtitle.isEmpty());
        UpdateText();
    }

    void Applied() {
        dirty = false;
        UpdateText();
    }

protected:
    bool eventFilter(QObject* object, QEvent* event) override {
        if (object != dialog) {
            return QObject::eventFilter(object, event);
        }
        if (event->type() == QEvent::Show && !shown) {
            shown = true;
            QTimer::singleShot(0, this, [this] {
                if (!dialog->isVisible())
                    return;
                if (dialog->parentWidget() && dialog->windowHandle()) {
                    dialog->windowHandle()->setScreen(dialog->parentWidget()->screen());
                }
                if (qApp->property("touchSettingsFullscreen").toBool() &&
                    !dialog->property("touchFullscreenOptOut").toBool()) {
                    dialog->showFullScreen();
                }
                ready = true;
                UpdateText();
            });
        } else if (event->type() == QEvent::Resize) {
            status->setVisible(dialog->width() >= 800);
            hint->setVisible(dialog->height() >= 600 && !hint->text().isEmpty());
        } else if (event->type() == QEvent::KeyPress &&
                   static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape &&
                   !qApp->activePopupWidget()) {
            Leave();
            return true;
        } else if (event->type() == QEvent::Close && dirty && !requesting_close) {
            Leave();
            event->ignore();
            return true;
        }
        return QObject::eventFilter(object, event);
    }

private:
    static bool IsNavigation(QWidget* widget) {
        for (; widget; widget = widget->parentWidget()) {
            if (widget->property("touchUiNavigation").toBool() ||
                widget->objectName() == QStringLiteral("ForestSearch") ||
                widget->objectName() == QStringLiteral("ForestSettingsNavigation"))
                return true;
        }
        return false;
    }

    static bool InsideButtonBox(QWidget* widget) {
        for (; widget; widget = widget->parentWidget()) {
            if (qobject_cast<QDialogButtonBox*>(widget))
                return true;
        }
        return false;
    }

    static void RemoveWidget(QLayout* layout, QWidget* widget) {
        layout->removeWidget(widget);
        for (int i = 0; i < layout->count(); ++i) {
            if (auto* nested = layout->itemAt(i)->layout())
                RemoveWidget(nested, widget);
        }
    }

    void Edited() {
        if (ready && dialog->isVisible() && accept_action) {
            dirty = true;
            UpdateText();
        }
    }

    void UpdateText() {
        back->setText(tr("← Back"));
        windowed->setText(dialog->isFullScreen() ? tr("Windowed") : tr("Full screen"));
        status->setText(dirty ? tr("Changes pending") : tr("Ready"));
        for (auto* box : dialog->findChildren<QDialogButtonBox*>()) {
            if (auto* ok = box->button(QDialogButtonBox::Ok))
                ok->setText(tr("Save && close"));
        }
    }

    void Leave() {
        if (dirty) {
            QMessageBox question(
                QMessageBox::Question, tr("Pending changes"),
                tr("Save pending changes before leaving? Previously applied changes are kept."),
                QMessageBox::NoButton, dialog);
            auto* save = question.addButton(tr("Save && close"), QMessageBox::AcceptRole);
            auto* discard =
                question.addButton(tr("Discard pending changes"), QMessageBox::DestructiveRole);
            question.addButton(tr("Keep editing"), QMessageBox::RejectRole);
            question.exec();
            if (question.clickedButton() == save) {
                if (accept_action)
                    accept_action->click();
                return;
            }
            if (question.clickedButton() != discard)
                return;
        }
        // Invoke the original Cancel/Close action: some settings dialogs guard active work
        // there. Direct reject() would bypass those guards and could destroy live callbacks.
        if (reject_action) {
            reject_action->click();
        } else {
            requesting_close = true;
            dialog->close();
            requesting_close = false;
        }
        if (!dialog->isVisible())
            dirty = false;
    }

    QDialog* dialog;
    QWidget* header;
    QLabel* title_label;
    QLabel* hint;
    QLabel* status;
    QPushButton* back;
    QPushButton* windowed;
    QPointer<QAbstractButton> accept_action;
    QPointer<QAbstractButton> reject_action;
    bool shown{};
    bool ready{};
    bool dirty{};
    bool requesting_close{};
};

} // namespace

bool IsComfortable() {
    return !qApp || !qApp->property("touchComfortable").isValid() ||
           qApp->property("touchComfortable").toBool();
}

int TargetSize() {
    return IsComfortable() ? 48 : 28;
}

void Initialize(bool comfortable, bool settings_fullscreen) {
    qApp->setProperty("touchComfortable", comfortable);
    qApp->setProperty("touchSettingsFullscreen", settings_fullscreen);
    if (!qApp->property("touchStyleInstalled").toBool()) {
        qApp->setStyle(new TouchStyle);
        qApp->setProperty("touchStyleInstalled", true);
        auto keep_editor_visible = [] {
            auto* editor = QApplication::focusWidget();
            if (!editor ||
                (!qobject_cast<QLineEdit*>(editor) && !qobject_cast<QTextEdit*>(editor) &&
                 !qobject_cast<QPlainTextEdit*>(editor)))
                return;
            bool settings = false;
            for (auto* ancestor = editor; ancestor; ancestor = ancestor->parentWidget()) {
                settings |= ancestor->property("forestDialogPrepared").toBool();
            }
            if (!settings)
                return;
            for (auto* ancestor = editor->parentWidget(); ancestor;
                 ancestor = ancestor->parentWidget()) {
                if (auto* area = qobject_cast<QScrollArea*>(ancestor)) {
                    area->ensureWidgetVisible(editor, 16, 32);
                    if (qApp->inputMethod()->isVisible() && area->widget()) {
                        const int y = editor->mapTo(area->widget(), QPoint()).y();
                        area->verticalScrollBar()->setValue(y - area->viewport()->height() / 4);
                    }
                }
            }
        };
        QObject::connect(qApp, &QApplication::focusChanged, qApp, [keep_editor_visible] {
            QTimer::singleShot(0, qApp, keep_editor_visible);
        });
        QObject::connect(qApp->inputMethod(), &QInputMethod::keyboardRectangleChanged, qApp,
                         keep_editor_visible);
    }
    for (auto* widget : qApp->allWidgets()) {
        if (widget->property("touchNumericButton").toBool()) {
            widget->setMinimumSize(TargetSize(), TargetSize());
        }
        if (!comfortable && widget->property("touchOldMinimumHeight").isValid()) {
            widget->setMinimumHeight(widget->property("touchOldMinimumHeight").toInt());
        }
        widget->updateGeometry();
    }
    QFont font = qApp->font();
    font.setPointSize(comfortable ? 12 : 10);
    qApp->setFont(font);
}

void PreparePage(QWidget* page) {
    if (!IsComfortable())
        return;
    QList<QWidget*> widgets{page};
    widgets.append(page->findChildren<QWidget*>());
    for (auto* widget : widgets) {
        if (auto* form = qobject_cast<QFormLayout*>(widget->layout())) {
            form->setRowWrapPolicy(QFormLayout::WrapLongRows);
            form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        }
        if (widget->layout() && !widget->property("touchPagePrepared").toBool()) {
            widget->setProperty("touchPagePrepared", true);
            WrapNumbers(widget->layout());
        }
        if (auto* area = qobject_cast<QAbstractScrollArea*>(widget)) {
            QScroller::grabGesture(area->viewport(), QScroller::TouchGesture);
        }
        if (auto* field = qobject_cast<QLineEdit*>(widget)) {
            if (!field->property("touchOldMinimumHeight").isValid()) {
                field->setProperty("touchOldMinimumHeight", field->minimumHeight());
            }
            field->setMinimumHeight(48);
            for (auto* button : field->findChildren<QToolButton*>()) {
                button->setMinimumSize(48, 48);
            }
        }
        if (auto* combo = qobject_cast<QComboBox*>(widget)) {
            combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            combo->setMinimumContentsLength(8);
            combo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        }
        if (auto* button = qobject_cast<QAbstractButton*>(widget)) {
            if (button->maximumWidth() < 100)
                button->setMaximumWidth(QWIDGETSIZE_MAX);
            button->setMinimumWidth(qMax(48, button->minimumWidth()));
        }
        // These existing controller rows contain whole sections, not individual bindings.
        // Changing direction retains every widget and connection while making them scrollable.
        if (!widget->property("touchRowsPrepared").toBool()) {
            widget->setProperty("touchRowsPrepared", true);
            for (auto* row : widget->findChildren<QHBoxLayout*>()) {
                const QString name = row->objectName();
                if (row->parentWidget() == widget &&
                    (name == QStringLiteral("shoulderButtons") ||
                     name == QStringLiteral("miscButtons")) &&
                    !row->property("touchResponsive").toBool()) {
                    row->setProperty("touchResponsive", true);
                    new ResponsiveGrid(row, widget);
                    continue;
                }
                if (row->parentWidget() == widget &&
                    (name == QStringLiteral("top") || name == QStringLiteral("_2") ||
                     (name == QStringLiteral("main") &&
                      widget->objectName() == QStringLiteral("mainInputAdvanced")))) {
                    if (!row->property("touchResponsive").toBool()) {
                        row->setProperty("touchResponsive", true);
                        new ResponsiveRow(row, widget);
                    }
                }
            }
        }
    }
}

void InstallSettingsShell(QDialog* dialog, const QString& title, const QString& subtitle) {
    if (auto* existing = dialog->findChild<QObject*>(QStringLiteral("TouchSettingsShell"),
                                                     Qt::FindDirectChildrenOnly)) {
        static_cast<SettingsShell*>(existing)->SetText(title, subtitle);
        return;
    }
    if (!dialog->layout())
        return;
    dialog->setProperty("forestDialogPrepared", true);
    new SettingsShell(dialog, title, subtitle);
}

void MarkApplied(QDialog* dialog) {
    if (auto* shell = dialog->findChild<QObject*>(QStringLiteral("TouchSettingsShell"),
                                                  Qt::FindDirectChildrenOnly)) {
        static_cast<SettingsShell*>(shell)->Applied();
    }
}

} // namespace QtCommon::TouchUi
