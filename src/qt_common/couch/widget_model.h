// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <vector>
#include <QHash>
#include <QModelIndex>
#include <QObject>
#include <QPointer>
#include <QVariant>

class QWidget;
class QAbstractItemView;

namespace QtCommon::Couch {

struct SettingsPage {
    QWidget* widget{};
    QString id;
    QString title;
    QString group;
};

// A second presentation of the existing controls, never a second configuration store.
class WidgetModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList rows READ Rows NOTIFY changed)
    Q_PROPERTY(QVariantList sections READ Sections NOTIFY sectionsChanged)
    Q_PROPERTY(QVariantList changes READ Changes NOTIFY changed)
    Q_PROPERTY(bool dirty READ Dirty NOTIFY changed)
public:
    explicit WidgetModel(QObject* parent = nullptr);
    void SetPages(const std::vector<SettingsPage>& pages, bool flatten = false);
    void Clear();
    void Refresh();
    void MarkSaved();
    void Restore();
    void Select(const QString& section, int player, const QString& search = {});
    QVariantList Rows() const;
    QVariantList Sections() const;
    QVariantList Changes() const;
    bool Dirty() const;
    Q_INVOKABLE QVariantMap describe(int id) const {
        return Describe(id);
    }
    Q_INVOKABLE bool stage(int id, const QVariant& value) {
        return Stage(id, value);
    }
    Q_INVOKABLE bool reset(int id, bool inherit) {
        return Reset(id, inherit);
    }
    Q_INVOKABLE void activate(int id) {
        Activate(id);
    }
    Q_INVOKABLE QVariantList treeRows(int id) const {
        return TreeRows(id);
    }
    Q_INVOKABLE void treeAction(int id, int row, int column, bool edit) {
        ActivateTree(id, row, column, edit);
    }
    QVariantMap Describe(int id) const;
    QWidget* Control(int id) const;
    bool Stage(int id, const QVariant& value, QString* error = nullptr);
    bool Reset(int id, bool inherit);
    void Activate(int id);
    Q_INVOKABLE void context(int id);
    Q_INVOKABLE void treeContext(int id, int row, int column);
    void SetActivationGate(std::function<void(std::function<void()>)> gate) {
        activation_gate = std::move(gate);
    }
    QVariantList TreeRows(int id, const QModelIndex& root = {}) const;
    void ActivateTree(int id, int row, int column = 0, bool edit = false);
    void SetTreeRoot(int id, const QModelIndex& root);

signals:
    void changed();
    void sectionsChanged();
    void ActionTriggered(int id);

private:
    void TriggerControl(int id);
    void RunTreeAction(int id, int row, int column, bool edit);
    std::function<void(std::function<void()>)> activation_gate;
    struct Entry {
        QPointer<QWidget> control;
        QPointer<QWidget> owner;
        QString label;
        QString help;
        QString section;
        QString group;
        QString key;
        int player{-1};
        QVariant baseline;
        bool baseline_inherited{};
    };
    bool Available(const Entry& entry) const;
    QVariant Value(const Entry& entry) const;
    QString Kind(const Entry& entry) const;
    bool Inherited(const Entry& entry) const;
    QWidget* Managed(const Entry& entry) const;
    QString Label(QWidget* control, QWidget* page) const;
    void Watch(QWidget* control);
    void Collect(const SettingsPage& page);
    QVariantMap Metadata(QWidget* control) const;
    bool IsChanged(const Entry& entry) const;
    std::vector<Entry> entries;
    std::vector<SettingsPage> source_pages;
    bool flat{};
    QVariantList page_metadata;
    QHash<QString, QString> source_titles;
    QHash<QString, QVariantMap> catalogue;
    QHash<int, QModelIndex> tree_roots;
    QString section;
    QString query;
    int player{};
    bool refreshing{};
};
} // namespace QtCommon::Couch
