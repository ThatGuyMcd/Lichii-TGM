// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QWidget>
class QCheckBox;
class QComboBox;
class QSpinBox;

class ConfigureCouch final : public QWidget {
    Q_OBJECT
public:
    explicit ConfigureCouch(QWidget* parent = nullptr);
    void ApplyConfiguration();

private:
    void ImportTheme();
    void RefreshThemes(const QString& selected);
    QCheckBox* startup;
    QCheckBox* invitation;
    QCheckBox* reduced;
    QComboBox* theme;
    QComboBox* prompts;
    QSpinBox* scale;
};
