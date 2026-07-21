// coding: utf-8
// =============================================================================
// ProgramPasswordDialog.h
//
// PwdVault 程序密码管理对话框。三种模式：
//   - Enable：首次启用程序密码，输入两次新密码 + 强度条，
//             调用 client->enable_program_password(password)
//   - Disable：禁用程序密码，输入当前密码验证，
//             调用 client->disable_program_password(password)
//   - Change：修改程序密码，输入旧密码 + 两次新密码 + 强度条，
//             调用 client->change_program_password(old, new)
//
// 操作成功后 emit finished(true)，由 SettingsView 刷新状态显示。
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

class ProgramPasswordDialog : public QWidget {
    Q_OBJECT
public:
    enum class Mode {
        Enable,
        Disable,
        Change,
    };

    ProgramPasswordDialog(IpcClient* client, Mode mode, QWidget* parent = nullptr);
    ~ProgramPasswordDialog() override;

signals:
    /// 操作成功时触发，调用方应刷新程序密码状态显示。
    void succeeded();

    /// 用户关闭对话框但操作未成功时触发。
    void rejected();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void on_show_password_toggled(bool checked);
    void on_new_password_changed(const QString& text);
    void on_submit_clicked();

private:
    void build_ui_enable();
    void build_ui_disable();
    void build_ui_change();
    void update_strength(const QString& password);
    void set_error(const QString& message);

    IpcClient* client_;
    Mode mode_;
    bool succeeded_ = false;

    // 通用控件
    QLabel* title_label_ = nullptr;
    QLabel* hint_label_ = nullptr;
    QLineEdit* old_password_edit_ = nullptr;      // 仅 Change/Disable
    QLineEdit* new_password_edit_ = nullptr;      // 仅 Enable/Change
    QLineEdit* confirm_edit_ = nullptr;           // 仅 Enable/Change
    QCheckBox* show_password_check_ = nullptr;
    QPushButton* submit_button_ = nullptr;
    QLabel* error_label_ = nullptr;
    QProgressBar* strength_bar_ = nullptr;        // 仅 Enable/Change
    QLabel* strength_label_ = nullptr;            // 仅 Enable/Change
};

}  // namespace pwdvault::ui
