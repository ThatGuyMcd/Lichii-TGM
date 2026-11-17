// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <QDialog>

class QCheckBox;

namespace QtCommon::Couch {

class WelcomeDialog final : public QDialog {
    Q_OBJECT
public:
    explicit WelcomeDialog(std::function<void()> remember_dismissal, QWidget* parent = nullptr);
    void Navigate(Qt::Key key);

private:
    QCheckBox* never_ask;
};

} // namespace QtCommon::Couch
