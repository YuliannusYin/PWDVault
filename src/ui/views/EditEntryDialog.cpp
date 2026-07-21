// coding: utf-8
// =============================================================================
// EditEntryDialog.cpp
//
// 编辑密码条目对话框实现。预填 entry 字段，保存时调用 update_entry。
// =============================================================================
#include "EditEntryDialog.h"
#include "IpcClient.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

namespace pwdvault::ui {

EditEntryDialog::EditEntryDialog(IpcClient* client, const core::PasswordEntry& entry,
                                 QWidget* parent)
    : QDialog(parent), client_(client), entry_(entry)
{
    setWindowTitle(QStringLiteral("编辑条目"));
    setMinimumSize(400, 320);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(8);

    auto* form = new QFormLayout();
    form->setSpacing(8);

    website_edit_ = new QLineEdit(this);
    website_edit_->setText(QString::fromStdString(entry_.website));
    form->addRow(QStringLiteral("网站/应用名称（*）："), website_edit_);

    username_edit_ = new QLineEdit(this);
    username_edit_->setText(QString::fromStdString(entry_.username));
    form->addRow(QStringLiteral("账号/用户名（*）："), username_edit_);

    password_edit_ = new QLineEdit(this);
    password_edit_->setEchoMode(QLineEdit::Password);
    password_edit_->setText(QString::fromStdString(entry_.password));
    form->addRow(QStringLiteral("密码（*）："), password_edit_);

    show_password_check_ = new QCheckBox(QStringLiteral("显示密码"), this);
    form->addRow(QString(), show_password_check_);

    note_edit_ = new QPlainTextEdit(this);
    note_edit_->setPlainText(QString::fromStdString(entry_.note));
    note_edit_->setMinimumHeight(60);
    form->addRow(QStringLiteral("备注："), note_edit_);

    layout->addLayout(form);

    error_label_ = new QLabel(this);
    error_label_->setStyleSheet(QStringLiteral("color: red;"));
    error_label_->setWordWrap(true);
    layout->addWidget(error_label_);

    layout->addStretch(1);

    auto* btn_box = new QDialogButtonBox(Qt::Horizontal, this);
    save_button_ = btn_box->addButton(QStringLiteral("保存"), QDialogButtonBox::AcceptRole);
    btn_box->addButton(QStringLiteral("取消"), QDialogButtonBox::RejectRole);
    layout->addWidget(btn_box);

    connect(show_password_check_, &QCheckBox::toggled,
            this, &EditEntryDialog::on_show_password_toggled);
    connect(save_button_, &QPushButton::clicked,
            this, &EditEntryDialog::on_save_clicked);
    connect(btn_box, &QDialogButtonBox::rejected,
            this, &QDialog::reject);
}

EditEntryDialog::~EditEntryDialog() = default;

void EditEntryDialog::on_show_password_toggled(bool checked) {
    password_edit_->setEchoMode(checked ? QLineEdit::Normal : QLineEdit::Password);
}

void EditEntryDialog::on_save_clicked() {
    if (error_label_) error_label_->clear();

    if (!client_) {
        if (error_label_)
            error_label_->setText(QStringLiteral("内部错误：IPC 客户端不可用。"));
        return;
    }

    const QString website = website_edit_->text().trimmed();
    const QString username = username_edit_->text().trimmed();
    const QString password = password_edit_->text();

    if (website.isEmpty()) {
        if (error_label_)
            error_label_->setText(QStringLiteral("网站/应用名称不能为空。"));
        website_edit_->setFocus();
        return;
    }
    if (username.isEmpty()) {
        if (error_label_)
            error_label_->setText(QStringLiteral("账号/用户名不能为空。"));
        username_edit_->setFocus();
        return;
    }
    if (password.isEmpty()) {
        if (error_label_)
            error_label_->setText(QStringLiteral("密码不能为空。"));
        password_edit_->setFocus();
        return;
    }

    // 构造更新请求：保留 id、created_at，更新可编辑字段
    core::PasswordEntry updated = entry_;
    updated.website = website.toStdString();
    updated.username = username.toStdString();
    updated.password = password.toStdString();
    updated.note = note_edit_->toPlainText().toStdString();

    auto result = client_->update_entry(updated);
    if (result.ok()) {
        entry_ = result.value().entry;
        emit entry_updated(entry_.id);
        accept();
    } else {
        const QString msg = QString::fromStdString(result.error().what());
        if (error_label_)
            error_label_->setText(msg.isEmpty()
                                      ? QStringLiteral("保存失败。")
                                      : QStringLiteral("保存失败：%1").arg(msg));
    }
}

}  // namespace pwdvault::ui
