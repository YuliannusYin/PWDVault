// coding: utf-8
// =============================================================================
// EditEntryDialog.h
//
// 编辑密码条目对话框（模态）。预填已有数据，保存时调用 client->update_entry。
// 被 PasswordBookView 在点击「编辑」按钮时弹出。
// =============================================================================
#pragma once

#include <QDialog>

#include "Types.h"

class QCheckBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QLabel;

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

private slots:
    void on_show_password_toggled(bool checked);
    void on_save_clicked();

private:
    IpcClient* client_;
    core::PasswordEntry entry_;

    QLabel* error_label_ = nullptr;
    QLineEdit* website_edit_ = nullptr;
    QLineEdit* username_edit_ = nullptr;
    QLineEdit* password_edit_ = nullptr;
    QPlainTextEdit* note_edit_ = nullptr;
    QCheckBox* show_password_check_ = nullptr;
    QPushButton* save_button_ = nullptr;
};

}  // namespace pwdvault::ui
