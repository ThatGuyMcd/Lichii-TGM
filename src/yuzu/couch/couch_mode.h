// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <memory>
#include <QDialog>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QPointer>
#include <QQuickWidget>
#include <QTimer>
#include "qt_common/couch/theme.h"
#include "qt_common/couch/widget_model.h"

class QDialog;
class QMenu;
class QStandardItemModel;
class QQuickImageProvider;
struct SDL_Gamepad;

struct CouchSettingsSource {
    std::unique_ptr<QDialog> dialog;
    std::vector<QtCommon::Couch::SettingsPage> pages;
    std::function<bool()> apply;
    std::function<void()> discard;
    std::function<void()> cleanup;
    std::function<bool()> external_dirty;
    std::function<void(const QString&)> select_page;
    std::function<QImage(int)> controller_image;
    std::function<bool()> mapping;
    std::function<void(int)> mapping_key;
};

class CouchMode final : public QQuickWidget {
    Q_OBJECT
    Q_PROPERTY(QVariantMap state READ State NOTIFY stateChanged)
    Q_PROPERTY(QVariantList games READ Games NOTIFY gamesChanged)
    Q_PROPERTY(QVariantList collections READ Collections NOTIFY gamesChanged)
    Q_PROPERTY(QtCommon::Couch::WidgetModel* settings READ Settings CONSTANT)
    Q_PROPERTY(QtCommon::Couch::Theme* theme READ Theme CONSTANT)
public:
    using SourceFactory = std::function<CouchSettingsSource(const QString&, const QString&)>;
    explicit CouchMode(QStandardItemModel* library, QString config_directory,
                       SourceFactory source_factory, QWidget* parent = nullptr);
    ~CouchMode() override;
    QVariantMap State() const;
    QVariantList Games() const {
        return games;
    }
    QVariantList Collections() const;
    QtCommon::Couch::WidgetModel* Settings() {
        return &settings;
    }
    QtCommon::Couch::Theme* Theme() {
        return &theme;
    }
    void SetActive(bool active);
    bool RequestClose();
    void PrepareShutdown();
    void ReloadPreferences();
    void SetGameRunning(bool running);
    void SetProfile(const QString& name, const QImage& avatar);
    void ResetSourceAfterOperation(std::function<void()> operation);
    Q_INVOKABLE QString clipboardText() const;
    Q_INVOKABLE void action(const QString& name, const QVariant& value = {});
    Q_INVOKABLE void setSearch(const QString& text);
    Q_INVOKABLE QVariantMap describe(int id) const {
        return settings.Describe(id);
    }
    Q_INVOKABLE bool stage(int id, const QVariant& value);
    Q_INVOKABLE void edit(int id);
    Q_INVOKABLE void reset(int id, bool inherit);
    Q_INVOKABLE void save();
    Q_INVOKABLE void discard();
    Q_INVOKABLE void chooseTheme(const QString& id);
    Q_INVOKABLE QVariantMap editText(const QString& text, int caret, const QString& operation,
                                     const QString& inserted = {}) const;
    Q_INVOKABLE void completeText(const QString& target, const QString& text);
    Q_INVOKABLE void closeModal();
    Q_INVOKABLE void openKeyboard(const QString& target, const QString& text, const QString& title,
                                  bool secret = false);
    Q_INVOKABLE void treeAction(int row, int column, bool edit);
    Q_INVOKABLE void refreshLibrary();

signals:
    void stateChanged();
    void gamesChanged();
    void input(const QString& action);
    void LaunchGame(const QString& path);
    void DesktopRequested();
    void ConfigurationSaved();
    void AddDirectoryRequested();
    void FavoriteRequested(qulonglong program_id);
    void CloseApproved();
    void NavigationOwnerChanged(bool owns_input);
    void PauseRequested();
    void ResumeRequested();
    void StopRequested();
    void ExitRequested();

protected:
    bool eventFilter(QObject* object, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void Notify();
    bool Dirty() const;
    void Deliver(const QString& action);
    void PreviewPreferences();
    void RebuildGames();
    void EnsureSettings();
    void ReleaseSettings(bool discard);
    void SelectSection(const QString& id);
    void ChangeTab(const QString& tab);
    void Guard(std::function<void()> continuation);
    void ShowModal(QVariantMap data, std::function<void(const QVariant&)> accept = {});
    void PollController();
    void RefreshControllerImage();
    void ShowNativeDialog(QDialog* dialog);
    void ShowNativeMenu(QMenu* menu);
    void LoadCollections();
    bool SaveCollections();
    void Toast(const QString& text);
    bool SaveSettings();
    QString SelectedPath() const;
    QVariantMap SelectedGame() const;

    QPointer<QStandardItemModel> library;
    QString config_directory;
    SourceFactory factory;
    CouchSettingsSource source;
    QtCommon::Couch::WidgetModel settings;
    QtCommon::Couch::Theme theme;
    QQuickImageProvider* images{};
    QVariantList games;
    struct CoverCache {
        QByteArray encoded;
        QImage image;
        QString revision;
    };
    QHash<QString, CoverCache> cover_cache;
    QVariantList collection_data;
    QVariantMap ui;
    struct Modal {
        QVariantMap data;
        std::function<void(const QVariant&)> accept;
    };
    std::vector<Modal> modals;
    std::function<void()> pending;
    QTimer input_timer;
    QTimer model_timer;
    QElapsedTimer clock;
    QHash<int, SDL_Gamepad*> pads;
    QHash<int, quint64> pad_state;
    int navigation_pad{};
    QHash<QString, qint64> held_since;
    QHash<QString, qint64> repeated_at;
    QHash<QString, bool> held;
    std::vector<QPointer<QQuickWidget>> native_presentations;
    std::function<void()> pending_activation;
    int pending_binding{-1};
    bool game_running{};
    bool active{};
    bool sdl_owned{};
    bool previous_native_dialogs{};
    bool preparing_dialog{};
    int image_serial{};
    qint64 last_image{};
    qint64 test_exit{};
};
