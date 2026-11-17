// SPDX-License-Identifier: GPL-3.0-or-later
#include <utility>
#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>
#include "qt_common/couch/welcome.h"

namespace QtCommon::Couch {

WelcomeDialog::WelcomeDialog(std::function<void()> remember_dismissal, QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("CouchWelcome"));
    setWindowTitle(tr("Get comfortable with Couch Mode"));
    setWindowModality(Qt::WindowModal);
    setMinimumWidth(420);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(16);
    auto* heading = new QLabel(tr("Meet Couch Mode"), this);
    auto font = heading->font();
    font.setPointSize(16);
    font.setBold(true);
    heading->setFont(font);
    layout->addWidget(heading);
    auto* message = new QLabel(
        tr("A full-screen library and all your settings, with roomy controls for a controller "
           "or touchscreen. Would you like to switch now?\n\n"
           "You can open it any time from View → Couch Mode or Ctrl+Shift+C. "
           "Choose Start in Couch Mode in settings to use it on future launches."), this);
    message->setWordWrap(true);
    message->setMaximumWidth(520);
    layout->addWidget(message);
    never_ask = new QCheckBox(tr("Never ask again"), this);
    never_ask->setObjectName(QStringLiteral("couchWelcomeNeverAsk"));
    never_ask->setMinimumHeight(48);
    layout->addWidget(never_ask);
    auto* buttons = new QDialogButtonBox(this);
    auto* now = buttons->addButton(tr("Switch now"), QDialogButtonBox::AcceptRole);
    now->setObjectName(QStringLiteral("couchWelcomeNow"));
    auto* later = buttons->addButton(tr("Later"), QDialogButtonBox::RejectRole);
    later->setObjectName(QStringLiteral("couchWelcomeLater"));
    later->setDefault(true);
    for (auto* button : {now, later})
        button->setMinimumSize(132, 48);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    connect(this, &QDialog::finished, this,
            [this, remember = std::move(remember_dismissal)](int result) {
                if (result == QDialog::Accepted || never_ask->isChecked())
                    remember();
            });
    later->setFocus();
}

void WelcomeDialog::Navigate(Qt::Key key) {
    QPointer<QWidget> target = focusWidget();
    if (!target)
        target = this;
    if (key == Qt::Key_Return && qobject_cast<QCheckBox*>(target.data()))
        key = Qt::Key_Space;
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
    QApplication::sendEvent(target, &press);
    if (target) {
        QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
        QApplication::sendEvent(target, &release);
    }
}

} // namespace QtCommon::Couch
