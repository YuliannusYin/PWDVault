// coding: utf-8
// =============================================================================
// InputView.h
//
// PwdVault 录入视图（新设计）。640px 居中卡片：
//   - 标题「新增密码条目」+ 副标题「所有字段加密存储于本地」
//   - 表单：*条目名 / *账号 / 用户名 / *密码（key 图标 + 生成 + 可见性）
//     + 4 段强度条 / 网站 / 标签（芯片流式输入）/ 备注（markdown 源码）
//   - 底部：清除 + 保存按钮
//
// 带 * 号为必填：entry_name / account / password。
// 保存成功后 emit entry_added(id)，清空表单并弹提示。
// 「生成」按钮 emit password_generator_requested，由 MainWindow 切换到生成器视图。
// =============================================================================
#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QProgressBar;
class QShowEvent;
class QTimer;

namespace pwdvault::core {
struct StrengthEstimate;
}

namespace pwdvault::ui {

class IpcClient;
class TagInputWidget;

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

protected:
    /// 首次显示时触发标签补全列表异步加载（Task 28 防重）。
    void showEvent(QShowEvent* event) override;

private slots:
    void on_generate_clicked();
    void on_toggle_password_clicked();
    void on_password_changed(const QString& text);
    void on_save_clicked();
    void on_clear_clicked();

private:
    void build_ui();
    /// 入口：启动 debounce 计时器，300ms 无新输入后发起异步强度评估。
    void update_strength(const QString& password);
    /// UI 更新：根据 service 返回的 StrengthEstimate 刷新强度条与文案。
    void update_strength_ui(const core::StrengthEstimate& estimate);
    void set_error(const QString& message);
    /// 同步入口（向后兼容），内部委托给 async 版本。
    void refresh_existing_tags();
    /// 异步加载全部已知标签，刷新 TagInputWidget 的补全列表。
    void refresh_existing_tags_async();

    IpcClient* client_;

    QLineEdit* entry_name_edit_ = nullptr;  ///< *必填* 条目显示标题
    QLineEdit* account_edit_ = nullptr;      ///< *必填* 登录账号
    QLineEdit* username_edit_ = nullptr;     ///< 可选 显示名
    QLineEdit* password_edit_ = nullptr;     ///< *必填* 明文密码
    QLineEdit* website_edit_ = nullptr;     ///< 可选 站点 URL
    TagInputWidget* tag_input_ = nullptr;    ///< 标签芯片输入
    QPlainTextEdit* note_edit_ = nullptr;     ///< 备注（markdown 源码）
    QPushButton* generate_button_ = nullptr;
    QPushButton* visibility_btn_ = nullptr;
    QProgressBar* strength_bar_ = nullptr;
    QLabel* strength_label_ = nullptr;
    QPushButton* save_button_ = nullptr;
    QPushButton* clear_button_ = nullptr;
    QLabel* error_label_ = nullptr;
    QTimer* strength_timer_ = nullptr;  ///< 强度评估 debounce 计时器
    bool password_visible_ = false;
    bool tags_loaded_ = false;       ///< Task 28：标签补全列表是否已加载，防重复请求
    bool saving_ = false;            ///< 保存中状态，防重复点击
    QString pending_password_;       ///< debounce 期间捕获的密码文本
};

}  // namespace pwdvault::ui
