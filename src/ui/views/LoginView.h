// coding: utf-8
// =============================================================================
// LoginView.h
//
// PwdVault 登录视图。两种模式：
//   - 首次设置模式（is_first_time=true）：设置主密码，需输入两次并确认
//   - 验证解锁模式（is_first_time=false）：输入主密码解锁已存在的密码库
//
// 登录成功后 emit login_succeeded()，由 MainWindow 负责关闭本视图并显示主界面。
// 输入主密码时实时调用 estimate_strength 显示强度条（仅首次设置模式）。
// =============================================================================
#pragma once

#include <QWidget>

class QCheckBox;
class QCloseEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QProgressBar;

namespace pwdvault::ui {

class IpcClient;

class LoginView : public QWidget {
    Q_OBJECT
public:
    LoginView(IpcClient* client, bool is_first_time, QWidget* parent = nullptr);
    ~LoginView() override;

signals:
    /// 登录/解锁成功时触发。MainWindow 收到后关闭本视图并显示主界面。
    void login_succeeded();

    /// 用户关闭对话框但未登录成功时触发。MainWindow 收到后退出应用。
    void rejected();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void on_show_password_toggled(bool checked);
    void on_password_changed(const QString& text);
    void on_submit_clicked();

private:
    void build_ui_first_time();
    void build_ui_unlock();
    void update_strength(const QString& password);
    void set_error(const QString& message);

    IpcClient* client_;
    bool is_first_time_;
    bool login_succeeded_ = false;

    // 通用控件
    QLabel* title_label_ = nullptr;
    QLabel* hint_label_ = nullptr;
    QLineEdit* password_edit_ = nullptr;
    QLineEdit* confirm_edit_ = nullptr;      // 仅首次模式
    QCheckBox* show_password_check_ = nullptr;
    QPushButton* submit_button_ = nullptr;
    QLabel* error_label_ = nullptr;
    QProgressBar* strength_bar_ = nullptr;   // 仅首次模式
    QLabel* strength_label_ = nullptr;       // 仅首次模式
    QLabel* attempts_label_ = nullptr;       // 仅解锁模式

    int remaining_attempts_ = 5;
};

}  // namespace pwdvault::ui
