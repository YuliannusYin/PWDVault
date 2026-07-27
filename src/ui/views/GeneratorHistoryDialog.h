// coding: utf-8
// =============================================================================
// GeneratorHistoryDialog.h
//
// PwdVault 生成器历史记录对话框。
//
// 模态遮罩弹窗（720px 居中卡片）：
//   - 头部：clock 图标 + 标题 + X 关闭
//   - 工具栏：[显示密码] 切换 + 记录数标签 + [刷新] 按钮
//   - 表格：# / 时间 / 长度 / 密码 / 操作（复制、删除）
//   - 尾部：[清空全部] + [关闭]
//
// 通过 IpcClient 调用 service：
//   - list_generated_records：加载列表
//   - remove_generated_record(id)：删除单条
//   - clear_generated_records：清空全部
//
// 密码列默认以圆点遮罩显示（不可读），勾选「显示密码」后明文显示。
// 删除/清空操作后自动刷新表格。
// =============================================================================
#pragma once

#include <QDialog>

#include "Types.h"

#include <vector>

class QCheckBox;
class QCloseEvent;
class QKeyEvent;
class QFrame;
class QLabel;
class QPushButton;
class QTableWidget;

namespace pwdvault::ui {

class IpcClient;

class GeneratorHistoryDialog : public QDialog {
    Q_OBJECT
public:
    explicit GeneratorHistoryDialog(IpcClient* client, QWidget* parent = nullptr);
    ~GeneratorHistoryDialog() override;

    /// 重新加载记录列表并刷新表格。
    /// 调用方可在显示前调用以预填充。
    void reload();

signals:
    /// 用户在空状态点击「去生成密码」时触发，父窗口切换到 GeneratorView。
    void generate_requested();

protected:
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void on_show_password_toggled(bool checked);
    void on_refresh_clicked();
    void on_clear_all_clicked();
    void on_close_clicked();

private:
    void build_ui();
    /// 拉取 service 端列表并填充表格。失败时显示空表 + 错误提示。
    void populate_table();
    /// 重新渲染表格中密码列的显示样式（圆点 / 明文）。
    void refresh_password_cells();
    /// 在指定行渲染「复制 / 删除」操作按钮单元格。
    void install_action_widget(int row, int64_t record_id);
    /// 删除指定记录（调用 service 后刷新）。
    void delete_record(int64_t record_id);
    /// 设置错误提示文本（可空字符串清除）。
    void set_status(const QString& message, bool is_error);

    IpcClient* client_;
    std::vector<core::GeneratedPasswordRecord> records_;

    QCheckBox* show_password_check_ = nullptr;
    QLabel* status_label_ = nullptr;
    QPushButton* refresh_btn_ = nullptr;
    QTableWidget* table_ = nullptr;
    QPushButton* clear_all_btn_ = nullptr;
    QPushButton* close_btn_ = nullptr;
    QLabel* empty_label_ = nullptr;            ///< 空状态文案标签
    QPushButton* empty_action_button_ = nullptr;  ///< 空状态引导按钮
};

}  // namespace pwdvault::ui
