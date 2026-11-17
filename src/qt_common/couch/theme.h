// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QVariant>

namespace QtCommon::Couch {
class Theme final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap data READ Data NOTIFY changed)
    Q_PROPERTY(QVariantList installed READ Installed NOTIFY changed)
public:
    explicit Theme(QString directory, QObject* parent = nullptr);
    QVariantMap Data() const {
        return data;
    }
    QVariantList Installed() const;
    bool Load(const QString& id, QString* error = nullptr);
    bool Import(const QString& manifest, QString* installed_id, QString* error = nullptr);
    QString Asset(const QString& name) const;
    static QVariantMap Defaults(bool light = false);
    static bool Validate(const QByteArray& input, const QString& root, QVariantMap* output,
                         QString* error = nullptr);
    QString Directory() const {
        return directory;
    }
signals:
    void changed();

private:
    QString directory;
    QString current_root;
    QVariantMap data;
};
} // namespace QtCommon::Couch
