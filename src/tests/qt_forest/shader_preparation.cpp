// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>
#include "common/fs/path_util.h"
#include "common/fs/file_lock.h"
#include "common/settings.h"
#include "yuzu/shader_preparation_job.h"

class ShaderPreparationTests : public QObject {
    Q_OBJECT
private slots:
    void selectedGameOverridesAreReadWithoutWritingConfiguration() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto previous = Common::FS::GetEdenPath(Common::FS::EdenPath::ConfigDir);
        const auto directory = std::filesystem::path{temporary.path().toStdU16String()};
        Common::FS::SetEdenPath(Common::FS::EdenPath::ConfigDir, directory);
        struct Restore {
            std::filesystem::path path;
            ~Restore() { Common::FS::SetEdenPath(Common::FS::EdenPath::ConfigDir, path); }
        } restore{previous};
        QVERIFY(QDir().mkpath(temporary.path() + QStringLiteral("/custom")));
        const QString filename = temporary.path() + QStringLiteral("/custom/0400486024DEC000.ini");
        QFile file(filename);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray original = "[Renderer]\naspect_ratio\\use_global=false\n"
            "aspect_ratio\\default=false\naspect_ratio=7\n";
        QCOMPARE(file.write(original), original.size());
        file.close();
        const auto job = ShaderPreparationUi::MakeJob(0x0400486024DEC000ULL,
            QStringLiteral("F:/Games/日本語 Game.nup"), temporary.path());
        QCOMPARE(job.value(QStringLiteral("settings")).toObject().value(QStringLiteral("aspect_ratio")).toString(),
                 QStringLiteral("7"));
        QCOMPARE(job.value(QStringLiteral("game_path")).toString(), QStringLiteral("F:/Games/日本語 Game.nup"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), original);
    }

    void reportSeparatesPreparedAndUnresolvedWork() {
        const QJsonObject result{{QStringLiteral("files_scanned"), 100},
            {QStringLiteral("programs"), 42}, {QStringLiteral("total"), 120},
            {QStringLiteral("built"), 70}, {QStringLiteral("already_prepared"), 30},
            {QStringLiteral("failed"), 3}, {QStringLiteral("incompatible"), 9},
            {QStringLiteral("quarantined"), 2}, {QStringLiteral("unreached"), 6}};
        const auto report = ShaderPreparationUi::ResultText(result);
        QVERIFY(report.contains(QStringLiteral("Prepared now: 70")));
        QVERIFY(report.contains(QStringLiteral("Previously prepared: 30")));
        QVERIFY(report.contains(QStringLiteral("Failed: 3")));
        QVERIFY(report.contains(QStringLiteral("Unsupported on this GPU: 9")));
        QVERIFY(report.contains(QStringLiteral("Quarantined: 2")));
        QVERIFY(report.contains(QStringLiteral("Not reached: 6")));
    }

    void packagedWorkerReportsInvalidJobsWithoutStartingGraphics() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString directory = temporary.path() + QStringLiteral("/Shader preparation 日本語");
        QVERIFY(QDir().mkpath(directory));
        const QString filename = directory + QStringLiteral("/job.json");
        QFile file(filename);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"format\":0}");
        file.close();
        QProcess worker;
        worker.start(ShaderPreparationUi::WorkerPath(), {QStringLiteral("--job"), filename});
        QVERIFY(worker.waitForStarted(5000));
        QVERIFY(worker.waitForFinished(15000));
        QCOMPARE(worker.exitStatus(), QProcess::NormalExit);
        QCOMPARE(worker.exitCode(), 1);
        const auto output = worker.readAllStandardOutput();
        const auto result = QJsonDocument::fromJson(output).object();
        QCOMPARE(result.value(QStringLiteral("event")).toString(), QStringLiteral("result"));
        QVERIFY(!result.value(QStringLiteral("ok")).toBool());
        QVERIFY(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("format")));
        QVERIFY(!output.contains("Preparing pipelines"));
    }

    void workerRejectsMixedBuildsBeforeReadingGameContent() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString filename = temporary.path() + QStringLiteral("/job.json");
        QFile file(filename);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"format\":1,\"expected_source\":\"different-build\"}");
        file.close();
        QProcess worker;
        worker.start(ShaderPreparationUi::WorkerPath(), {QStringLiteral("--job"), filename});
        QVERIFY(worker.waitForFinished(15000));
        QCOMPARE(worker.exitStatus(), QProcess::NormalExit);
        const auto result = QJsonDocument::fromJson(worker.readAllStandardOutput()).object();
        QVERIFY(!result.value(QStringLiteral("ok")).toBool());
        QVERIFY(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("different build")));
    }

    void workerRespectsAnotherInstancesTitleLock() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto scratch = std::filesystem::path{temporary.path().toStdU16String()};
        const auto title_dir = scratch / "0000000000001234";
        std::filesystem::create_directory(title_dir);
        Common::FS::FileLock lock(title_dir / "native-preparation.lock");
        QVERIFY(lock.Held());
        auto job = ShaderPreparationUi::MakeJob(0x1234, QStringLiteral("missing-game.nup"), temporary.path());
        auto paths = job.value(QStringLiteral("paths")).toObject();
        paths[QString::number(static_cast<int>(Common::FS::EdenPath::ShaderDir))] = temporary.path();
        job[QStringLiteral("paths")] = paths;
        const QString filename = temporary.path() + QStringLiteral("/job.json");
        QFile file(filename);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(job).toJson());
        file.close();
        QProcess worker;
        worker.start(ShaderPreparationUi::WorkerPath(), {QStringLiteral("--job"), filename});
        QVERIFY(worker.waitForFinished(15000));
        QCOMPARE(worker.exitStatus(), QProcess::NormalExit);
        const auto result = QJsonDocument::fromJson(worker.readAllStandardOutput()).object();
        QVERIFY(!result.value(QStringLiteral("ok")).toBool());
        QVERIFY(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("Another instance")));
        QVERIFY(!std::filesystem::exists(title_dir / "native-preparation.bin"));
    }
};

QTEST_GUILESS_MAIN(ShaderPreparationTests)
#include "shader_preparation.moc"
