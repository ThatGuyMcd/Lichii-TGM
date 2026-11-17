// SPDX-License-Identifier: GPL-3.0-or-later
#include <QAbstractButton>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include "qt_common/couch/native_dialog.h"

namespace QtCommon::Couch {
NativeDialog::NativeDialog(QDialog* dialog_, QtCommon::Couch::Theme* theme_, QObject* parent)
    : QObject(parent), dialog(dialog_), theme(theme_), settings(this) {
    settings.SetPages(
        {{dialog_, QStringLiteral("dialog"), dialog_->windowTitle(), QStringLiteral("dialog")}},
        true);
    settings.Select(QStringLiteral("dialog"), 0);
    connect(&settings, &WidgetModel::changed, this, &NativeDialog::changed);
    refresh();
}
QVariantMap NativeDialog::State() const {
    if (!dialog)
        return {};
    QVariantMap state{{QStringLiteral("title"), dialog->windowTitle()},
                      {QStringLiteral("kind"), QStringLiteral("settings")}};
    QVariantList buttons;
    int index = 0;
    if (auto* message = qobject_cast<QMessageBox*>(dialog.data())) {
        state[QStringLiteral("kind")] = QStringLiteral("message");
        state[QStringLiteral("message")] =
            QString(message->text() + QLatin1Char('\n') + message->informativeText());
        state[QStringLiteral("detail")] = message->detailedText();
        for (auto* button : message->buttons()) {
            buttons.push_back(QVariantMap{
                {QStringLiteral("id"), index++},
                {QStringLiteral("label"), button->text().remove(QLatin1Char('&'))},
                {QStringLiteral("enabled"), button->isEnabled()},
                {QStringLiteral("default"), qobject_cast<QPushButton*>(button) &&
                                                qobject_cast<QPushButton*>(button)->isDefault()}});
        }
    } else if (auto* box = dialog->findChild<QDialogButtonBox*>()) {
        for (auto* button : box->buttons()) {
            buttons.push_back(QVariantMap{
                {QStringLiteral("id"), index++},
                {QStringLiteral("label"), button->text().remove(QLatin1Char('&'))},
                {QStringLiteral("enabled"), button->isEnabled()},
                {QStringLiteral("default"), qobject_cast<QPushButton*>(button) &&
                                                qobject_cast<QPushButton*>(button)->isDefault()}});
        }
    }
    if (buttons.isEmpty())
        buttons.push_back(QVariantMap{{QStringLiteral("id"), -1},
                                      {QStringLiteral("label"), tr("Done")},
                                      {QStringLiteral("enabled"), true}});
    state[QStringLiteral("buttons")] = buttons;
    if (auto* file = qobject_cast<QFileDialog*>(dialog.data())) {
        state[QStringLiteral("kind")] = QStringLiteral("files");
        state[QStringLiteral("path")] = file->directory().absolutePath();
        state[QStringLiteral("save")] = file->acceptMode() == QFileDialog::AcceptSave;
        state[QStringLiteral("directory")] = file->fileMode() == QFileDialog::Directory;
        state[QStringLiteral("filters")] = file->nameFilters();
        state[QStringLiteral("selectedFilter")] = file->selectedNameFilter();
        state[QStringLiteral("selectedFile")] = file->selectedFiles().value(0);
        state[QStringLiteral("multiple")] = file->fileMode() == QFileDialog::ExistingFiles;
        state[QStringLiteral("selected")] = selected;
        QStringList drives;
        for (const auto& drive : QDir::drives())
            drives.push_back(drive.absoluteFilePath());
        state[QStringLiteral("drives")] = drives;
        QVariantList entries;
        for (int row = 0; row < files.size(); ++row) {
            const QFileInfo info(files[row]);
            entries.push_back(
                QVariantMap{{QStringLiteral("id"), row},
                            {QStringLiteral("label"),
                             info.fileName().isEmpty() ? info.absoluteFilePath() : info.fileName()},
                            {QStringLiteral("directory"), info.isDir()},
                            {QStringLiteral("selected"), selected.contains(files[row])}});
        }
        state[QStringLiteral("files")] = entries;
    }
    return state;
}
void NativeDialog::refresh() {
    files.clear();
    if (auto* file = qobject_cast<QFileDialog*>(dialog.data())) {
        const auto filter = file->selectedNameFilter();
        const auto match = QRegularExpression(QStringLiteral("\\(([^)]+)\\)")).match(filter);
        QStringList patterns = match.hasMatch()
                                   ? match.captured(1).split(QLatin1Char(' '), Qt::SkipEmptyParts)
                                   : QStringList{};
        QDir directory = file->directory();
        const auto flags = file->fileMode() == QFileDialog::Directory
                               ? QDir::Dirs | QDir::NoDotAndDotDot
                               : QDir::AllEntries | QDir::NoDotAndDotDot;
        for (const auto& entry :
             directory.entryInfoList(flags | (file->filter() & QDir::Hidden),
                                     QDir::DirsFirst | QDir::Name | QDir::IgnoreCase)) {
            if (entry.isDir() || patterns.isEmpty() || QDir::match(patterns, entry.fileName()))
                files.push_back(entry.absoluteFilePath());
        }
    }
    settings.Refresh();
    emit changed();
}
void NativeDialog::action(const QString& name, const QVariant& value) {
    if (!dialog)
        return;
    if (name == QStringLiteral("back")) {
        dialog->reject();
        return;
    }
    if (name == QStringLiteral("button")) {
        const int index = value.toInt();
        QList<QAbstractButton*> buttons;
        if (auto* message = qobject_cast<QMessageBox*>(dialog.data()))
            buttons = message->buttons();
        else if (auto* box = dialog->findChild<QDialogButtonBox*>())
            buttons = box->buttons();
        if (index >= 0 && index < buttons.size())
            buttons[index]->click();
        else if (index == -1)
            dialog->accept();
        refresh();
        return;
    }
    auto* file = qobject_cast<QFileDialog*>(dialog.data());
    if (!file)
        return;
    if (name == QStringLiteral("accept-files")) {
        if (auto* edit = file->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"))) {
            QStringList quoted;
            for (const auto& path : selected)
                quoted.push_back(QLatin1Char('"') + path + QLatin1Char('"'));
            edit->setText(quoted.join(QLatin1Char(' ')));
        }
        QMetaObject::invokeMethod(file, "accept", Qt::QueuedConnection);
        return;
    }
    if (name == QStringLiteral("up")) {
        QDir directory = file->directory();
        directory.cdUp();
        file->setDirectory(directory);
    } else if (name == QStringLiteral("home"))
        file->setDirectory(QDir::homePath());
    else if (name == QStringLiteral("drive")) {
        const auto drives = QDir::drives();
        const int index = value.toInt();
        if (index >= 0 && index < drives.size())
            file->setDirectory(drives[index].absoluteFilePath());
    } else if (name == QStringLiteral("file")) {
        const int index = value.toInt();
        if (index < 0 || index >= files.size())
            return;
        if (QFileInfo(files[index]).isDir())
            file->setDirectory(files[index]);
        else if (file->fileMode() == QFileDialog::ExistingFiles) {
            if (selected.contains(files[index]))
                selected.removeAll(files[index]);
            else
                selected.push_back(files[index]);
        } else {
            file->selectFile(files[index]);
            QMetaObject::invokeMethod(file, "accept", Qt::QueuedConnection);
        }
    } else if (name == QStringLiteral("path")) {
        const QString path = value.toString();
        if (QFileInfo(path).isDir())
            file->setDirectory(path);
        else
            file->selectFile(path);
    } else if (name == QStringLiteral("filter"))
        file->selectNameFilter(value.toString());
    else if (name == QStringLiteral("choose-directory")) {
        file->selectFile(file->directory().absolutePath());
        QMetaObject::invokeMethod(file, "accept", Qt::QueuedConnection);
    }
    refresh();
}
} // namespace QtCommon::Couch
