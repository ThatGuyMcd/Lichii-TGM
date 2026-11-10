// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

class QDialog;
class QWidget;

namespace QtCommon::TouchUi {

void Initialize(bool comfortable = true, bool settings_fullscreen = true);
bool IsComfortable();
int TargetSize();
void PreparePage(QWidget* page);
void InstallSettingsShell(QDialog* dialog, const QString& title, const QString& subtitle = {});
void MarkApplied(QDialog* dialog);

} // namespace QtCommon::TouchUi
