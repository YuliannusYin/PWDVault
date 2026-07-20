// coding: utf-8
// =============================================================================
// InputView.cpp
//
// PwdVault 录入视图实现。校验必填项 → add_entry → emit entry_added。
// =============================================================================
#include "InputView.h"
#include "IpcClient.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include <string>

namespace pwdvault::ui {

InputView::InputView(IpcClient* client, QWidget* parent)
    : QWidget(parent), client_(client)
{
    build_ui();
}

InputView::~InputView() = default;

void InputView::build_ui() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(8);

    auto* title = new QLabel(QStringLiteral("录入新密码"), this);
    QFont f = title->font();
    f.setPointSize(13);
    f.setBold(true);
    title->setFont(f);
    layout->addWidget(title);

    auto* form = new QFormLayout();
    form->setSpacing(8);

    website_edit_ = new QLineEdit(this);
    website_edit_->setPlaceholderText(QStringLiteral("例如：github.com"));
    form->addRow(QStringLiteral("网站/应用名称（*）："), website_edit_);

    username_edit_ = new QLineEdit(this);
    username_edit_->setPlaceholderText(QStringLiteral("登录账号或邮箱"));
    form->addRow(QStringLiteral("账号/用户名（*）："), username_edit_);

    auto* pwd_row = new QHBoxLayout();
    password_edit_ = new QLineEdit(this);
    password_edit_->setEchoMode(QLineEdit::Password);
    password_edit_->setPlaceholderText(QStringLiteral("密码"));
    pwd_row->addWidget(password_edit_, 1);

    generate_button_ = new QPushButton(QStringLiteral("生成密码..."), this);
    pwd_row->addWidget(generate_button_);
    form->addRow(QStringLiteral("密码（*）："), pwd_row);

    show_password_check_ = new QCheckBox(QStringLiteral("显示密码"), this);
    form->addRow(QString(), show_password_check_);

    note_edit_ = new QPlainTextEdit(this);
    note_edit_->setPlaceholderText(QStringLiteral("可选：备注信息"));
    note_edit_->setMinimumHeight(80);
    form->addRow(QStringLiteral("备注："), note_edit_);

    layout->addLayout(form);

    error_label_ = new QLabel(this);
    error_label_->setStyleSheet(QStringLiteral("color: red;"));
    error_label_->setWordWrap(true);
    layout->addWidget(error_label_);

    layout->addStretch(1);

    auto* btn_row = new QHBoxLayout();
    btn_row->addStretch(1);
    clear_button_ = new QPushButton(QStringLiteral("清空"), this);
    save_button_ = new QPushButton(QStringLiteral("保存"), this);
    save_button_->setDefault(true);
    btn_row->addWidget(clear_button_);
    btn_row->addWidget(save_button_);
    layout->addLayout(btn_row);

    // 信号槽
    connect(show_password_check_, &QCheckBox::toggled,
            this, &InputView::on_show_password_toggled);
    connect(generate_button_, &QPushButton::clicked,
            this, &InputView::on_generate_clicked);
    connect(save_button_, &QPushButton::clicked,
            this, &InputView::on_save_clicked);
    connect(clear_button_, &QPushButton::clicked,
            this, &InputView::on_clear_clicked);
}

void InputView::set_password(const QString& password) {
    if (password_edit_) password_edit_->setText(password);
}

void InputView::on_show_password_toggled(bool checked) {
    password_edit_->setEchoMode(checked ? QLineEdit::Normal : QLineEdit::Password);
}

void InputView::on_generate_clicked() {
    // 通知 MainWindow 切换到生成器视图；生成后由 MainWindow 调用 set_password 回填
    emit password_generator_requested();
}

void InputView::on_save_clicked() {
    set_error(QString());

    if (!client_) {
        set_error(QStringLiteral("内部错误：IPC 客户端不可用。"));
        return;
    }

    const QString website = website_edit_->text().trimmed();
    const QString username = username_edit_->text().trimmed();
    const QString password = password_edit_->text();

    if (website.isEmpty()) {
        set_error(QStringLiteral("网站/应用名称不能为空。"));
        website_edit_->setFocus();
        return;
    }
    if (username.isEmpty()) {
        set_error(QStringLiteral("账号/用户名不能为空。"));
        username_edit_->setFocus();
        return;
    }
    if (password.isEmpty()) {
        set_error(QStringLiteral("密码不能为空。"));
        password_edit_->setFocus();
        return;
    }

    core::PasswordEntry entry;
    entry.id = 0;  // 新条目
    entry.website = website.toStdString();
    entry.username = username.toStdString();
    entry.password = password.toStdString();
    entry.note = note_edit_->toPlainText().toStdString();

    auto result = client_->add_entry(entry);
    if (result.ok()) {
        const int64_t new_id = result.value().entry.id;
        emit entry_added(new_id);
        on_clear_clicked();
        QMessageBox::information(this, QStringLiteral("保存成功"),
            QStringLiteral("密码条目已保存。"));
    } else {
        const QString msg = QString::fromStdString(result.error().what());
        set_error(msg.isEmpty()
                      ? QStringLiteral("保存失败。")
                      : QStringLiteral("保存失败：%1").arg(msg));
    }
}

void InputView::on_clear_clicked() {
    website_edit_->clear();
    username_edit_->clear();
    password_edit_->clear();
    note_edit_->clear();
    show_password_check_->setChecked(false);
    set_error(QString());
    website_edit_->setFocus();
}

void InputView::set_error(const QString& message) {
    if (error_label_) error_label_->setText(message);
}

}  // namespace pwdvault::ui
