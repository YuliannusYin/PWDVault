// coding: utf-8
// =============================================================================
// UnlockView.cpp
//
// PwdVault 解锁视图实现。构建单次密码输入 UI，调用 client->unlock(password)。
// 解锁失败时显示剩余尝试次数，5 次用尽后禁用提交按钮。
// =============================================================================
#include "UnlockView.h"
#include "IpcClient.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

namespace pwdvault::ui {

UnlockView::UnlockView(IpcClient* client, QWidget* parent)
    : QWidget(parent), client_(client)
{
    // 作为模态对话框显示
    setWindowFlags(Qt::Dialog | Qt::WindowTitleHint);
    setWindowModality(Qt::ApplicationModal);
    setWindowTitle(QStringLiteral("PwdVault - 解锁"));
    setMinimumSize(420, 280);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);

    title_label_ = new QLabel(QStringLiteral("请输入程序密码"), this);
    QFont title_font = title_label_->font();
    title_font.setPointSize(14);
    title_font.setBold(true);
    title_label_->setFont(title_font);
    layout->addWidget(title_label_);

    hint_label_ = new QLabel(
        QStringLiteral("密码库已锁定，请输入程序密码解锁。"), this);
    hint_label_->setWordWrap(true);
    hint_label_->setStyleSheet(QStringLiteral("color: #555;"));
    layout->addWidget(hint_label_);

    password_edit_ = new QLineEdit(this);
    password_edit_->setEchoMode(QLineEdit::Password);
    password_edit_->setPlaceholderText(QStringLiteral("程序密码"));
    password_edit_->setFocus();
    layout->addWidget(new QLabel(QStringLiteral("程序密码："), this));
    layout->addWidget(password_edit_);

    show_password_check_ = new QCheckBox(QStringLiteral("显示密码"), this);
    layout->addWidget(show_password_check_);

    error_label_ = new QLabel(this);
    error_label_->setStyleSheet(QStringLiteral("color: red;"));
    error_label_->setWordWrap(true);
    layout->addWidget(error_label_);

    attempts_label_ = new QLabel(
        QStringLiteral("剩余 %1 次尝试").arg(remaining_attempts_), this);
    attempts_label_->setStyleSheet(QStringLiteral("color: #555;"));
    layout->addWidget(attempts_label_);

    layout->addStretch(1);

    submit_button_ = new QPushButton(QStringLiteral("解锁"), this);
    submit_button_->setDefault(true);
    submit_button_->setMinimumHeight(32);
    layout->addWidget(submit_button_);

    connect(show_password_check_, &QCheckBox::toggled,
            this, &UnlockView::on_show_password_toggled);
    connect(submit_button_, &QPushButton::clicked,
            this, &UnlockView::on_submit_clicked);
}

UnlockView::~UnlockView() = default;

void UnlockView::closeEvent(QCloseEvent* event) {
    if (!unlock_succeeded_) {
        emit rejected();
    }
    QWidget::closeEvent(event);
}

// ---------------------------------------------------------------------------
// 槽函数
// ---------------------------------------------------------------------------

void UnlockView::on_show_password_toggled(bool checked) {
    const auto mode = checked ? QLineEdit::Normal : QLineEdit::Password;
    if (password_edit_) password_edit_->setEchoMode(mode);
}

void UnlockView::on_submit_clicked() {
    set_error(QString());

    if (!client_) {
        set_error(QStringLiteral("内部错误：IPC 客户端不可用。"));
        return;
    }

    const std::string password = password_edit_->text().toStdString();

    if (password.empty()) {
        set_error(QStringLiteral("程序密码不能为空。"));
        return;
    }

    auto result = client_->unlock(password);
    if (result.ok() && result.value().success) {
        unlock_succeeded_ = true;
        emit unlock_succeeded();
    } else {
        --remaining_attempts_;
        if (remaining_attempts_ > 0) {
            attempts_label_->setText(
                QStringLiteral("剩余 %1 次尝试").arg(remaining_attempts_));
        } else {
            attempts_label_->setText(
                QStringLiteral("尝试次数已用尽，请稍后再试。"));
            attempts_label_->setStyleSheet(QStringLiteral("color: red;"));
            if (submit_button_) submit_button_->setEnabled(false);
        }

        QString msg;
        if (result.ok()) {
            msg = QString::fromStdString(result.value().error_message);
        } else {
            msg = QString::fromStdString(result.error().what());
        }
        set_error(msg.isEmpty()
                      ? QStringLiteral("解锁失败，请检查程序密码。")
                      : QStringLiteral("解锁失败：%1").arg(msg));
    }
}

// ---------------------------------------------------------------------------
// 辅助
// ---------------------------------------------------------------------------

void UnlockView::set_error(const QString& message) {
    if (error_label_) error_label_->setText(message);
}

}  // namespace pwdvault::ui
