// coding: utf-8
// =============================================================================
// ProgramPasswordDialog.cpp
//
// PwdVault 程序密码管理对话框实现。根据 Mode 构建不同 UI 并调用对应 IPC 命令。
// =============================================================================
#include "ProgramPasswordDialog.h"
#include "IpcClient.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

namespace pwdvault::ui {

namespace {

/// 强度条颜色：< 40 红、40-80 黄、> 80 绿。
QString strength_color(int bits) {
    if (bits < 40) return QStringLiteral("red");
    if (bits < 80) return QStringLiteral("orange");
    return QStringLiteral("green");
}

/// 强度文本描述。
QString strength_text(int bits) {
    if (bits < 40) return QStringLiteral("弱");
    if (bits < 80) return QStringLiteral("中");
    return QStringLiteral("强");
}

}  // namespace

ProgramPasswordDialog::ProgramPasswordDialog(IpcClient* client, Mode mode, QWidget* parent)
    : QWidget(parent), client_(client), mode_(mode)
{
    setWindowFlags(Qt::Dialog | Qt::WindowTitleHint);
    setWindowModality(Qt::ApplicationModal);
    setMinimumSize(420, 360);

    switch (mode_) {
        case Mode::Enable:  setWindowTitle(QStringLiteral("PwdVault - 启用程序密码")); break;
        case Mode::Disable: setWindowTitle(QStringLiteral("PwdVault - 禁用程序密码")); break;
        case Mode::Change:  setWindowTitle(QStringLiteral("PwdVault - 修改程序密码")); break;
    }

    switch (mode_) {
        case Mode::Enable:  build_ui_enable();  break;
        case Mode::Disable: build_ui_disable(); break;
        case Mode::Change:  build_ui_change();  break;
    }
}

ProgramPasswordDialog::~ProgramPasswordDialog() = default;

void ProgramPasswordDialog::closeEvent(QCloseEvent* event) {
    if (!succeeded_) {
        emit rejected();
    }
    QWidget::closeEvent(event);
}

// ---------------------------------------------------------------------------
// UI 构建
// ---------------------------------------------------------------------------

void ProgramPasswordDialog::build_ui_enable() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);

    title_label_ = new QLabel(QStringLiteral("启用程序密码"), this);
    QFont title_font = title_label_->font();
    title_font.setPointSize(14);
    title_font.setBold(true);
    title_label_->setFont(title_font);
    layout->addWidget(title_label_);

    hint_label_ = new QLabel(
        QStringLiteral("启用后，所有保存的密码将被加密存储。\n"
                       "请妥善保管程序密码，<b>忘记后无法找回</b>，所有数据将永久丢失。"),
        this);
    hint_label_->setWordWrap(true);
    hint_label_->setStyleSheet(QStringLiteral("color: #555;"));
    layout->addWidget(hint_label_);

    new_password_edit_ = new QLineEdit(this);
    new_password_edit_->setEchoMode(QLineEdit::Password);
    new_password_edit_->setPlaceholderText(QStringLiteral("请输入程序密码"));
    layout->addWidget(new QLabel(QStringLiteral("程序密码："), this));
    layout->addWidget(new_password_edit_);

    confirm_edit_ = new QLineEdit(this);
    confirm_edit_->setEchoMode(QLineEdit::Password);
    confirm_edit_->setPlaceholderText(QStringLiteral("再次输入程序密码"));
    layout->addWidget(new QLabel(QStringLiteral("确认密码："), this));
    layout->addWidget(confirm_edit_);

    strength_bar_ = new QProgressBar(this);
    strength_bar_->setRange(0, 100);
    strength_bar_->setValue(0);
    strength_bar_->setTextVisible(false);
    strength_bar_->setFixedHeight(8);
    layout->addWidget(strength_bar_);

    strength_label_ = new QLabel(QStringLiteral("强度：-"), this);
    layout->addWidget(strength_label_);

    show_password_check_ = new QCheckBox(QStringLiteral("显示密码"), this);
    layout->addWidget(show_password_check_);

    error_label_ = new QLabel(this);
    error_label_->setStyleSheet(QStringLiteral("color: red;"));
    error_label_->setWordWrap(true);
    layout->addWidget(error_label_);

    layout->addStretch(1);

    submit_button_ = new QPushButton(QStringLiteral("启用"), this);
    submit_button_->setDefault(true);
    submit_button_->setMinimumHeight(32);
    layout->addWidget(submit_button_);

    connect(show_password_check_, &QCheckBox::toggled,
            this, &ProgramPasswordDialog::on_show_password_toggled);
    connect(new_password_edit_, &QLineEdit::textChanged,
            this, &ProgramPasswordDialog::on_new_password_changed);
    connect(submit_button_, &QPushButton::clicked,
            this, &ProgramPasswordDialog::on_submit_clicked);
}

void ProgramPasswordDialog::build_ui_disable() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);

    title_label_ = new QLabel(QStringLiteral("禁用程序密码"), this);
    QFont title_font = title_label_->font();
    title_font.setPointSize(14);
    title_font.setBold(true);
    title_label_->setFont(title_font);
    layout->addWidget(title_label_);

    hint_label_ = new QLabel(
        QStringLiteral("禁用后，所有保存的密码将以明文形式存储在磁盘上。\n"
                       "请输入当前程序密码以确认操作。"),
        this);
    hint_label_->setWordWrap(true);
    hint_label_->setStyleSheet(QStringLiteral("color: #555;"));
    layout->addWidget(hint_label_);

    old_password_edit_ = new QLineEdit(this);
    old_password_edit_->setEchoMode(QLineEdit::Password);
    old_password_edit_->setPlaceholderText(QStringLiteral("请输入当前程序密码"));
    old_password_edit_->setFocus();
    layout->addWidget(new QLabel(QStringLiteral("当前程序密码："), this));
    layout->addWidget(old_password_edit_);

    show_password_check_ = new QCheckBox(QStringLiteral("显示密码"), this);
    layout->addWidget(show_password_check_);

    error_label_ = new QLabel(this);
    error_label_->setStyleSheet(QStringLiteral("color: red;"));
    error_label_->setWordWrap(true);
    layout->addWidget(error_label_);

    layout->addStretch(1);

    submit_button_ = new QPushButton(QStringLiteral("禁用"), this);
    submit_button_->setDefault(true);
    submit_button_->setMinimumHeight(32);
    layout->addWidget(submit_button_);

    connect(show_password_check_, &QCheckBox::toggled,
            this, &ProgramPasswordDialog::on_show_password_toggled);
    connect(submit_button_, &QPushButton::clicked,
            this, &ProgramPasswordDialog::on_submit_clicked);
}

void ProgramPasswordDialog::build_ui_change() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);

    title_label_ = new QLabel(QStringLiteral("修改程序密码"), this);
    QFont title_font = title_label_->font();
    title_font.setPointSize(14);
    title_font.setBold(true);
    title_label_->setFont(title_font);
    layout->addWidget(title_label_);

    hint_label_ = new QLabel(
        QStringLiteral("请输入当前程序密码与新程序密码。\n"
                       "已保存的密码条目无需重新加密。"),
        this);
    hint_label_->setWordWrap(true);
    hint_label_->setStyleSheet(QStringLiteral("color: #555;"));
    layout->addWidget(hint_label_);

    old_password_edit_ = new QLineEdit(this);
    old_password_edit_->setEchoMode(QLineEdit::Password);
    old_password_edit_->setPlaceholderText(QStringLiteral("当前程序密码"));
    old_password_edit_->setFocus();
    layout->addWidget(new QLabel(QStringLiteral("当前程序密码："), this));
    layout->addWidget(old_password_edit_);

    new_password_edit_ = new QLineEdit(this);
    new_password_edit_->setEchoMode(QLineEdit::Password);
    new_password_edit_->setPlaceholderText(QStringLiteral("新程序密码"));
    layout->addWidget(new QLabel(QStringLiteral("新程序密码："), this));
    layout->addWidget(new_password_edit_);

    confirm_edit_ = new QLineEdit(this);
    confirm_edit_->setEchoMode(QLineEdit::Password);
    confirm_edit_->setPlaceholderText(QStringLiteral("再次输入新程序密码"));
    layout->addWidget(new QLabel(QStringLiteral("确认新密码："), this));
    layout->addWidget(confirm_edit_);

    strength_bar_ = new QProgressBar(this);
    strength_bar_->setRange(0, 100);
    strength_bar_->setValue(0);
    strength_bar_->setTextVisible(false);
    strength_bar_->setFixedHeight(8);
    layout->addWidget(strength_bar_);

    strength_label_ = new QLabel(QStringLiteral("强度：-"), this);
    layout->addWidget(strength_label_);

    show_password_check_ = new QCheckBox(QStringLiteral("显示密码"), this);
    layout->addWidget(show_password_check_);

    error_label_ = new QLabel(this);
    error_label_->setStyleSheet(QStringLiteral("color: red;"));
    error_label_->setWordWrap(true);
    layout->addWidget(error_label_);

    layout->addStretch(1);

    submit_button_ = new QPushButton(QStringLiteral("修改"), this);
    submit_button_->setDefault(true);
    submit_button_->setMinimumHeight(32);
    layout->addWidget(submit_button_);

    connect(show_password_check_, &QCheckBox::toggled,
            this, &ProgramPasswordDialog::on_show_password_toggled);
    connect(new_password_edit_, &QLineEdit::textChanged,
            this, &ProgramPasswordDialog::on_new_password_changed);
    connect(submit_button_, &QPushButton::clicked,
            this, &ProgramPasswordDialog::on_submit_clicked);
}

// ---------------------------------------------------------------------------
// 槽函数
// ---------------------------------------------------------------------------

void ProgramPasswordDialog::on_show_password_toggled(bool checked) {
    const auto mode = checked ? QLineEdit::Normal : QLineEdit::Password;
    if (old_password_edit_) old_password_edit_->setEchoMode(mode);
    if (new_password_edit_) new_password_edit_->setEchoMode(mode);
    if (confirm_edit_) confirm_edit_->setEchoMode(mode);
}

void ProgramPasswordDialog::on_new_password_changed(const QString& text) {
    if (mode_ == Mode::Enable || mode_ == Mode::Change) {
        update_strength(text);
    }
}

void ProgramPasswordDialog::on_submit_clicked() {
    set_error(QString());

    if (!client_) {
        set_error(QStringLiteral("内部错误：IPC 客户端不可用。"));
        return;
    }

    switch (mode_) {
        case Mode::Enable: {
            const std::string password = new_password_edit_->text().toStdString();
            if (password.empty()) {
                set_error(QStringLiteral("程序密码不能为空。"));
                return;
            }
            const std::string confirm = confirm_edit_->text().toStdString();
            if (password != confirm) {
                set_error(QStringLiteral("两次输入的密码不一致。"));
                return;
            }
            auto result = client_->enable_program_password(password);
            if (result.ok() && result.value().success) {
                succeeded_ = true;
                QMessageBox::information(this, QStringLiteral("启用成功"),
                    QStringLiteral("程序密码已启用，所有密码条目已加密保存。"));
                emit succeeded();
            } else {
                QString msg = result.ok()
                    ? QString::fromStdString(result.value().error_message)
                    : QString::fromStdString(result.error().what());
                set_error(msg.isEmpty()
                              ? QStringLiteral("启用程序密码失败。")
                              : QStringLiteral("启用失败：%1").arg(msg));
            }
            break;
        }
        case Mode::Disable: {
            const std::string password = old_password_edit_->text().toStdString();
            if (password.empty()) {
                set_error(QStringLiteral("请输入当前程序密码。"));
                return;
            }
            auto result = client_->disable_program_password(password);
            if (result.ok() && result.value().success) {
                succeeded_ = true;
                QMessageBox::information(this, QStringLiteral("禁用成功"),
                    QStringLiteral("程序密码已禁用，所有密码条目已转为明文保存。"));
                emit succeeded();
            } else {
                QString msg = result.ok()
                    ? QString::fromStdString(result.value().error_message)
                    : QString::fromStdString(result.error().what());
                set_error(msg.isEmpty()
                              ? QStringLiteral("禁用程序密码失败。")
                              : QStringLiteral("禁用失败：%1").arg(msg));
            }
            break;
        }
        case Mode::Change: {
            const std::string old_pw = old_password_edit_->text().toStdString();
            const std::string new_pw = new_password_edit_->text().toStdString();
            if (old_pw.empty() || new_pw.empty()) {
                set_error(QStringLiteral("密码不能为空。"));
                return;
            }
            const std::string confirm = confirm_edit_->text().toStdString();
            if (new_pw != confirm) {
                set_error(QStringLiteral("两次输入的新密码不一致。"));
                return;
            }
            if (old_pw == new_pw) {
                set_error(QStringLiteral("新密码不能与旧密码相同。"));
                return;
            }
            auto result = client_->change_program_password(old_pw, new_pw);
            if (result.ok() && result.value().success) {
                succeeded_ = true;
                QMessageBox::information(this, QStringLiteral("修改成功"),
                    QStringLiteral("程序密码已修改。"));
                emit succeeded();
            } else {
                QString msg = result.ok()
                    ? QString::fromStdString(result.value().error_message)
                    : QString::fromStdString(result.error().what());
                set_error(msg.isEmpty()
                              ? QStringLiteral("修改程序密码失败。")
                              : QStringLiteral("修改失败：%1").arg(msg));
            }
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// 辅助
// ---------------------------------------------------------------------------

void ProgramPasswordDialog::update_strength(const QString& password) {
    if (!strength_bar_ || !strength_label_ || !client_) return;

    if (password.isEmpty()) {
        strength_bar_->setValue(0);
        strength_bar_->setStyleSheet(QString());
        strength_label_->setText(QStringLiteral("强度：-"));
        return;
    }

    auto result = client_->estimate_strength(password.toStdString());
    int bits = 0;
    if (result.ok()) {
        bits = result.value().strength_bits;
    }

    const int pct = (bits >= 128) ? 100 : (bits * 100 / 128);
    strength_bar_->setValue(pct);

    const QString color = strength_color(bits);
    strength_bar_->setStyleSheet(
        QStringLiteral("QProgressBar::chunk { background-color: %1; }").arg(color));

    strength_label_->setText(
        QStringLiteral("强度：%1（%2 bit）").arg(strength_text(bits)).arg(bits));
}

void ProgramPasswordDialog::set_error(const QString& message) {
    if (error_label_) error_label_->setText(message);
}

}  // namespace pwdvault::ui
