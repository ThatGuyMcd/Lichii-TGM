// SPDX-License-Identifier: GPL-3.0-or-later

#include "qt_common/forest_theme.h"

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QStyle>
#include <QStyleFactory>
#include <QVBoxLayout>

static void InitializeForestResources() {
    Q_INIT_RESOURCE(forest);
}

namespace QtCommon::ForestTheme {
namespace {

class Artwork final : public QFrame {
public:
    Artwork(QWidget* parent, const QString& resource, bool footer = false)
        : QFrame(parent), picture(resource), is_footer(footer) {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setFixedHeight(footer ? 80 : 88);
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        QFrame::paintEvent(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        if (is_footer) {
            painter.drawPixmap(QRect(0, -22, 315, 105), picture);
        } else {
            const int width = qMin(420, this->width());
            painter.drawPixmap(QRect(this->width() - width, -26, width, width / 3), picture);
        }
    }

private:
    QPixmap picture;
    bool is_footer;
};

class Landscape final : public QWidget {
public:
    explicit Landscape(QWidget* parent) : QWidget(parent), picture(QStringLiteral(":/forest/forest-glade.png")) {
        setFixedHeight(180);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        const QPixmap scaled = picture.scaled(size(), Qt::KeepAspectRatioByExpanding,
                                               Qt::SmoothTransformation);
        painter.drawPixmap((width() - scaled.width()) / 2, (height() - scaled.height()) / 2, scaled);
    }

private:
    QPixmap picture;
};

void RemoveFromLayout(QLayout* layout, QWidget* widget) {
    layout->removeWidget(widget);
    for (int i = 0; i < layout->count(); ++i) {
        if (auto* child = layout->itemAt(i)->layout()) {
            RemoveFromLayout(child, widget);
        }
    }
}

class DialogDecorator final : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* object, QEvent* event) override {
        if (IsForestTheme() && event->type() == QEvent::Show) {
            auto* dialog = qobject_cast<QDialog*>(object);
            if (dialog) {
                const QByteArray name = dialog->metaObject()->className();
                if (name.startsWith("Configure") || name == "GameProperties" ||
                    name == "HotkeyDialog" || name == "AboutDialog") {
                    PrepareDialog(dialog);
                }
            }
        }
        return QObject::eventFilter(object, event);
    }
};

} // namespace

bool IsForestTheme() {
    return qApp && qApp->property("forestTheme").toBool();
}

bool IsForestThemeName(const QString& name) {
    return name == QStringLiteral("forest_lantern_dark") ||
           name == QStringLiteral("forest_lantern_light");
}

void Apply(const QString& name) {
    InitializeForestResources();
    const bool dark = name == QStringLiteral("forest_lantern_dark");
    const QList<QPair<QString, QString>> colors{
        {QStringLiteral("TEXT"), dark ? QStringLiteral("#f4f2dc") : QStringLiteral("#203b2d")},
        {QStringLiteral("WINDOW"), dark ? QStringLiteral("#10251f") : QStringLiteral("#edf4e7")},
        {QStringLiteral("BASE"), dark ? QStringLiteral("#142e26") : QStringLiteral("#fcfdf7")},
        {QStringLiteral("PANEL"), dark ? QStringLiteral("#1a362d") : QStringLiteral("#e4eedc")},
        {QStringLiteral("ALTERNATE"), dark ? QStringLiteral("#1b3d31") : QStringLiteral("#f0f7e9")},
        {QStringLiteral("DISABLED"), dark ? QStringLiteral("#82998b") : QStringLiteral("#71836d")},
        {QStringLiteral("MUTED"), dark ? QStringLiteral("#b4c7b7") : QStringLiteral("#516955")},
        {QStringLiteral("BORDER"), dark ? QStringLiteral("#3d5c49") : QStringLiteral("#a8bd9e")},
        {QStringLiteral("ACCENT"), dark ? QStringLiteral("#9cddad") : QStringLiteral("#3f7750")},
        {QStringLiteral("ACCENTLABEL"), dark ? QStringLiteral("#c3eac9") : QStringLiteral("#326541")},
        {QStringLiteral("ACCENTTEXT"), dark ? QStringLiteral("#142f22") : QStringLiteral("#ffffff")},
        {QStringLiteral("ACCENTHOVER"), dark ? QStringLiteral("#b9ebc4") : QStringLiteral("#326541")},
        {QStringLiteral("HOVER"), dark ? QStringLiteral("#254738") : QStringLiteral("#d8e8cf")},
        {QStringLiteral("SELECTION"), dark ? QStringLiteral("#365c43") : QStringLiteral("#c9e3c3")},
        {QStringLiteral("SELECTEDTEXT"), dark ? QStringLiteral("#f4f2dc") : QStringLiteral("#203b2d")},
        {QStringLiteral("AMBER"), dark ? QStringLiteral("#e8bd6a") : QStringLiteral("#9b6a22")},
    };
    auto color = [&colors](const QString& key) {
        for (const auto& entry : colors) {
            if (entry.first == key) {
                return QColor(entry.second);
            }
        }
        return QColor();
    };
    if (!qApp->property("forestStyleInitialized").toBool()) {
        qApp->setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        auto* decorator = new DialogDecorator(qApp);
        qApp->installEventFilter(decorator);
        qApp->setProperty("forestStyleInitialized", true);
    }
    QPalette palette;
    for (auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        palette.setColor(group, QPalette::Window, color(QStringLiteral("WINDOW")));
        palette.setColor(group, QPalette::Base, color(QStringLiteral("BASE")));
        palette.setColor(group, QPalette::AlternateBase, color(QStringLiteral("ALTERNATE")));
        palette.setColor(group, QPalette::Button, color(QStringLiteral("PANEL")));
        const QColor text = color(group == QPalette::Disabled ? QStringLiteral("DISABLED")
                                                              : QStringLiteral("TEXT"));
        for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText,
                          QPalette::ToolTipText}) {
            palette.setColor(group, role, text);
        }
        palette.setColor(group, QPalette::ToolTipBase, color(QStringLiteral("PANEL")));
        palette.setColor(group, QPalette::Highlight, color(QStringLiteral("SELECTION")));
        palette.setColor(group, QPalette::HighlightedText, color(QStringLiteral("SELECTEDTEXT")));
        palette.setColor(group, QPalette::Link, color(QStringLiteral("ACCENTLABEL")));
        palette.setColor(group, QPalette::PlaceholderText, color(QStringLiteral("MUTED")));
        palette.setColor(group, QPalette::Light, color(QStringLiteral("BORDER")));
        palette.setColor(group, QPalette::Mid, color(QStringLiteral("MUTED")));
        palette.setColor(group, QPalette::Dark, color(QStringLiteral("BORDER")));
    }
    qApp->setPalette(palette);
    QFont font = qApp->font();
    font.setFamilies({QStringLiteral("Segoe UI"), QStringLiteral("Noto Sans"),
                      QStringLiteral("DejaVu Sans")});
    font.setPointSize(10);
    qApp->setFont(font);
    QFile stylesheet(QStringLiteral(":/forest/style.qss"));
    stylesheet.open(QFile::ReadOnly);
    QString css = QString::fromUtf8(stylesheet.readAll());
    for (const auto& entry : colors) {
        css.replace(QStringLiteral("@%1@").arg(entry.first), entry.second);
    }
    qApp->setProperty("forestTheme", true);
    qApp->setStyleSheet(css);
}

void Clear() {
    const bool active = IsForestTheme();
    qApp->setProperty("forestTheme", false);
    if (active) {
        qApp->setPalette(qApp->style()->standardPalette());
    }
}

QIcon Icon(const QString& name) {
    QIcon icon;
    for (auto mode : {QIcon::Normal, QIcon::Disabled}) {
        QPixmap pixmap(48, 48);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(2, 2);
        const auto color = qApp->palette().color(mode == QIcon::Disabled ? QPalette::Disabled
                                                                        : QPalette::Active,
                                               QPalette::WindowText);
        painter.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (name == QStringLiteral("play")) {
            painter.setBrush(color);
            painter.drawPolygon(QPolygonF{QPointF(8, 5), QPointF(19, 12), QPointF(8, 19)});
        } else if (name == QStringLiteral("pause")) {
            painter.drawLine(8, 5, 8, 19);
            painter.drawLine(16, 5, 16, 19);
        } else if (name == QStringLiteral("stop")) {
            painter.drawRoundedRect(QRectF(5, 5, 14, 14), 2, 2);
        } else if (name == QStringLiteral("restart")) {
            painter.drawArc(QRectF(5, 5, 14, 14), 30 * 16, 295 * 16);
            painter.drawPolyline(QPolygonF{QPointF(15, 3), QPointF(20, 5), QPointF(17, 10)});
        } else if (name == QStringLiteral("fullscreen")) {
            for (const auto& point : {QPointF(4, 4), QPointF(20, 4), QPointF(20, 20), QPointF(4, 20)}) {
                const qreal dx = point.x() < 12 ? 5 : -5;
                const qreal dy = point.y() < 12 ? 5 : -5;
                painter.drawPolyline(QPolygonF{point + QPointF(0, dy), point,
                                               point + QPointF(dx, 0)});
            }
        } else if (name == QStringLiteral("open")) {
            painter.drawPolyline(QPolygonF{QPointF(3, 19), QPointF(3, 5), QPointF(10, 5),
                                           QPointF(12, 8), QPointF(21, 8), QPointF(19, 19),
                                           QPointF(3, 19), QPointF(6, 10), QPointF(20, 10)});
        } else {
            for (int x : {6, 12, 18}) {
                painter.drawLine(x, 4, x, 20);
                painter.setBrush(qApp->palette().color(QPalette::Button));
                painter.drawEllipse(QPointF(x, x == 12 ? 15 : 9), 2.5, 2.5);
            }
        }
        painter.end();
        icon.addPixmap(pixmap, mode);
    }
    return icon;
}

QWidget* CreateHeader(QWidget* parent, const QString& title, const QString& subtitle) {
    InitializeForestResources();
    auto* header = new Artwork(parent, QStringLiteral(":/forest/header-bough.png"));
    header->setObjectName(QStringLiteral("ForestHeader"));
    auto* layout = new QVBoxLayout(header);
    layout->setContentsMargins(18, 10, 125, 10);
    auto* heading = new QLabel(title, header);
    heading->setObjectName(QStringLiteral("ForestHeading"));
    heading->setWordWrap(true);
    layout->addWidget(heading);
    if (!subtitle.isEmpty()) {
        auto* label = new QLabel(subtitle, header);
        label->setObjectName(QStringLiteral("ForestSubtitle"));
        label->setWordWrap(true);
        layout->addWidget(label);
    }
    return header;
}

QWidget* CreateFooter(QWidget* parent) {
    InitializeForestResources();
    return new Artwork(parent, QStringLiteral(":/forest/footer-fox.png"), true);
}

void PrepareDialog(QDialog* dialog) {
    if (!IsForestTheme() || dialog->property("forestDialogPrepared").toBool() || !dialog->layout()) {
        return;
    }
    dialog->setProperty("forestDialogPrepared", true);
    auto* old_layout = dialog->layout();
    const auto boxes = dialog->findChildren<QDialogButtonBox*>(QString(), Qt::FindDirectChildrenOnly);
    for (auto* box : boxes) {
        RemoveFromLayout(old_layout, box);
    }
    auto* content = new QWidget;
    content->setLayout(old_layout);
    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(12, 8, 12, 12);
    layout->addWidget(CreateHeader(dialog, dialog->windowTitle()));
    auto* scroll = new QScrollArea(dialog);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    layout->addWidget(scroll, 1);
    for (auto* box : boxes) {
        layout->addWidget(box);
    }
    dialog->setSizeGripEnabled(true);
    const QSize available = dialog->screen()->availableGeometry().size() - QSize(48, 64);
    dialog->setMinimumSize(QSize(460, 340).boundedTo(available));
    dialog->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    dialog->resize((dialog->sizeHint() + QSize(40, 40)).boundedTo(available));
}

LibraryPanel::LibraryPanel(QWidget* parent) : QFrame(parent) {
    InitializeForestResources();
    setObjectName(QStringLiteral("ForestLibraryPanel"));
    setMinimumWidth(230);
    setMaximumWidth(380);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);
    layout->addWidget(new Landscape(this));
    icon_label = new QLabel(this);
    layout->addWidget(icon_label);
    title_label = new QLabel(this);
    title_label->setObjectName(QStringLiteral("ForestGameTitle"));
    title_label->setWordWrap(true);
    layout->addWidget(title_label);
    path_label = new QLabel(this);
    path_label->setObjectName(QStringLiteral("ForestHint"));
    path_label->setWordWrap(true);
    path_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    path_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(path_label);
    layout->addStretch();
    play_button = new QPushButton(tr("Play"), this);
    play_button->setProperty("forestPrimary", true);
    layout->addWidget(play_button);
    properties_button = new QPushButton(tr("Game settings"), this);
    layout->addWidget(properties_button);
    connect(play_button, &QPushButton::clicked, this, &LibraryPanel::PlayRequested);
    connect(properties_button, &QPushButton::clicked, this, &LibraryPanel::PropertiesRequested);
    SetGame({}, {});
}

void LibraryPanel::SetGame(const QString& title, const QString& path, const QPixmap& icon) {
    game_title = title;
    game_path = path;
    const bool selected = !path.isEmpty();
    title_label->setText(selected ? title : tr("Your next adventure"));
    path_label->setText(selected ? path : tr("Select a game in your library to get started."));
    icon_label->setPixmap(icon.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    icon_label->setVisible(!icon.isNull());
    play_button->setEnabled(selected);
    properties_button->setEnabled(selected);
}

void LibraryPanel::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        play_button->setText(tr("Play"));
        properties_button->setText(tr("Game settings"));
        title_label->setText(game_path.isEmpty() ? tr("Your next adventure") : game_title);
        path_label->setText(game_path.isEmpty() ? tr("Select a game in your library to get started.")
                                              : game_path);
    }
    QFrame::changeEvent(event);
}

} // namespace QtCommon::ForestTheme
