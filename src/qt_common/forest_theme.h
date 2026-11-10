// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QFrame>
#include <QIcon>
#include <QString>

class QApplication;
class QDialog;
class QLabel;
class QPushButton;

namespace QtCommon {
class CoverArt;
}

namespace QtCommon::ForestTheme {

bool IsForestTheme();
bool IsForestThemeName(const QString& name);
void Apply(const QString& name);
void Clear();
QIcon Icon(const QString& name);
QWidget* CreateHeader(QWidget* parent, const QString& title, const QString& subtitle = {});
void PrepareDialog(QDialog* dialog);

class LibraryPanel final : public QFrame {
    Q_OBJECT
public:
    explicit LibraryPanel(QWidget* parent = nullptr);
    void SetGame(const QString& title, const QString& path, const QByteArray& artwork = {});

signals:
    void PlayRequested();
    void PropertiesRequested();
    void MoreRequested();

protected:
    void changeEvent(QEvent* event) override;

private:
    QString game_title;
    QString game_path;
    QLabel* title_label{};
    QLabel* path_label{};
    QtCommon::CoverArt* cover{};
    QPushButton* play_button{};
    QPushButton* properties_button{};
    QPushButton* more_button{};
};

} // namespace QtCommon::ForestTheme
