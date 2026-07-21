// coding: utf-8
// =============================================================================
// SettingsView.h
//
// PwdVault 设置视图。包含：
//   - 版本信息与数据存储路径
//   - 程序密码管理区：状态显示 + 启用/禁用/修改密码按钮
//   - 锁定按钮（仅程序密码已启用时可用）
//   - 关于按钮
//
// 程序密码管理通过 ProgramPasswordDialog 完成，操作成功后刷新状态显示。
// 锁定按钮调用 client->lock()，emit lock_requested 由 MainWindow 切回解锁视图。
// =============================================================================
#pragma once

#include <QWidget>

class QLabel;
class QPushButton;

namespace pwdvault::ui {

class IpcClient;

class SettingsView : public QWidget {
    Q_OBJECT
public:
    explicit SettingsView(IpcClient* client, QWidget* parent = nullptr);
    ~SettingsView() override;

signals:
    /// 用户点击「锁定」且 lock() 调用成功后触发，
    /// MainWindow 收到后显示 UnlockView。
    void lock_requested();

    /// 程序密码状态发生变化（启用/禁用）后触发，
    /// MainWindow 可据此调整锁定按钮可见性等。
    void password_state_changed(bool enabled);

public slots:
    /// 查询 service 当前 vault 状态并刷新 UI 显示。
    /// 应在视图显示前调用（如主窗口启动后、解锁成功后）。
    void refresh_status();

private slots:
    void on_enable_clicked();
    void on_disable_clicked();
    void on_change_clicked();
    void on_lock_clicked();
    void on_about_clicked();

private:
    void build_ui();
    /// 根据 password_enabled_ 调整各按钮的可见性与可用性。
    void update_button_visibility();

    IpcClient* client_;
    QLabel* version_label_ = nullptr;
    QLabel* storage_path_label_ = nullptr;
    QLabel* password_status_label_ = nullptr;
    QPushButton* enable_button_ = nullptr;
    QPushButton* disable_button_ = nullptr;
    QPushButton* change_button_ = nullptr;
    QPushButton* lock_button_ = nullptr;
    QPushButton* about_button_ = nullptr;

    /// 缓存当前程序密码是否已启用。
    bool password_enabled_ = false;
};

}  // namespace pwdvault::ui
