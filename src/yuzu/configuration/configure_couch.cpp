// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include "common/fs/fs_paths.h"
#include "common/fs/path_util.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/couch/theme.h"
#include "yuzu/configuration/configure_couch.h"

namespace {
QString ThemeDirectory() {
    return QDir(QString::fromStdString(Common::FS::PathToUTF8String(
                    Common::FS::GetEdenPath(Common::FS::EdenPath::ConfigDir))))
        .filePath(QStringLiteral("couch-themes"));
}
} // namespace
ConfigureCouch::ConfigureCouch(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("ConfigureCouch"));
    auto* form = new QFormLayout(this);
    startup = new QCheckBox(tr("Start in Couch Mode"), this);
    startup->setObjectName(QStringLiteral("couchStartup"));
    startup->setChecked(UISettings::values.couch_startup.GetValue());
    startup->setToolTip(tr("Open the optional controller-first interface at startup. Desktop Mode "
                           "stays available from the dock."));
    form->addRow(startup);
    invitation = new QCheckBox(tr("Show the Couch Mode invitation"), this);
    invitation->setObjectName(QStringLiteral("couchInvitation"));
    invitation->setChecked(!UISettings::values.couch_invitation_dismissed.GetValue());
    invitation->setToolTip(tr("Offer Couch Mode when starting in Desktop. Later asks on the next "
                             "launch; Never ask again hides the invitation."));
    form->addRow(invitation);
    theme = new QComboBox(this);
    theme->setObjectName(QStringLiteral("couchTheme"));
    RefreshThemes(QString::fromStdString(UISettings::values.couch_theme.GetValue()));
    form->addRow(tr("Couch theme"), theme);
    prompts = new QComboBox(this);
    prompts->setObjectName(QStringLiteral("couchPrompts"));
    prompts->addItem(tr("Automatic"), 0);
    prompts->addItem(tr("Xbox / Steam Deck"), 1);
    prompts->addItem(tr("Nintendo"), 2);
    prompts->addItem(tr("PlayStation"), 3);
    prompts->setCurrentIndex(static_cast<int>(UISettings::values.couch_prompts.GetValue()));
    form->addRow(tr("Controller prompts"), prompts);
    scale = new QSpinBox(this);
    scale->setObjectName(QStringLiteral("couchTextScale"));
    scale->setRange(75, 125);
    scale->setSuffix(QStringLiteral("%"));
    scale->setValue(static_cast<int>(UISettings::values.couch_text_scale.GetValue()));
    form->addRow(tr("Text scale"), scale);
    reduced = new QCheckBox(tr("Reduced motion"), this);
    reduced->setObjectName(QStringLiteral("couchReducedMotion"));
    reduced->setChecked(UISettings::values.couch_reduced_motion.GetValue());
    form->addRow(reduced);
    auto* import = new QPushButton(tr("Import theme pack"), this);
    import->setObjectName(QStringLiteral("couchImportTheme"));
    connect(import, &QPushButton::clicked, this, &ConfigureCouch::ImportTheme);
    form->addRow(import);
}
void ConfigureCouch::RefreshThemes(const QString& selected) {
    QtCommon::Couch::Theme installed(ThemeDirectory());
    theme->clear();
    for (const auto& entry : installed.Installed()) {
        const auto data = entry.toMap();
        theme->addItem(data.value(QStringLiteral("name")).toString(),
                       data.value(QStringLiteral("id")));
    }
    const int index = theme->findData(selected);
    theme->setCurrentIndex(index < 0 ? 0 : index);
}
void ConfigureCouch::ImportTheme() {
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Import Couch theme"), QDir::homePath(), tr("Couch theme (theme.json)"));
    if (file.isEmpty())
        return;
    QtCommon::Couch::Theme installed(ThemeDirectory());
    QString id, error;
    if (!installed.Import(file, &id, &error)) {
        QMessageBox::warning(this, tr("Import theme"), error);
        return;
    }
    RefreshThemes(id);
}
void ConfigureCouch::ApplyConfiguration() {
    UISettings::values.couch_startup = startup->isChecked();
    UISettings::values.couch_invitation_dismissed = !invitation->isChecked();
    UISettings::values.couch_theme = theme->currentData().toString().toStdString();
    UISettings::values.couch_reduced_motion = reduced->isChecked();
    UISettings::values.couch_text_scale = static_cast<u32>(scale->value());
    UISettings::values.couch_prompts = static_cast<u32>(prompts->currentData().toInt());
}
