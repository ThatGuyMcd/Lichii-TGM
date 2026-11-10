// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <memory>
#include <system_error>
#include <utility>

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QSaveFile>
#include <QScrollArea>
#include <QUuid>

#include "common/fs/path_util.h"
#include "common/settings.h"
#include "qt_common/config/uisettings.h"
#include "video_core/shader_warm/shader_warm_builder.h"
#include "yuzu/shader_preparation_job.h"
#include "yuzu/shader_warm_dialog.h"

namespace ShaderWarm = VideoCommon::ShaderWarm;

bool ShaderWarmFilesExist(u64 title_id) {
    if (title_id == 0) {
        return false;
    }
    std::error_code ec;
    return std::filesystem::exists(ShaderWarm::ComputeWarmPath(title_id), ec) ||
           std::filesystem::exists(ShaderWarm::GraphicsWarmPath(title_id), ec) ||
           std::filesystem::exists(ShaderWarm::MeshWarmPath(title_id), ec) ||
           std::filesystem::exists(ShaderWarm::NativeRecipePath(title_id), ec);

}

bool DeleteShaderWarmFiles(u64 title_id, std::string& error) {
    return ShaderWarm::DeleteWarmFiles(title_id, &error);
}

namespace {

// The builder reports its stage in English; show the known ones translated.
QString StageText(const std::string& stage) {
    if (stage == "Reading game files") {
        return ShaderWarmDialog::tr("Reading game files");
    }
    if (stage == "Extracting shaders") {
        return ShaderWarmDialog::tr("Extracting shaders");
    }
    if (stage == "Converting") {
        return ShaderWarmDialog::tr("Converting shaders");
    }
    if (stage == "Writing") {
        return ShaderWarmDialog::tr("Writing the precache files");
    }
    if (stage == "Checking game contents") {
        return ShaderWarmDialog::tr("Checking game contents and updates");
    }
    if (stage == "Extracting native shaders") {
        return ShaderWarmDialog::tr("Extracting native shaders");
    }
    if (stage == "Assembling pipelines from recipe") {
        return ShaderWarmDialog::tr("Assembling pipelines from the recipe");
    }
    if (stage == "Writing native preparation") {
        return ShaderWarmDialog::tr("Writing the native pipeline file");
    }
    return QString::fromStdString(stage);
}

/// True when shader libraries are on (the setting or EDEN_NX2_GPL).
bool ShaderLibrariesOn() {
    if (Settings::values.use_graphics_pipeline_library.GetValue()) {
        return true;
    }
    const char* value = std::getenv("EDEN_NX2_GPL");
    return value && *value && *value != '0';
}

} // namespace

/// Builds the stage label, progress bar, result text and the Cancel/Close button.
ShaderWarmDialog::ShaderWarmDialog(Core::System& system_, std::filesystem::path game_path_,
                                   u64 title_id_, const QString& game_name, QWidget* parent,
                                   std::filesystem::path recipe_, bool full_preparation_)
    : QDialog(parent), system{system_}, game_path{std::move(game_path_)},
      recipe{std::move(recipe_)}, title_id{title_id_}, full_preparation{full_preparation_} {
    setWindowTitle((full_preparation ? tr("Prepare game shaders: %1") : tr("Precache shaders: %1")).arg(game_name));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setModal(false);
    setMinimumWidth(420);

    stage_label = new QLabel(tr("Starting"), this);
    progress_bar = new QProgressBar(this);
    progress_bar->setRange(0, 0);
    result_label = new QLabel(this);
    result_label->setWordWrap(true);
    result_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    result_label->setVisible(false);
    button = new QPushButton(tr("Cancel"), this);
    connect(button, &QPushButton::clicked, this, [this] {
        if (running) {
            RequestStop();
        } else {
            reject();
        }
    });

    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(button);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(stage_label);
    layout->addWidget(progress_bar);
    if (full_preparation) {
        auto* scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(result_label);
        layout->addWidget(scroll, 1);
        button->setMinimumHeight(48);
        resize(720, 500);
    } else {
        layout->addWidget(result_label);
    }
    layout->addLayout(buttons);
}

/// Stops and joins a running build.
ShaderWarmDialog::~ShaderWarmDialog() {
    if (process && process->state() != QProcess::NotRunning) {
        QFile cancel(job_directory + QStringLiteral("/cancel"));
        cancel.open(QIODevice::WriteOnly);
        cancel.close();
        if (!process->waitForFinished(5000)) {
            process->kill();
            process->waitForFinished(3000);
        }
    }
    if (worker.joinable()) {
        worker.request_stop();
        worker.join();
    }
}

/// Starts the build on a worker thread. Progress (at most every 100 ms unless the stage changes)
/// and the result, failures included, come back on the UI thread.
void ShaderWarmDialog::Start() {
    if (running) {
        return;
    }
    running = true;
    stopping = false;
    if (full_preparation) {
        StartFullPreparation();
        return;
    }
    worker = std::jthread([this](std::stop_token stop) {
        auto last_update = std::chrono::steady_clock::time_point{};
        std::string last_stage;
        const auto on_progress = [this, &last_update, &last_stage](const ShaderWarm::Progress& p) {
            const auto now = std::chrono::steady_clock::now();
            const bool stage_changed = p.stage != last_stage;
            const bool stage_done = p.total != 0 && p.done >= p.total;
            if (!stage_changed && !stage_done &&
                now - last_update < std::chrono::milliseconds(100)) {
                return;
            }
            last_update = now;
            last_stage = p.stage;
            QMetaObject::invokeMethod(
                this,
                [this, stage = StageText(p.stage), done = static_cast<qulonglong>(p.done),
                 total = static_cast<qulonglong>(p.total)] { OnProgress(stage, done, total); },
                Qt::QueuedConnection);
        };
        // An exception must not leave the thread (std::terminate would close the emulator):
        // report it as a failed build instead.
        auto result = std::make_shared<ShaderWarm::Result>();
        try {
            ShaderWarm::BuildOptions options;
            options.recipe_path = recipe;
            *result = ShaderWarm::BuildWarmFilesWith(system, game_path, title_id, options,
                                                     on_progress, stop);
        } catch (const std::exception& e) {
            result->ok = false;
            result->error = e.what();
        } catch (...) {
            result->ok = false;
            result->error = "unknown error";
        }
        const bool stopped = stop.stop_requested();
        QMetaObject::invokeMethod(
            this, [this, result, stopped] { OnFinished(*result, stopped); },
            Qt::QueuedConnection);
    });
}

/// Asks a running build to stop and disables the button until it has.
void ShaderWarmDialog::RequestStop() {
    if (!running || stopping) {
        return;
    }
    stopping = true;
    if (full_preparation) {
        QFile cancel(job_directory + QStringLiteral("/cancel"));
        cancel.open(QIODevice::WriteOnly);
        stage_label->setText(tr("Stopping after the current pipeline finishes…"));
        button->setEnabled(false);
        return;
    }
    worker.request_stop();
    stage_label->setText(tr("Stopping"));
    button->setEnabled(false);
}

void ShaderWarmDialog::StartFullPreparation() {
    const auto root = Common::FS::GetEdenPath(Common::FS::EdenPath::ShaderDir);
    job_directory = QString::fromStdString(Common::FS::PathToUTF8String(root)) +
        QStringLiteral("/%1/preparation-jobs/%2").arg(title_id, 16, 16, QLatin1Char('0'))
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    const auto fail = [this](const QString& error) {
        worker_result = {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
        FinishWorker(true);
    };
    if (!QFileInfo::exists(ShaderPreparationUi::WorkerPath())) {
        fail(tr("The shader preparation worker is missing. Extract the complete emulator package."));
        return;
    }
    if (!QDir().mkpath(job_directory)) {
        fail(tr("The shader cache folder could not be created."));
        return;
    }
    QSaveFile file(job_directory + QStringLiteral("/job.json"));
    const auto job = ShaderPreparationUi::MakeJob(title_id,
        QString::fromStdString(Common::FS::PathToUTF8String(game_path)), job_directory);
    const auto data = QJsonDocument(job).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        fail(tr("The preparation job could not be saved."));
        return;
    }
    process = new QProcess(this);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(process, &QProcess::readyReadStandardOutput, this, &ShaderWarmDialog::ReadWorkerOutput);
    connect(process, &QProcess::readyReadStandardError, this, [this] {
        process_errors += process->readAllStandardError();
        process_errors = process_errors.right(16 * 1024);
    });
    connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) FinishWorker(false);
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int, QProcess::ExitStatus status) {
            ReadWorkerOutput();
            FinishWorker(status == QProcess::NormalExit);
        });
    process->start(ShaderPreparationUi::WorkerPath(), {QStringLiteral("--job"), file.fileName()});
    if (UISettings::values.settings_fullscreen.GetValue()) showFullScreen();
}

void ShaderWarmDialog::ReadWorkerOutput() {
    process_output += process->readAllStandardOutput();
    if (process_output.size() > 2 * 1024 * 1024) process_output.clear();
    qsizetype newline;
    while ((newline = process_output.indexOf('\n')) >= 0) {
        const auto line = process_output.left(newline);
        process_output.remove(0, newline + 1);
        const auto value = QJsonDocument::fromJson(line).object();
        if (value.value(QStringLiteral("event")) == QStringLiteral("progress")) {
            OnProgress(value.value(QStringLiteral("stage")).toString(), value.value(QStringLiteral("done")).toVariant().toULongLong(),
                       value.value(QStringLiteral("total")).toVariant().toULongLong());
        } else if (value.value(QStringLiteral("event")) == QStringLiteral("result")) {
            worker_result = value;
        }
    }
}

void ShaderWarmDialog::FinishWorker(bool normal_exit) {
    if (!running) return;
    running = false;
    const bool cancelled = stopping || worker_result.value(QStringLiteral("cancelled")).toBool();
    const bool ok = normal_exit && worker_result.value(QStringLiteral("ok")).toBool() && !cancelled;
    const bool partial = worker_result.value(QStringLiteral("partial")).toBool() ||
                         !worker_result.value(QStringLiteral("native_ready")).toBool();
    stage_label->setText(cancelled ? tr("Preparation stopped. Completed pipelines can be resumed.") :
        !ok ? tr("Shader preparation could not finish.") :
        partial ? tr("Partially prepared — see the remaining work below.") :
        tr("Available shader pipelines prepared."));
    if (worker_result.isEmpty()) {
        worker_result = {{QStringLiteral("error"), tr("The preparation worker exited unexpectedly. Its last "
            "completed checkpoints and your existing caches are retained.\n%1")
            .arg(QString::fromUtf8(process_errors))}};
    }
    result_label->setText(ShaderPreparationUi::ResultText(worker_result) +
        tr("\n\nNew runtime configurations may still require compilation.\nReport: %1/report.json")
            .arg(job_directory));
    result_label->show();
    progress_bar->setRange(0, 1);
    progress_bar->setValue(ok ? 1 : 0);
    button->setText(tr("Close"));
    button->setEnabled(true);
    emit BuildFinished(ok);
}

/// Shows the stage and its progress; a busy bar when the total is unknown.
void ShaderWarmDialog::OnProgress(const QString& stage, qulonglong done, qulonglong total) {
    if (stopping) {
        return;
    }
    if (total > 0) {
        stage_label->setText(tr("%1: %L2 / %L3").arg(stage).arg(done).arg(total));
        progress_bar->setRange(0, 1000);
        progress_bar->setValue(static_cast<int>((std::min<qulonglong>)(done * 1000 / total, 1000)));
    } else {
        stage_label->setText(done > 0 ? tr("%1: %L2").arg(stage).arg(done) : stage);
        progress_bar->setRange(0, 0);
    }
}

/// Joins the worker and shows the result: program counts, stopped, or the error text.
void ShaderWarmDialog::OnFinished(const ShaderWarm::Result& result, bool stopped) {
    running = false;
    if (worker.joinable()) {
        worker.join();
    }
    progress_bar->setRange(0, 1);
    progress_bar->setValue(result.ok ? 1 : 0);

    QString text;
    if (result.ok) {
        stage_label->setText(tr("Shader precache is ready."));
        const size_t graphics_programs = result.vertex_programs + result.fragment_programs;
        if (result.compute_programs == 0 && graphics_programs == 0 && result.mesh_pipelines == 0) {
            text = result.archives == 0
                       ? tr("This game keeps its shaders in a format Lichii-tgm can't read, so none "
                            "are prepared from the game files.")
                       : tr("The game files have no shaders to prepare.");
            text += QLatin1Char(' ') +
                    tr("The shaders your earlier play sessions saved are still precached.");
        } else {
            text = tr("Compute programs: %L1\nVertex programs: %L2\nFragment programs: %L3\nMesh candidates: %L4")
                       .arg(static_cast<qulonglong>(result.compute_programs))
                       .arg(static_cast<qulonglong>(result.vertex_programs))
                       .arg(static_cast<qulonglong>(result.fragment_programs))
                       .arg(static_cast<qulonglong>(result.mesh_pipelines));
        }
        if (result.native_pipelines > 0) {
            text += QLatin1Char('\n') +
                    tr("Native pipelines from the recipe: %L1 (%L2 programs and %L3 combinations "
                       "found in the game files)")
                        .arg(static_cast<qulonglong>(result.native_pipelines))
                        .arg(static_cast<qulonglong>(result.native_programs))
                        .arg(static_cast<qulonglong>(result.native_combinations));
            if (result.native_dropped > 0) {
                text +=
                    QLatin1Char('\n') + tr("Recipe pipelines this copy of the game lacks: %L1")
                                            .arg(static_cast<qulonglong>(result.native_dropped));
            }
        }
        if (result.skipped > 0) {
            text += QLatin1Char('\n') +
                    tr("Skipped (could not be read or converted): %L1")
                        .arg(static_cast<qulonglong>(result.skipped));
        }
        text += QStringLiteral("\n\n") +
                tr("It is built on the loading screen when Precache shaders before game start is "
                   "on. An interrupted build continues where it stopped.");
        if (graphics_programs != 0 && !ShaderLibrariesOn()) {
            text += QLatin1Char(' ') +
                    tr("The graphics part also needs Shader libraries, in Configure, Graphics, "
                       "Advanced.");
        }
    } else if (stopped) {
        stage_label->setText(tr("Shader precache stopped."));
    } else {
        stage_label->setText(tr("Shader precache could not be built."));
        text = result.error.empty() ? tr("No details were given.")
                                    : QString::fromStdString(result.error);
    }
    result_label->setText(text);
    result_label->setVisible(!text.isEmpty());
    button->setText(tr("Close"));
    button->setEnabled(true);
    adjustSize();
    emit BuildFinished(result.ok);
}

/// Closing while a build runs only asks it to stop.
void ShaderWarmDialog::closeEvent(QCloseEvent* event) {
    if (running) {
        RequestStop();
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

/// Escape or Cancel while a build runs only asks it to stop.
void ShaderWarmDialog::reject() {
    if (running) {
        RequestStop();
        return;
    }
    QDialog::reject();
}
