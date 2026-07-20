// coding: utf-8
// =============================================================================
// SettingsView.h
//
// PwdVault 设置视图（简化版）。显示版本号、数据存储路径；提供锁定与关于按钮。
// 锁定按钮调用 client->lock()，emit lock_requested 由 MainWindow 切回登录视图。
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
    /// MainWindow 收到后显示 LoginView（验证模式）。
    void lock_requested();

private slots:
    void on_lock_clicked();
    void on_about_clicked();

private:
    void build_ui();

    IpcClient* client_;
    QLabel* version_label_ = nullptr;
    QLabel* storage_path_label_ = nullptr;
    QPushButton* lock_button_ = nullptr;
    QPushButton* about_button_ = nullptr;
};

}  // namespace pwdvault::ui
