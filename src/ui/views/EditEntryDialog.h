// coding: utf-8
// =============================================================================
// EditEntryDialog.h
//
// PwdVault 编辑条目对话框（新设计）。模态遮罩弹窗：
//   - 半透明遮罩层（rgba(0,0,0,0.55)）覆盖父窗口
//   - 560px 居中卡片：头部（pencil 图标 + 标题 + X 关闭）
//                       + 体（网站/用户名/密码（带可见性+生成）+ 备注 + 更新时间）
//                       + 尾部（取消 + 保存修改）
//
// 预填已有数据，保存时调用 client->update_entry。
// 仍继承 QDialog 以便 PasswordBookView 用 dlg.exec() 阻塞。
// =============================================================================
#pragma once

#include <QDialog>

#include "Types.h"

class QCloseEvent;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTimer;

namespace pwdvault::ui {

class IpcClient;

class EditEntryDialog : public QDialog {
    Q_OBJECT
public:
    EditEntryDialog(IpcClient* client, const core::PasswordEntry& entry,
                    QWidget* parent = nullptr);
    ~EditEntryDialog() override;

signals:
    /// 保存成功时触发，\p id 为更新条目的 id。
    void entry_updated(int64_t id);

protected:
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void on_toggle_password_clicked();
    void on_generate_clicked();
    void on_password_changed(const QString& text);
    void on_save_clicked();
    void on_cancel_clicked();

private:
    void build_ui();
    void update_strength(const QString& password);
    void set_error(const QString& message);

    IpcClient* client_;
    core::PasswordEntry entry_;

    QLabel* error_label_ = nullptr;
    QLineEdit* website_edit_ = nullptr;
    QLineEdit* username_edit_ = nullptr;
    QLineEdit* password_edit_ = nullptr;
    QPlainTextEdit* note_edit_ = nullptr;
    QPushButton* visibility_btn_ = nullptr;
    QPushButton* generate_btn_ = nullptr;
    QPushButton* save_button_ = nullptr;
    QPushButton* cancel_button_ = nullptr;
    QLabel* strength_label_ = nullptr;
    QLabel* updated_label_ = nullptr;
    QTimer* strength_timer_ = nullptr;  ///< 强度评估 debounce 计时器
    bool password_visible_ = false;
};

}  // namespace pwdvault::ui
