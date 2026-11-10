// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QFrame>
#include <QIcon>
#include <QString>

class QApplication;
class QDialog;
class QLabel;
class QPushButton;

namespace QtCommon::ForestTheme {

bool IsForestTheme();
bool IsForestThemeName(const QString& name);
void Apply(const QString& name);
void Clear();
QIcon Icon(const QString& name);
QWidget* CreateHeader(QWidget* parent, const QString& title, const QString& subtitle = {});
QWidget* CreateFooter(QWidget* parent);
void PrepareDialog(QDialog* dialog);

class LibraryPanel final : public QFrame {
    Q_OBJECT
public:
    explicit LibraryPanel(QWidget* parent = nullptr);
    void SetGame(const QString& title, const QString& path, const QPixmap& icon = {});

signals:
    void PlayRequested();
    void PropertiesRequested();

protected:
    void changeEvent(QEvent* event) override;

private:
    QString game_title;
    QString game_path;
    QLabel* title_label{};
    QLabel* path_label{};
    QLabel* icon_label{};
    QPushButton* play_button{};
    QPushButton* properties_button{};
};

} // namespace QtCommon::ForestTheme
