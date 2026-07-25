// coding: utf-8
// =============================================================================
// InputView.h
//
// PwdVault 录入视图（新设计）。640px 居中卡片：
//   - 标题「新增密码条目」+ 副标题「所有字段加密存储于本地」
//   - 表单：网站（globe 图标）/ 用户名（at-sign 图标）/ 密码（key 图标 + 生成 + 可见性）
//     + 4 段强度条
//   - 备注（textarea）
//   - 底部：清除 + 保存按钮
//
// 保存成功后 emit entry_added(id)，清空表单并弹提示。
// 「生成」按钮 emit password_generator_requested，由 MainWindow 切换到生成器视图。
// =============================================================================
#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QProgressBar;
class QTimer;

namespace pwdvault::ui {

class IpcClient;

class InputView : public QWidget {
    Q_OBJECT
public:
    explicit InputView(IpcClient* client, QWidget* parent = nullptr);
    ~InputView() override;

    /// 由 MainWindow 调用：将生成器生成的密码填入密码输入框。
    void set_password(const QString& password);

    /// 让第一个字段获得焦点（由 MainWindow 在点击「新增」时调用）。
    void focus_first_field();

signals:
    /// 新条目保存成功时触发，\p id 为 service 分配的主键。
    void entry_added(int64_t id);

    /// 用户点击「生成密码」时触发，MainWindow 切换到生成器视图。
    void password_generator_requested();

private slots:
    void on_generate_clicked();
    void on_toggle_password_clicked();
    void on_password_changed(const QString& text);
    void on_save_clicked();
    void on_clear_clicked();

private:
    void build_ui();
    void update_strength(const QString& password);
    void set_error(const QString& message);

    IpcClient* client_;

    QLineEdit* website_edit_ = nullptr;
    QLineEdit* username_edit_ = nullptr;
    QLineEdit* password_edit_ = nullptr;
    QPlainTextEdit* note_edit_ = nullptr;
    QPushButton* generate_button_ = nullptr;
    QPushButton* visibility_btn_ = nullptr;
    QProgressBar* strength_bar_ = nullptr;
    QLabel* strength_label_ = nullptr;
    QPushButton* save_button_ = nullptr;
    QPushButton* clear_button_ = nullptr;
    QLabel* error_label_ = nullptr;
    QTimer* strength_timer_ = nullptr;  ///< 强度评估 debounce 计时器
    bool password_visible_ = false;
};

}  // namespace pwdvault::ui
