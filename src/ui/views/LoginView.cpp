// coding: utf-8
// =============================================================================
// LoginView.cpp
//
// PwdVault 登录视图实现。根据 is_first_time 构建不同的 UI：
//   - 首次模式：两次输入 + 强度条，调用 client->login(password, true)
//   - 解锁模式：单次输入 + 剩余次数，调用 client->unlock(password)
// =============================================================================
#include "LoginView.h"
#include "IpcClient.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include <string>

namespace pwdvault::ui {

namespace {

/// 强度条颜色：&lt; 40 红、40-80 黄、&gt; 80 绿。
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

LoginView::LoginView(IpcClient* client, bool is_first_time, QWidget* parent)
    : QWidget(parent), client_(client), is_first_time_(is_first_time)
{
    // 作为模态对话框显示
    setWindowFlags(Qt::Dialog | Qt::WindowTitleHint);
    setWindowModality(Qt::ApplicationModal);
    setWindowTitle(is_first_time_
                       ? QStringLiteral("PwdVault - 设置主密码")
                       : QStringLiteral("PwdVault - 解锁"));
    setMinimumSize(420, 360);

    if (is_first_time_) {
        build_ui_first_time();
    } else {
        build_ui_unlock();
    }
}

LoginView::~LoginView() = default;

void LoginView::closeEvent(QCloseEvent* event) {
    if (!login_succeeded_) {
        emit rejected();
    }
    QWidget::closeEvent(event);
}

// ---------------------------------------------------------------------------
// UI 构建
// ---------------------------------------------------------------------------

void LoginView::build_ui_first_time() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);

    title_label_ = new QLabel(QStringLiteral("首次使用，请设置主密码"), this);
    QFont title_font = title_label_->font();
    title_font.setPointSize(14);
    title_font.setBold(true);
    title_label_->setFont(title_font);
    layout->addWidget(title_label_);

    hint_label_ = new QLabel(
        QStringLiteral("主密码用于加密所有保存的密码。\n"
                       "请妥善保管，<b>忘记后无法找回</b>，所有数据将永久丢失。"),
        this);
    hint_label_->setWordWrap(true);
    hint_label_->setStyleSheet(QStringLiteral("color: #555;"));
    layout->addWidget(hint_label_);

    password_edit_ = new QLineEdit(this);
    password_edit_->setEchoMode(QLineEdit::Password);
    password_edit_->setPlaceholderText(QStringLiteral("请输入主密码"));
    layout->addWidget(new QLabel(QStringLiteral("主密码："), this));
    layout->addWidget(password_edit_);

    confirm_edit_ = new QLineEdit(this);
    confirm_edit_->setEchoMode(QLineEdit::Password);
    confirm_edit_->setPlaceholderText(QStringLiteral("再次输入主密码"));
    layout->addWidget(new QLabel(QStringLiteral("确认密码："), this));
    layout->addWidget(confirm_edit_);

    // 强度条
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

    submit_button_ = new QPushButton(QStringLiteral("设置主密码"), this);
    submit_button_->setDefault(true);
    submit_button_->setMinimumHeight(32);
    layout->addWidget(submit_button_);

    // 信号槽
    connect(show_password_check_, &QCheckBox::toggled,
            this, &LoginView::on_show_password_toggled);
    connect(password_edit_, &QLineEdit::textChanged,
            this, &LoginView::on_password_changed);
    connect(submit_button_, &QPushButton::clicked,
            this, &LoginView::on_submit_clicked);
}

void LoginView::build_ui_unlock() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);

    title_label_ = new QLabel(QStringLiteral("请输入主密码"), this);
    QFont title_font = title_label_->font();
    title_font.setPointSize(14);
    title_font.setBold(true);
    title_label_->setFont(title_font);
    layout->addWidget(title_label_);

    hint_label_ = new QLabel(
        QStringLiteral("密码库已锁定，请输入主密码解锁。"), this);
    hint_label_->setWordWrap(true);
    hint_label_->setStyleSheet(QStringLiteral("color: #555;"));
    layout->addWidget(hint_label_);

    password_edit_ = new QLineEdit(this);
    password_edit_->setEchoMode(QLineEdit::Password);
    password_edit_->setPlaceholderText(QStringLiteral("主密码"));
    password_edit_->setFocus();
    layout->addWidget(new QLabel(QStringLiteral("主密码："), this));
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

    // 信号槽
    connect(show_password_check_, &QCheckBox::toggled,
            this, &LoginView::on_show_password_toggled);
    connect(submit_button_, &QPushButton::clicked,
            this, &LoginView::on_submit_clicked);
}

// ---------------------------------------------------------------------------
// 槽函数
// ---------------------------------------------------------------------------

void LoginView::on_show_password_toggled(bool checked) {
    const auto mode = checked ? QLineEdit::Normal : QLineEdit::Password;
    if (password_edit_) password_edit_->setEchoMode(mode);
    if (confirm_edit_) confirm_edit_->setEchoMode(mode);
}

void LoginView::on_password_changed(const QString& text) {
    if (is_first_time_) {
        update_strength(text);
    }
}

void LoginView::on_submit_clicked() {
    set_error(QString());

    if (!client_) {
        set_error(QStringLiteral("内部错误：IPC 客户端不可用。"));
        return;
    }

    const std::string password = password_edit_->text().toStdString();

    if (password.empty()) {
        set_error(QStringLiteral("主密码不能为空。"));
        return;
    }

    if (is_first_time_) {
        // 首次设置：两次输入必须一致
        if (!confirm_edit_) {
            set_error(QStringLiteral("内部错误：缺少确认输入框。"));
            return;
        }
        const std::string confirm = confirm_edit_->text().toStdString();
        if (password != confirm) {
            set_error(QStringLiteral("两次输入的密码不一致。"));
            return;
        }

        auto result = client_->login(password, true);
        if (result.ok() && result.value().success) {
            login_succeeded_ = true;
            emit login_succeeded();
        } else {
            QString msg;
            if (result.ok()) {
                // LoginResponse 成功返回但 success=false
                msg = QString::fromStdString(result.value().error_message);
            } else {
                msg = QString::fromStdString(result.error().what());
            }
            set_error(msg.isEmpty()
                          ? QStringLiteral("设置主密码失败。")
                          : QStringLiteral("设置失败：%1").arg(msg));
        }
    } else {
        // 解锁模式
        auto result = client_->unlock(password);
        if (result.ok() && result.value().success) {
            login_succeeded_ = true;
            emit login_succeeded();
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
                          ? QStringLiteral("解锁失败，请检查主密码。")
                          : QStringLiteral("解锁失败：%1").arg(msg));
        }
    }
}

// ---------------------------------------------------------------------------
// 辅助
// ---------------------------------------------------------------------------

void LoginView::update_strength(const QString& password) {
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

    // 0-128 bits → 0-100%
    const int pct = (bits >= 128) ? 100 : (bits * 100 / 128);
    strength_bar_->setValue(pct);

    const QString color = strength_color(bits);
    strength_bar_->setStyleSheet(
        QStringLiteral("QProgressBar::chunk { background-color: %1; }").arg(color));

    strength_label_->setText(
        QStringLiteral("强度：%1（%2 bit）").arg(strength_text(bits)).arg(bits));
}

void LoginView::set_error(const QString& message) {
    if (error_label_) error_label_->setText(message);
}

}  // namespace pwdvault::ui
