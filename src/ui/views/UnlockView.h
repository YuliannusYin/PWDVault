// coding: utf-8
// =============================================================================
// UnlockView.h
//
// PwdVault 解锁视图。仅在程序密码已启用且密码库处于锁定状态时显示。
// 用户输入程序密码后调用 client->unlock(password)，成功后 emit unlock_succeeded()。
//
// 程序密码未启用时（明文模式）由 MainWindow 直接进入主界面，不会显示本视图。
// 启用程序密码、修改程序密码、禁用程序密码的入口位于 SettingsView。
// =============================================================================
#pragma once

#include <QWidget>

class QCheckBox;
class QCloseEvent;
class QLabel;
class QLineEdit;
class QPushButton;

namespace pwdvault::ui {

class IpcClient;

class UnlockView : public QWidget {
    Q_OBJECT
public:
    explicit UnlockView(IpcClient* client, QWidget* parent = nullptr);
    ~UnlockView() override;

signals:
    /// 解锁成功时触发。MainWindow 收到后关闭本视图并显示主界面。
    void unlock_succeeded();

    /// 用户关闭对话框但未解锁成功时触发。MainWindow 收到后退出应用。
    void rejected();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void on_show_password_toggled(bool checked);
    void on_submit_clicked();

private:
    void set_error(const QString& message);

    IpcClient* client_;
    bool unlock_succeeded_ = false;

    QLabel* title_label_ = nullptr;
    QLabel* hint_label_ = nullptr;
    QLineEdit* password_edit_ = nullptr;
    QCheckBox* show_password_check_ = nullptr;
    QPushButton* submit_button_ = nullptr;
    QLabel* error_label_ = nullptr;
    QLabel* attempts_label_ = nullptr;

    int remaining_attempts_ = 5;
};

}  // namespace pwdvault::ui
