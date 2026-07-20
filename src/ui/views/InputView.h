// coding: utf-8
// =============================================================================
// InputView.h
//
// PwdVault 录入视图。添加新密码条目：
//   - 网站/应用名称（必填）
//   - 账号/用户名（必填）
//   - 密码（必填，默认掩码）
//   - 备注（可选）
//
// 保存成功后 emit entry_added(id)，清空表单并弹提示。
// 「生成密码」按钮 emit password_generator_requested，由 MainWindow 切换到生成器视图。
// =============================================================================
#pragma once

#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

namespace pwdvault::ui {

class IpcClient;

class InputView : public QWidget {
    Q_OBJECT
public:
    explicit InputView(IpcClient* client, QWidget* parent = nullptr);
    ~InputView() override;

    /// 由 MainWindow 调用：将生成器生成的密码填入密码输入框。
    void set_password(const QString& password);

signals:
    /// 新条目保存成功时触发，\p id 为 service 分配的主键。
    void entry_added(int64_t id);

    /// 用户点击「生成密码」时触发，MainWindow 切换到生成器视图。
    void password_generator_requested();

private slots:
    void on_show_password_toggled(bool checked);
    void on_generate_clicked();
    void on_save_clicked();
    void on_clear_clicked();

private:
    void build_ui();
    void set_error(const QString& message);

    IpcClient* client_;

    QLineEdit* website_edit_ = nullptr;
    QLineEdit* username_edit_ = nullptr;
    QLineEdit* password_edit_ = nullptr;
    QPlainTextEdit* note_edit_ = nullptr;
    QCheckBox* show_password_check_ = nullptr;
    QPushButton* save_button_ = nullptr;
    QPushButton* clear_button_ = nullptr;
    QPushButton* generate_button_ = nullptr;
    QLabel* error_label_ = nullptr;
};

}  // namespace pwdvault::ui
