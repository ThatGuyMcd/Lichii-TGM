// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QString>
#include "common/common_types.h"

namespace ShaderPreparationUi {
QJsonObject MakeJob(u64 title_id, const QString& game_path, const QString& job_directory);
QString ResultText(const QJsonObject& result);
QString WorkerPath();
} // namespace ShaderPreparationUi
