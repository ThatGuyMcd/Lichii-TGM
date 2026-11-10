// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QCache>
#include <QFutureWatcher>
#include <QImage>
#include <QWidget>

namespace QtCommon {

class CoverArt final : public QWidget {
    Q_OBJECT
public:
    explicit CoverArt(QWidget* parent = nullptr);
    void SetSource(const QString& identity, const QByteArray& encoded);
    const QImage& CurrentImage() const {
        return current;
    }
    QRect ImageRect() const;

signals:
    void ArtChanged();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    void LoadPending();
    QString wanted_key;
    QString loading_key;
    QByteArray pending;
    QImage current;
    QCache<QString, QImage> cache{16 * 1024}; // KiB, decoded images only.
    QFutureWatcher<QImage> watcher;
};

} // namespace QtCommon
