// SPDX-License-Identifier: GPL-3.0-or-later
#include "qt_common/couch/widget_model.h"

#include <algorithm>
#include <cmath>
#include <QAbstractButton>
#include <QAbstractItemDelegate>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSet>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyleOptionViewItem>
#include <QTabBar>
#include <QTextDocumentFragment>
#include <QTextEdit>
#include <QTimer>
#include <QTreeView>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace QtCommon::Couch {
namespace {
QString Plain(QString text) {
    if (text.contains(QLatin1Char('<')))
        text = QTextDocumentFragment::fromHtml(text).toPlainText();
    return text.replace(QLatin1Char('&'), QString{}).simplified();
}
QString Human(QString text) {
    text.replace(QRegularExpression(QStringLiteral("([a-z0-9])([A-Z])")),
                 QStringLiteral("\\1 \\2"));
    text.replace(QLatin1Char('_'), QLatin1Char(' '));
    return text.simplified();
}
bool Internal(QWidget* widget, QWidget* root) {
    for (auto* parent = widget->parentWidget(); parent && parent != root;
         parent = parent->parentWidget()) {
        if (qobject_cast<QKeySequenceEdit*>(parent) || qobject_cast<QComboBox*>(parent) ||
            qobject_cast<QAbstractSpinBox*>(parent) || qobject_cast<QAbstractItemView*>(parent) ||
            qobject_cast<QDialogButtonBox*>(parent) ||
            parent->property("couchPresentation").toBool())
            return true;
    }
    return qobject_cast<QScrollBar*>(widget) || qobject_cast<QTabBar*>(widget) ||
           widget->objectName().startsWith(QStringLiteral("RestoreButton"));
}
bool Primitive(QWidget* widget) {
    return qobject_cast<QKeySequenceEdit*>(widget) || qobject_cast<QAbstractButton*>(widget) ||
           qobject_cast<QComboBox*>(widget) || qobject_cast<QAbstractSpinBox*>(widget) ||
           qobject_cast<QTextEdit*>(widget) || qobject_cast<QPlainTextEdit*>(widget) ||
           qobject_cast<QLineEdit*>(widget) || qobject_cast<QSlider*>(widget) ||
           qobject_cast<QAbstractItemView*>(widget) ||
           (qobject_cast<QGroupBox*>(widget) && qobject_cast<QGroupBox*>(widget)->isCheckable());
}
} // namespace

WidgetModel::WidgetModel(QObject* parent) : QObject(parent) {
    QFile file(QStringLiteral(":/couch/catalogue.json"));
    if (file.open(QIODevice::ReadOnly)) {
        const auto document = QJsonDocument::fromJson(file.readAll()).object();
        page_metadata = document.value(QStringLiteral("pages")).toArray().toVariantList();
        for (const auto& item : document.value(QStringLiteral("controls")).toArray()) {
            const auto data = item.toObject().toVariantMap();
            catalogue.insert(data.value(QStringLiteral("lookup")).toString(), data);
        }
    }
}

void WidgetModel::Clear() {
    for (const auto& entry : entries)
        if (entry.control)
            disconnect(entry.control, nullptr, this, nullptr);
    entries.clear();
    source_pages.clear();
    source_titles.clear();
    emit sectionsChanged();
    tree_roots.clear();
    emit changed();
}

void WidgetModel::SetPages(const std::vector<SettingsPage>& pages, bool flatten) {
    Clear();
    flat = flatten;
    source_pages = pages;
    for (const auto& page : pages) {
        source_titles.insert(page.id, page.title);
        Collect(page);
    }
    MarkSaved();
    emit sectionsChanged();
}

QVariantMap WidgetModel::Metadata(QWidget* control) const {
    for (QWidget* candidate = control; candidate; candidate = candidate->parentWidget()) {
        const QString key = candidate->property("couchKey").toString();
        if (!key.isEmpty()) {
            const auto found = catalogue.constFind(QStringLiteral("setting:") + key);
            if (found != catalogue.cend())
                return *found;
        }
    }
    for (QWidget* candidate = control; candidate; candidate = candidate->parentWidget()) {
        const QString key = QString::fromLatin1(candidate->metaObject()->className()) +
                            QLatin1Char(':') + control->objectName();
        const auto found = catalogue.constFind(key);
        if (found != catalogue.cend())
            return *found;
    }
    return {};
}

void WidgetModel::Collect(const SettingsPage& page) {
    if (!page.widget)
        return;
    QSet<QWidget*> known;
    for (const auto& entry : entries)
        if (entry.control)
            known.insert(entry.control);
    const auto controls = page.widget->findChildren<QWidget*>();
    for (QWidget* control : controls) {
        if (!Primitive(control) || Internal(control, page.widget))
            continue;
        if (known.contains(control))
            continue;
        const auto metadata = Metadata(control);
        Entry entry;
        entry.control = control;
        entry.owner = page.widget;
        entry.label = Label(control, page.widget);
        if (entry.label.isEmpty())
            entry.label = metadata.value(QStringLiteral("label")).toString();
        if (entry.label.isEmpty())
            entry.label = Human(control->objectName());
        if (entry.label.isEmpty())
            continue;
        entry.help = Plain(control->toolTip());
        if (entry.help.isEmpty()) {
            for (QWidget* ancestor = control->parentWidget(); ancestor && ancestor != page.widget;
                 ancestor = ancestor->parentWidget()) {
                if (!ancestor->toolTip().isEmpty()) {
                    entry.help = Plain(ancestor->toolTip());
                    break;
                }
            }
        }
        if (entry.help.isEmpty())
            entry.help = metadata.value(QStringLiteral("help")).toString();
        entry.section = flat ? page.id : metadata.value(QStringLiteral("page"), page.id).toString();
        entry.group =
            flat ? page.group : metadata.value(QStringLiteral("group"), page.group).toString();
        entry.key = metadata.value(QStringLiteral("lookup"), control->objectName()).toString();
        for (QWidget* ancestor = control; ancestor; ancestor = ancestor->parentWidget()) {
            if (ancestor->property("couchPlayer").isValid()) {
                entry.player = ancestor->property("couchPlayer").toInt();
                break;
            }
        }
        const auto match = QRegularExpression(QStringLiteral("(?:checkbox)?Player(\\d)"))
                               .match(control->objectName());
        if (match.hasMatch())
            entry.player = match.captured(1).toInt() - 1;
        if (flat)
            entry.player = -1;
        if(entry.player>=0 && entry.section==QStringLiteral("bindings") && !metadata.value(QStringLiteral("label")).toString().isEmpty())entry.label=metadata.value(QStringLiteral("label")).toString();
        entry.baseline = Value(entry);
        entry.baseline_inherited = Inherited(entry);
        entries.push_back(std::move(entry));
        Watch(control);
    }
}

QString WidgetModel::Label(QWidget* control, QWidget* page) const {
    if (auto* input = qobject_cast<QInputDialog*>(page))
        return Plain(input->labelText());
    if (qobject_cast<QKeySequenceEdit*>(control))
        return tr("Keyboard shortcut");
    if (!control->accessibleName().isEmpty())
        return Plain(control->accessibleName());
    if (auto* button = qobject_cast<QAbstractButton*>(control)) {
        if (!button->text().isEmpty() && !control->property("couchBinding").toBool())
            return Plain(button->text());
    }
    if (auto* box = qobject_cast<QGroupBox*>(control))
        return Plain(box->title());
    for (auto* label : page->findChildren<QLabel*>())
        if (label->buddy() == control)
            return Plain(label->text());
    for (QWidget* ancestor = control; ancestor; ancestor = ancestor->parentWidget()) {
        if (!ancestor->property("couchLabel").toString().isEmpty())
            return Plain(ancestor->property("couchLabel").toString());
        if (ancestor != control && ancestor->layout()) {
            if (auto* form = qobject_cast<QFormLayout*>(ancestor->layout())) {
                if (auto* label = qobject_cast<QLabel*>(form->labelForField(control)))
                    return Plain(label->text());
            }
        }
        if (ancestor == page)
            break;
    }
    std::function<QString(QLayout*)> nearby = [&](QLayout* layout) -> QString {
        if (!layout)
            return {};
        const int index = layout->indexOf(control);
        if (index >= 0) {
            if (auto* grid = qobject_cast<QGridLayout*>(layout)) {
                int row, col, rs, cs;
                grid->getItemPosition(index, &row, &col, &rs, &cs);
                for (int previous = col - 1; previous >= 0; --previous)
                    if (auto* item = grid->itemAtPosition(row, previous))
                        if (auto* label = qobject_cast<QLabel*>(item->widget()))
                            return Plain(label->text());
            } else
                for (int previous = index - 1; previous >= 0; --previous)
                    if (auto* label = qobject_cast<QLabel*>(layout->itemAt(previous)->widget()))
                        return Plain(label->text());
        }
        for (int child = 0; child < layout->count(); ++child)
            if (auto* nested = layout->itemAt(child)->layout()) {
                const auto found = nearby(nested);
                if (!found.isEmpty())
                    return found;
            }
        return {};
    };
    for (auto* ancestor = control->parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
        const auto found = nearby(ancestor->layout());
        if (!found.isEmpty())
            return found;
        if (ancestor == page)
            break;
    }
    const auto metadata = Metadata(control);
    if (metadata.contains(QStringLiteral("label")))
        return metadata.value(QStringLiteral("label")).toString();
    const auto name = Human(control->objectName());
    if (!name.isEmpty())
        return name;
    if (qobject_cast<QAbstractItemView*>(control))
        return tr("Items");
    if (qobject_cast<QLineEdit*>(control) || qobject_cast<QTextEdit*>(control) ||
        qobject_cast<QPlainTextEdit*>(control))
        return tr("Text");
    if (qobject_cast<QComboBox*>(control))
        return tr("Choice");
    return {};
}

QWidget* WidgetModel::Managed(const Entry& entry) const {
    for (auto* ancestor = entry.control.data(); ancestor; ancestor = ancestor->parentWidget())
        if (ancestor->metaObject()->indexOfMethod("CouchTouch()") >= 0)
            return ancestor;
    return nullptr;
}

bool WidgetModel::Inherited(const Entry& entry) const {
    bool inherited = false;
    if (auto* managed = Managed(entry))
        QMetaObject::invokeMethod(managed, "CouchUsingGlobal", Qt::DirectConnection,
                                  Q_RETURN_ARG(bool, inherited));
    return inherited;
}

bool WidgetModel::Available(const Entry& entry) const {
    if(!entry.control || !entry.control->isEnabled())return false;
    if(entry.player>=0)for(auto* widget=entry.control.data();widget && widget!=entry.owner;widget=widget->parentWidget()) {
        if(widget->property("couchPlayer").isValid())break;
        if(widget->testAttribute(Qt::WA_WState_ExplicitShowHide) && widget->isHidden() &&
           !qobject_cast<QStackedWidget*>(widget->parentWidget()))return false;
    }
    return true;
}
QString WidgetModel::Kind(const Entry& entry) const {
    auto* widget = entry.control.data();
    if (!widget)
        return QStringLiteral("info");
    if (auto* spin = qobject_cast<QAbstractSpinBox*>(widget); spin && spin->isReadOnly())
        return QStringLiteral("info");
    if (qobject_cast<QKeySequenceEdit*>(widget))
        return QStringLiteral("text");
    if (qobject_cast<QDateTimeEdit*>(widget))
        return QStringLiteral("date");
    if (qobject_cast<QSpinBox*>(widget) || qobject_cast<QDoubleSpinBox*>(widget))
        return QStringLiteral("number");
    if (qobject_cast<QSlider*>(widget))
        return QStringLiteral("slider");
    if (qobject_cast<QComboBox*>(widget))
        return QStringLiteral("select");
    if (auto* line = qobject_cast<QLineEdit*>(widget))
        return line->isReadOnly() ? QStringLiteral("info") : QStringLiteral("text");
    if (auto* text = qobject_cast<QTextEdit*>(widget))
        return text->isReadOnly() ? QStringLiteral("info") : QStringLiteral("text");
    if (auto* text = qobject_cast<QPlainTextEdit*>(widget))
        return text->isReadOnly() ? QStringLiteral("info") : QStringLiteral("text");
    if (qobject_cast<QGroupBox*>(widget))
        return QStringLiteral("toggle");
    if (auto* button = qobject_cast<QAbstractButton*>(widget)) {
        if (button->isCheckable())
            return QStringLiteral("toggle");
        return widget->property("couchBinding").toBool() ? QStringLiteral("binding")
                                                         : QStringLiteral("action");
    }
    if (qobject_cast<QAbstractItemView*>(widget))
        return QStringLiteral("tree");
    return QStringLiteral("info");
}

QVariant WidgetModel::Value(const Entry& entry) const {
    auto* widget = entry.control.data();
    if (!widget)
        return {};
    if (auto* keys = qobject_cast<QKeySequenceEdit*>(widget))
        return keys->keySequence().toString(QKeySequence::PortableText);
    if (auto* box = qobject_cast<QDateTimeEdit*>(widget))
        return box->dateTime().toString(Qt::ISODateWithMs);
    if (auto* box = qobject_cast<QSpinBox*>(widget))
        return box->value();
    if (auto* box = qobject_cast<QDoubleSpinBox*>(widget))
        return box->value();
    if (auto* slider = qobject_cast<QSlider*>(widget)) {
        if (slider->property("couchMinimum").isValid())
            return slider->property("couchMinimum").toDouble() +
                   slider->property("couchStep").toDouble() * slider->value();
        return slider->value();
    }
    if (auto* combo = qobject_cast<QComboBox*>(widget))
        return combo->currentIndex();
    if (auto* line = qobject_cast<QLineEdit*>(widget))
        return line->text();
    if (auto* text = qobject_cast<QTextEdit*>(widget))
        return text->toPlainText();
    if (auto* text = qobject_cast<QPlainTextEdit*>(widget))
        return text->toPlainText();
    if (auto* group = qobject_cast<QGroupBox*>(widget))
        return group->isChecked();
    if (auto* button = qobject_cast<QAbstractButton*>(widget)) {
        if (button->isCheckable())
            return button->isChecked();
        if (widget->property("couchBinding").toBool())
            return button->text();
    }
    if (auto* view = qobject_cast<QAbstractItemView*>(widget)) {
        QVariantList values;
        std::function<void(const QModelIndex&)> visit = [&](const QModelIndex& parent) {
            for (int row = 0; row < view->model()->rowCount(parent); ++row) {
                for (int col = 0; col < view->model()->columnCount(parent); ++col) {
                    const auto index = view->model()->index(row, col, parent);
                    values.push_back(index.data(Qt::DisplayRole));
                    values.push_back(index.data(Qt::CheckStateRole));
                }
                visit(view->model()->index(row, 0, parent));
            }
        };
        if (view->model())
            visit({});
        return values;
    }
    return {};
}

QVariantMap WidgetModel::Describe(int id) const {
    if (id < 0 || static_cast<size_t>(id) >= entries.size() || !entries[id].control)
        return {};
    const auto& entry = entries[id];
    auto* widget = entry.control.data();
    const QString kind = Kind(entry);
    const QVariant value = Value(entry);
    QString display = value.toString();
    if(auto* spin=qobject_cast<QAbstractSpinBox*>(widget))display=spin->text();
    if (kind == QStringLiteral("toggle"))
        display = value.toBool() ? tr("On") : tr("Off");
    if (kind == QStringLiteral("action"))
        display = tr("Open");
    if (kind == QStringLiteral("tree"))
        display = tr("Manage");
    if (auto* combo = qobject_cast<QComboBox*>(widget))
        display = combo->currentText();
    if (auto* date = qobject_cast<QDateTimeEdit*>(widget))
        display = date->dateTime().toString(date->displayFormat());
    bool secret = false;
    if (auto* line = qobject_cast<QLineEdit*>(widget)) {
        secret = line->echoMode() != QLineEdit::Normal;
        if (secret && !display.isEmpty())
            display = QStringLiteral("••••••••");
    }
    if (display.isEmpty())
        display = tr("Not set");
    QVariantMap data{
        {QStringLiteral("id"), id},
        {QStringLiteral("label"), entry.label},
        {QStringLiteral("help"), entry.help},
        {QStringLiteral("kind"), kind},
        {QStringLiteral("value"), value},
        {QStringLiteral("display"), display},
        {QStringLiteral("section"), entry.section},
        {QStringLiteral("group"), entry.group},
        {QStringLiteral("enabled"), Available(entry)},
        {QStringLiteral("secret"), secret},
        {QStringLiteral("dirty"), IsChanged(entry)},
        {QStringLiteral("inherited"), Inherited(entry)},
        {QStringLiteral("canInherit"),
         Managed(entry) != nullptr && Managed(entry)->property("couchAllowInherit").toBool()},
        {QStringLiteral("player"), entry.player}};
    if (auto* combo = qobject_cast<QComboBox*>(widget)) {
        QVariantList choices;
        for (int index = 0; index < combo->count(); ++index)
            choices.push_back(
                QVariantMap{{QStringLiteral("label"), combo->itemText(index)},
                            {QStringLiteral("value"), index},
                            {QStringLiteral("enabled"),
                             bool(combo->model()->flags(combo->model()->index(index, 0)) &
                                  Qt::ItemIsEnabled)}});
        data.insert(QStringLiteral("choices"), choices);
    }
    if (auto* box = qobject_cast<QSpinBox*>(widget)) {
        data.insert(QStringLiteral("minimum"), box->minimum());
        data.insert(QStringLiteral("maximum"), box->maximum());
        data.insert(QStringLiteral("step"), box->singleStep());
        data.insert(QStringLiteral("suffix"), box->suffix());
        data.insert(QStringLiteral("decimals"), 0);
    } else if (auto* double_box = qobject_cast<QDoubleSpinBox*>(widget)) {
        data.insert(QStringLiteral("minimum"), double_box->minimum());
        data.insert(QStringLiteral("maximum"), double_box->maximum());
        data.insert(QStringLiteral("step"), double_box->singleStep());
        data.insert(QStringLiteral("suffix"), double_box->suffix());
        data.insert(QStringLiteral("decimals"), double_box->decimals());
    } else if (auto* slider = qobject_cast<QSlider*>(widget)) {
        const bool transformed = slider->property("couchMinimum").isValid();
        data.insert(QStringLiteral("minimum"),
                    transformed ? slider->property("couchMinimum") : QVariant(slider->minimum()));
        data.insert(QStringLiteral("maximum"),
                    transformed ? slider->property("couchMaximum") : QVariant(slider->maximum()));
        data.insert(QStringLiteral("step"),
                    transformed ? slider->property("couchStep") : QVariant(slider->singleStep()));
        data.insert(QStringLiteral("decimals"), transformed ? 6 : 0);
        if(transformed)data[QStringLiteral("display")]=QString::number(value.toDouble(),'g',6);
    }
    if (auto* date = qobject_cast<QDateTimeEdit*>(widget)) {
        data.insert(QStringLiteral("seconds"), date->displayFormat().contains(QLatin1Char('s')));
        data.insert(QStringLiteral("dateMode"),
                    qobject_cast<QDateEdit*>(widget)   ? QStringLiteral("date")
                    : qobject_cast<QTimeEdit*>(widget) ? QStringLiteral("time")
                                                       : QStringLiteral("both"));
        data.insert(QStringLiteral("minimumDate"),
                    date->minimumDateTime().toString(Qt::ISODateWithMs));
        data.insert(QStringLiteral("maximumDate"),
                    date->maximumDateTime().toString(Qt::ISODateWithMs));
    }
    data.insert(QStringLiteral("context"), widget->contextMenuPolicy() == Qt::CustomContextMenu);
    data.insert(QStringLiteral("canReset"), Managed(entry) != nullptr);
    if (auto* line = qobject_cast<QLineEdit*>(widget))
        data.insert(QStringLiteral("maximumLength"), line->maxLength());
    return data;
}

QVariantList WidgetModel::Rows() const {
    QVariantList result;
    const auto terms = query.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (size_t index = 0; index < entries.size(); ++index) {
        const auto& entry = entries[index];
        if (!entry.control || (entry.player >= 0 && entry.player != player))
            continue;
        if (query.isEmpty() && entry.section != section)
            continue;
        const QString text = entry.label + QLatin1Char(' ') + entry.help + QLatin1Char(' ') +
                             entry.section + QLatin1Char(' ') + entry.group;
        bool matches = true;
        for (const auto& term : terms)
            matches &= text.contains(term, Qt::CaseInsensitive);
        if (matches)
            result.push_back(Describe(static_cast<int>(index)));
    }
    if(query.isEmpty() && section==QStringLiteral("bindings")) {
        static const QStringList order{
            QStringLiteral("buttonA"),QStringLiteral("buttonB"),QStringLiteral("buttonX"),QStringLiteral("buttonY"),
            QStringLiteral("buttonL"),QStringLiteral("buttonR"),QStringLiteral("buttonZL"),QStringLiteral("buttonZR"),
            QStringLiteral("buttonPlus"),QStringLiteral("buttonMinus"),QStringLiteral("buttonLStick"),QStringLiteral("buttonRStick"),
            QStringLiteral("buttonDpadUp"),QStringLiteral("buttonDpadDown"),QStringLiteral("buttonDpadLeft"),QStringLiteral("buttonDpadRight"),
            QStringLiteral("buttonHome"),QStringLiteral("buttonScreenshot"),QStringLiteral("buttonSLLeft"),QStringLiteral("buttonSRLeft"),
            QStringLiteral("buttonSLRight"),QStringLiteral("buttonSRRight"),QStringLiteral("buttonLStickUp"),QStringLiteral("buttonLStickDown"),
            QStringLiteral("buttonLStickLeft"),QStringLiteral("buttonLStickRight"),QStringLiteral("buttonRStickUp"),QStringLiteral("buttonRStickDown"),
            QStringLiteral("buttonRStickLeft"),QStringLiteral("buttonRStickRight"),QStringLiteral("buttonMotionLeft"),QStringLiteral("buttonMotionRight")};
        const auto rank=[&](const QVariant& value){auto* widget=Control(value.toMap().value(QStringLiteral("id")).toInt());const auto index=widget?order.indexOf(widget->objectName()):-1;return index<0?1000:index;};
        std::stable_sort(result.begin(),result.end(),[&](const QVariant& a,const QVariant& b){return rank(a)<rank(b);});
    }
    return result;
}

QVariantList WidgetModel::Sections() const {
    QVariantList result;
    QStringList present;
    for (const auto& page : page_metadata) {
        auto data = page.toMap();
        const auto id = data.value(QStringLiteral("id")).toString();
        if (source_titles.contains(id))
            data[QStringLiteral("label")] = source_titles.value(id);
        for (const auto& entry : entries) {
            if (entry.section == id && !present.contains(id)) {
                result.push_back(data);
                present.push_back(id);
                break;
            }
        }
    }
    for (const auto& entry : entries) {
        if (present.contains(entry.section))
            continue;
        result.push_back(QVariantMap{
            {QStringLiteral("id"), entry.section},
            {QStringLiteral("label"), source_titles.value(entry.section, Human(entry.section))},
            {QStringLiteral("group"), entry.group}});
        present.push_back(entry.section);
    }
    return result;
}

bool WidgetModel::IsChanged(const Entry& entry) const {
    const QString kind = Kind(entry);
    return (kind != QStringLiteral("action") && kind != QStringLiteral("info")) &&
           (Value(entry) != entry.baseline || Inherited(entry) != entry.baseline_inherited);
}
bool WidgetModel::Dirty() const {
    for (const auto& entry : entries)
        if (IsChanged(entry))
            return true;
    return false;
}
QVariantList WidgetModel::Changes() const {
    QVariantList result;
    for (size_t id = 0; id < entries.size(); ++id)
        if (IsChanged(entries[id]))
            result.push_back(Describe(static_cast<int>(id)));
    return result;
}
QWidget* WidgetModel::Control(int id) const {
    return id >= 0 && static_cast<size_t>(id) < entries.size() ? entries[id].control.data()
                                                               : nullptr;
}
void WidgetModel::Select(const QString& selected, int selected_player, const QString& search) {
    section = selected;
    player = selected_player;
    query = search;
    emit changed();
}
void WidgetModel::Refresh() {
    if (refreshing)
        return;
    refreshing = true;
    QTimer::singleShot(0, this, [this] {
        refreshing = false;
        const auto count = entries.size();
        for (const auto& page : source_pages)
            Collect(page);
        if (count != entries.size())
            emit sectionsChanged();
        emit changed();
    });
}
void WidgetModel::MarkSaved() {
    for (auto& entry : entries) {
        entry.baseline = Value(entry);
        entry.baseline_inherited = Inherited(entry);
    }
    emit changed();
}

void WidgetModel::Watch(QWidget* widget) {
    if (auto* text = qobject_cast<QTextEdit*>(widget))
        connect(text, &QTextEdit::textChanged, this, &WidgetModel::Refresh);
    if (auto* text = qobject_cast<QPlainTextEdit*>(widget))
        connect(text, &QPlainTextEdit::textChanged, this, &WidgetModel::Refresh);
    if (auto* keys = qobject_cast<QKeySequenceEdit*>(widget))
        connect(keys, &QKeySequenceEdit::keySequenceChanged, this, &WidgetModel::Refresh);
    if (auto* box = qobject_cast<QComboBox*>(widget))
        connect(box, &QComboBox::currentIndexChanged, this, &WidgetModel::Refresh);
    if (auto* box = qobject_cast<QSpinBox*>(widget))
        connect(box, &QSpinBox::valueChanged, this, &WidgetModel::Refresh);
    if (auto* box = qobject_cast<QDoubleSpinBox*>(widget))
        connect(box, &QDoubleSpinBox::valueChanged, this, &WidgetModel::Refresh);
    if (auto* slider = qobject_cast<QSlider*>(widget))
        connect(slider, &QSlider::valueChanged, this, &WidgetModel::Refresh);
    if (auto* line = qobject_cast<QLineEdit*>(widget))
        connect(line, &QLineEdit::textChanged, this, &WidgetModel::Refresh);
    if (auto* button = qobject_cast<QAbstractButton*>(widget))
        connect(button, &QAbstractButton::toggled, this, &WidgetModel::Refresh);
    if (auto* group = qobject_cast<QGroupBox*>(widget))
        connect(group, &QGroupBox::toggled, this, &WidgetModel::Refresh);
    if (auto* date = qobject_cast<QDateTimeEdit*>(widget))
        connect(date, &QDateTimeEdit::dateTimeChanged, this, &WidgetModel::Refresh);
    if (auto* view = qobject_cast<QAbstractItemView*>(widget))
        if (view->model()) {
            connect(view->model(), &QAbstractItemModel::dataChanged, this, &WidgetModel::Refresh);
            connect(view->model(), &QAbstractItemModel::modelReset, this, &WidgetModel::Refresh);
            connect(view->model(), &QAbstractItemModel::rowsInserted, this, &WidgetModel::Refresh);
            connect(view->model(), &QAbstractItemModel::rowsRemoved, this, &WidgetModel::Refresh);
        }
    connect(widget, &QObject::destroyed, this, &WidgetModel::Refresh);
}

bool WidgetModel::Stage(int id, const QVariant& value, QString* error) {
    auto* control = Control(id);
    if (!control || !Available(entries[id]))
        return false;
    if (auto* spin = qobject_cast<QAbstractSpinBox*>(control); spin && spin->isReadOnly())
        return false;
    const auto fail = [&] {
        if (error)
            *error = tr("The value is outside this setting's allowed range or format.");
        return false;
    };
    bool numeric_ok = false;
    const double number = value.toDouble(&numeric_ok);
    if (auto* keys = qobject_cast<QKeySequenceEdit*>(control)) {
        const auto sequence =
            QKeySequence::fromString(value.toString(), QKeySequence::PortableText);
        if (sequence.isEmpty() && !value.toString().isEmpty())
            return fail();
        keys->setKeySequence(sequence);
    } else if (auto* date = qobject_cast<QDateTimeEdit*>(control)) {
        const auto parsed = QDateTime::fromString(value.toString(), Qt::ISODateWithMs);
        if (!parsed.isValid() || parsed < date->minimumDateTime() ||
            parsed > date->maximumDateTime())
            return fail();
        date->setDateTime(parsed);
    } else if (auto* spin = qobject_cast<QSpinBox*>(control)) {
        if (!numeric_ok || !std::isfinite(number) || std::floor(number) != number ||
            number < spin->minimum() || number > spin->maximum())
            return fail();
        spin->setValue(static_cast<int>(number));
    } else if (auto* double_spin = qobject_cast<QDoubleSpinBox*>(control)) {
        if (!numeric_ok || !std::isfinite(number) || number < double_spin->minimum() ||
            number > double_spin->maximum())
            return fail();
        double_spin->setValue(number);
    } else if (auto* slider = qobject_cast<QSlider*>(control)) {
        const bool transformed = slider->property("couchMinimum").isValid();
        const double minimum =
            transformed ? slider->property("couchMinimum").toDouble() : slider->minimum();
        const double maximum =
            transformed ? slider->property("couchMaximum").toDouble() : slider->maximum();
        if (!numeric_ok || !std::isfinite(number) || number < minimum || number > maximum || (!transformed && std::floor(number)!=number))
            return fail();
        slider->setValue(transformed
                             ? static_cast<int>(std::lround(
                                   (number - minimum) / slider->property("couchStep").toDouble()))
                             : static_cast<int>(number));
    } else if (auto* combo = qobject_cast<QComboBox*>(control)) {
        if (!numeric_ok || std::floor(number) != number || number < 0 || number >= combo->count())
            return fail();
        const int index = static_cast<int>(number);
        if (!(combo->model()->flags(combo->model()->index(index, 0)) & Qt::ItemIsEnabled))
            return fail();
        combo->setCurrentIndex(index);
        emit combo->activated(index);
    } else if (auto* rich_text = qobject_cast<QTextEdit*>(control)) {
        if (rich_text->isReadOnly() || value.toString().size() > 65536)
            return fail();
        rich_text->setPlainText(value.toString());
    } else if (auto* plain_text = qobject_cast<QPlainTextEdit*>(control)) {
        if (plain_text->isReadOnly() || value.toString().size() > 65536)
            return fail();
        plain_text->setPlainText(value.toString());
    } else if (auto* line = qobject_cast<QLineEdit*>(control)) {
        if (line->isReadOnly() || value.toString().size() > line->maxLength())
            return fail();
        QString text = value.toString();
        int pos = text.size();
        if (line->validator() && line->validator()->validate(text, pos) != QValidator::Acceptable)
            return fail();
        line->setText(text);
        emit line->textEdited(text);
        emit line->editingFinished();
    } else if (auto* button = qobject_cast<QAbstractButton*>(control)) {
        if (!button->isCheckable())
            return false;
        if (button->isChecked() != value.toBool())
            button->click();
    } else if (auto* group = qobject_cast<QGroupBox*>(control)) {
        group->setChecked(value.toBool());
        emit group->clicked(value.toBool());
    } else
        return false;
    if (auto* managed = Managed(entries[id]))
        QMetaObject::invokeMethod(managed, "CouchTouch", Qt::DirectConnection);
    Refresh();
    return true;
}

bool WidgetModel::Reset(int id, bool inherit) {
    if (!Control(id))
        return false;
    if (auto* managed = Managed(entries[id])) {
        if (inherit)
            QMetaObject::invokeMethod(managed, "CouchUseGlobal", Qt::DirectConnection);
        else
            QMetaObject::invokeMethod(managed, "CouchReset", Qt::DirectConnection,
                                      Q_ARG(QObject*, entries[id].control.data()));
        Refresh();
        return true;
    }
    return false;
}
void WidgetModel::Restore() {
    // The owning native dialog is recreated on Discard; this is also useful for simple controls in
    // tests.
    for (size_t id = 0; id < entries.size(); ++id)
        if (IsChanged(entries[id]))
            Stage(static_cast<int>(id), entries[id].baseline);
    MarkSaved();
}
void WidgetModel::treeContext(int id, int row, int column) {
    auto* view = qobject_cast<QAbstractItemView*>(Control(id));
    if (!view || !view->model() || row < 0 || view->contextMenuPolicy() != Qt::CustomContextMenu)
        return;
    const auto index = view->model()->index(row, column, tree_roots.value(id, view->rootIndex()));
    if (!index.isValid() || !(index.flags() & Qt::ItemIsEnabled))
        return;
    if (auto* tree = qobject_cast<QTreeView*>(view))
        for (auto parent = index.parent(); parent.isValid(); parent = parent.parent())
            tree->expand(parent);
    view->setCurrentIndex(index);
    view->doItemsLayout();
    view->scrollTo(index);
    emit view->customContextMenuRequested(view->visualRect(index).center());
}
void WidgetModel::context(int id) {
    if (auto* control = Control(id))
        if (control->contextMenuPolicy() == Qt::CustomContextMenu)
            emit control->customContextMenuRequested(control->rect().center());
}
void WidgetModel::Activate(int id) {
    if (activation_gate && Control(id) && Control(id)->property("couchBinding").toBool()) {
        QPointer<WidgetModel> safe = this;
        activation_gate([safe, id] {
            if (safe)
                safe->TriggerControl(id);
        });
        return;
    }
    TriggerControl(id);
}
void WidgetModel::TriggerControl(int id) {
    if (auto* button = qobject_cast<QAbstractButton*>(Control(id))) {
        emit ActionTriggered(id);
        if (auto* push = qobject_cast<QPushButton*>(button); push && push->menu())
            push->showMenu();
        else
            button->click();
        Refresh();
    }
}

QVariantList WidgetModel::TreeRows(int id, const QModelIndex& given_root) const {
    auto* view = qobject_cast<QAbstractItemView*>(Control(id));
    if (!view || !view->model())
        return {};
    const auto root = given_root.isValid() ? given_root : tree_roots.value(id, view->rootIndex());
    QVariantList result;
    if (root.isValid())
        result.push_back(QVariantMap{{QStringLiteral("index"), -1},
                                     {QStringLiteral("columns"), QVariantList{tr("← Parent")}},
                                     {QStringLiteral("label"), tr("Parent")},
                                     {QStringLiteral("children"), true}});
    for (int row = 0; row < view->model()->rowCount(root); ++row) {
        QVariantList columns;
        for (int col = 0; col < view->model()->columnCount(root); ++col)
            columns.push_back(Plain(view->model()->index(row, col, root).data().toString()));
        const auto index = view->model()->index(row, 0, root);
        result.push_back(QVariantMap{
            {QStringLiteral("index"), row},
            {QStringLiteral("columns"), columns},
            {QStringLiteral("label"), columns.value(0)},
            {QStringLiteral("children"), view->model()->hasChildren(index)},
            {QStringLiteral("checked"), index.data(Qt::CheckStateRole)},
            {QStringLiteral("checkable"), bool(index.flags() & Qt::ItemIsUserCheckable)}});
    }
    return result;
}
void WidgetModel::SetTreeRoot(int id, const QModelIndex& root) {
    tree_roots[id] = root;
}
void WidgetModel::ActivateTree(int id, int row, int column, bool edit) {
    if (activation_gate && edit &&
        (column == 2 || (Control(id) && Control(id)->property("couchBindingColumn").isValid() &&
                         Control(id)->property("couchBindingColumn").toInt() == column))) {
        QPointer<WidgetModel> safe = this;
        activation_gate([safe, id, row, column, edit] {
            if (safe)
                safe->RunTreeAction(id, row, column, edit);
        });
        return;
    }
    RunTreeAction(id, row, column, edit);
}
void WidgetModel::RunTreeAction(int id, int row, int column, bool edit) {
    auto* view = qobject_cast<QAbstractItemView*>(Control(id));
    if (!view || !view->model())
        return;
    if (row < 0) {
        tree_roots[id] = tree_roots.value(id, view->rootIndex()).parent();
        Refresh();
        return;
    }
    const auto index = view->model()->index(row, column, tree_roots.value(id, view->rootIndex()));
    if (!index.isValid() || !(index.flags() & Qt::ItemIsEnabled))
        return;
    view->setCurrentIndex(index);
    emit view->clicked(index);
    if (!edit && view->model()->hasChildren(index)) {
        tree_roots[id] = index;
        return;
    }
    if (index.flags() & Qt::ItemIsUserCheckable) {
        view->model()->setData(index,
                               index.data(Qt::CheckStateRole).toInt() == Qt::Checked ? Qt::Unchecked
                                                                                     : Qt::Checked,
                               Qt::CheckStateRole);
    } else if (index.flags() & Qt::ItemIsEditable) {
        auto* dialog = new QDialog(view->window());
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle(tr("Edit %1").arg(index.data().toString()));
        auto* layout = new QVBoxLayout(dialog);
        auto* delegate = view->itemDelegateForIndex(index);
        QStyleOptionViewItem option;
        auto* editor = delegate->createEditor(dialog, option, index);
        if (!editor) {
            dialog->deleteLater();
            return;
        }
        delegate->setEditorData(editor, index);
        editor->setAccessibleName(tr("Value"));
        layout->addWidget(editor);
        auto* buttons =
            new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
        layout->addWidget(buttons);
        const QPersistentModelIndex persistent = index;
        connect(buttons, &QDialogButtonBox::accepted, dialog,
                [this, dialog, delegate, editor, persistent, view] {
                    if (persistent.isValid())
                        delegate->setModelData(editor, view->model(), persistent);
                    dialog->accept();
                    Refresh();
                });
        connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
        dialog->setWindowModality(Qt::ApplicationModal);
        dialog->show();
    } else if (auto* tree = qobject_cast<QTreeWidget*>(view)) {
        emit tree->itemDoubleClicked(tree->currentItem(), column);
    } else
        emit view->doubleClicked(index);
    Refresh();
}
} // namespace QtCommon::Couch
