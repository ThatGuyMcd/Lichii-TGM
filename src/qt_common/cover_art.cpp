// SPDX-License-Identifier: GPL-3.0-or-later

#include "qt_common/cover_art.h"

#include <utility>
#include <QBuffer>
#include <QCryptographicHash>
#include <QImageReader>
#include <QPainter>
#include <QtConcurrentRun>

namespace QtCommon {

CoverArt::CoverArt(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("SelectedGameCover"));
    setMinimumSize(160, 160);
    setMaximumHeight(320);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setAccessibleName(tr("Selected game cover artwork"));
    connect(&watcher, &QFutureWatcher<QImage>::finished, this, [this] {
        const QImage image = watcher.result();
        if (!image.isNull()) {
            cache.insert(loading_key, new QImage(image), qMax(1, int(image.sizeInBytes() / 1024)));
        }
        if (loading_key == wanted_key) {
            current = image;
            update();
            emit ArtChanged();
        }
        loading_key.clear();
        LoadPending();
    });
}

void CoverArt::SetSource(const QString& identity, const QByteArray& encoded) {
    const QString key =
        identity.isEmpty() || encoded.isEmpty() || encoded.size() > 4 * 1024 * 1024
            ? QString{}
            : identity + QLatin1Char(':') +
                  QString::fromLatin1(
                      QCryptographicHash::hash(encoded, QCryptographicHash::Sha256).toHex());
    if (key == wanted_key)
        return;
    wanted_key = key;
    current = {};
    pending = {};
    if (!key.isEmpty()) {
        if (const auto* image = cache.object(key)) {
            current = *image;
        } else {
            pending = encoded;
        }
    }
    update();
    emit ArtChanged();
    LoadPending();
}

void CoverArt::LoadPending() {
    if (!loading_key.isEmpty() || pending.isEmpty())
        return;
    loading_key = wanted_key;
    const QByteArray data = std::exchange(pending, {});
    watcher.setFuture(QtConcurrent::run([data] {
        QBuffer buffer;
        buffer.setData(data);
        if (!buffer.open(QIODevice::ReadOnly))
            return QImage{};
        QImageReader reader(&buffer);
        reader.setDecideFormatFromContent(true);
        const QSize size = reader.size();
        if (!size.isValid() || qint64(size.width()) * size.height() > 16 * 1024 * 1024)
            return QImage{};
        if (size.width() > 1024 || size.height() > 1024) {
            reader.setScaledSize(size.scaled(1024, 1024, Qt::KeepAspectRatio));
        }
        return reader.read();
    }));
}

QRect CoverArt::ImageRect() const {
    if (current.isNull())
        return {};
    const QSize fitted = current.size().scaled(size(), Qt::KeepAspectRatio);
    return QRect(QPoint((width() - fitted.width()) / 2, (height() - fitted.height()) / 2), fitted);
}

void CoverArt::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), palette().brush(QPalette::Base));
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    if (!current.isNull()) {
        painter.drawImage(ImageRect(), current);
    } else {
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.drawText(rect().adjusted(12, 12, -12, -12), Qt::AlignCenter | Qt::TextWordWrap,
                         tr("No cover artwork"));
    }
}

} // namespace QtCommon
