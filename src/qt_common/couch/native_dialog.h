// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDialog>
#include <QPointer>
#include <QQuickWidget>
#include "qt_common/couch/theme.h"
#include "qt_common/couch/widget_model.h"

namespace QtCommon::Couch {
// Preserves the original dialog, validation and accepted/rejected signals under a couch
// presentation.
class NativeDialog final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap state READ State NOTIFY changed)
    Q_PROPERTY(QtCommon::Couch::WidgetModel* settings READ Settings CONSTANT)
    Q_PROPERTY(QtCommon::Couch::Theme* theme READ Theme CONSTANT)
public:
    explicit NativeDialog(QDialog* dialog, Theme* theme, QObject* parent = nullptr);
    QVariantMap State() const;
    WidgetModel* Settings() {
        return &settings;
    }
    QtCommon::Couch::Theme* Theme() {
        return theme;
    }
    Q_INVOKABLE void action(const QString& name, const QVariant& value = {});
    Q_INVOKABLE void refresh();
signals:
    void changed();

private:
    QPointer<QDialog> dialog;
    QtCommon::Couch::Theme* theme;
    WidgetModel settings;
    QStringList files;
    QStringList selected;
};
} // namespace QtCommon::Couch
