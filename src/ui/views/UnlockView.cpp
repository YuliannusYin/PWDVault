// coding: utf-8
// =============================================================================
// UnlockView.cpp
//
// PwdVault 解锁视图实现（新设计）。380px 居中卡片 + 盾牌图标 + 可见性切换。
// =============================================================================
#include "UnlockView.h"
#include "IpcClient.h"
#include "IconKit.h"

#include <QCloseEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QString>
#include <QStyle>
#include <QVBoxLayout>
#include <QWidget>

namespace pwdvault::ui {

UnlockView::UnlockView(IpcClient* client, QWidget* parent)
    : QWidget(parent), client_(client)
{
    // 模态全屏遮罩：覆盖父窗口整个区域
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setWindowModality(Qt::ApplicationModal);
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowTitle(QStringLiteral("PwdVault - 解锁"));

    // 自动覆盖父窗口大小
    if (parent) {
        setGeometry(parent->geometry());
    }

    build_ui();
}

UnlockView::~UnlockView() = default;

void UnlockView::build_ui() {
    // 根容器：垂直布局，整体居中
    auto* root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(0, 0, 0, 0);
    root_layout->setSpacing(0);

    // 半透明背景层（模拟遮罩）
    auto* overlay = new QFrame(this);
    overlay->setProperty("cssClass", QStringLiteral("modalOverlay"));
    auto* overlay_layout = new QVBoxLayout(overlay);
    overlay_layout->setContentsMargins(16, 16, 16, 16);
    overlay_layout->setAlignment(Qt::AlignCenter);

    // 卡片
    auto* card = new QFrame(overlay);
    card->setFixedWidth(380);
    card->setProperty("cssClass", QStringLiteral("modal"));
    auto* card_layout = new QVBoxLayout(card);
    card_layout->setContentsMargins(36, 36, 36, 28);
    card_layout->setSpacing(0);

    // 盾牌图标
    shield_icon_label_ = new QLabel(card);
    shield_icon_label_->setAlignment(Qt::AlignCenter);
    shield_icon_label_->setPixmap(
        QIcon(QStringLiteral(":/logo.png")).pixmap(QSize(56, 56)));
    card_layout->addWidget(shield_icon_label_);

    // 标题
    title_label_ = new QLabel(QStringLiteral("PwdVault"), card);
    title_label_->setAlignment(Qt::AlignCenter);
    title_label_->setProperty("cssClass", QStringLiteral("titleLg"));
    card_layout->addWidget(title_label_);

    // 副标题
    subtitle_label_ = new QLabel(
        QStringLiteral("输入程序密码以解锁保险库"), card);
    subtitle_label_->setAlignment(Qt::AlignCenter);
    subtitle_label_->setProperty("cssClass", QStringLiteral("muted"));
    card_layout->addWidget(subtitle_label_);

    // 密码输入框容器：lock 图标 + 输入框 + 可见性按钮 一体化
    auto* pwd_container = new QFrame(card);
    pwd_container->setFixedHeight(40);
    pwd_container->setProperty("cssClass", QStringLiteral("inputField"));
    auto* pwd_layout = new QHBoxLayout(pwd_container);
    pwd_layout->setContentsMargins(0, 0, 0, 0);
    pwd_layout->setSpacing(0);

    // lock 图标（输入框左侧，绝对位于容器内）
    auto* lock_icon = new QLabel(pwd_container);
    lock_icon->setPixmap(
        tinted_pixmap(QStringLiteral(":/icons/lock.svg"), IconRole::Normal, QSize(18, 18)));
    lock_icon->setProperty("cssClass", QStringLiteral("inlineIcon"));
    lock_icon->setFixedSize(36, 40);
    lock_icon->setAlignment(Qt::AlignCenter);
    pwd_layout->addWidget(lock_icon);

    // 输入框（透明背景，无边框，融入容器）
    password_edit_ = new QLineEdit(pwd_container);
    password_edit_->setEchoMode(QLineEdit::Password);
    password_edit_->setPlaceholderText(QStringLiteral("程序密码"));
    password_edit_->setProperty("cssClass", QStringLiteral("inlineEdit"));
    pwd_layout->addWidget(password_edit_, 1);

    // 可见性切换按钮（输入框右侧）
    visibility_btn_ = new QPushButton(pwd_container);
    visibility_btn_->setIcon(tinted_icon(QStringLiteral(":/icons/eye.svg"), IconRole::Normal));
    visibility_btn_->setIconSize(QSize(18, 18));
    visibility_btn_->setCursor(Qt::PointingHandCursor);
    visibility_btn_->setFixedSize(36, 40);
    visibility_btn_->setProperty("cssClass", QStringLiteral("inlineBtn"));
    pwd_layout->addWidget(visibility_btn_);

    card_layout->addWidget(pwd_container);
    card_layout->addSpacing(12);

    // 提示行：盾牌图标 + 提示文字 + 剩余尝试次数
    auto* hint_row = new QHBoxLayout();
    hint_row->setSpacing(6);

    auto* hint_icon = new QLabel(card);
    hint_icon->setPixmap(
        tinted_pixmap(QStringLiteral(":/icons/shield-check.svg"), IconRole::Normal, QSize(14, 14)));
    hint_icon->setProperty("cssClass", QStringLiteral("inlineIcon"));
    hint_row->addWidget(hint_icon);

    auto* hint_text = new QLabel(
        QStringLiteral("连续 5 次失败将锁定 5 分钟"), card);
    hint_text->setProperty("cssClass", QStringLiteral("caption"));
    hint_row->addWidget(hint_text);
    hint_row->addStretch(1);

    attempts_label_ = new QLabel(
        QStringLiteral("剩余尝试 5/5"), card);
    attempts_label_->setProperty("cssClass", QStringLiteral("caption"));
    attempts_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    hint_row->addWidget(attempts_label_);
    card_layout->addLayout(hint_row);

    // 解锁按钮
    submit_button_ = new QPushButton(card);
    submit_button_->setIcon(tinted_icon(QStringLiteral(":/icons/lock-open.svg"), IconRole::OnPrimary));
    submit_button_->setIconSize(QSize(18, 18));
    submit_button_->setText(QStringLiteral("解锁"));
    submit_button_->setCursor(Qt::PointingHandCursor);
    submit_button_->setFixedHeight(40);
    submit_button_->setProperty("cssClass", QStringLiteral("primary"));
    card_layout->addSpacing(20);
    card_layout->addWidget(submit_button_);

    // 错误提示
    error_label_ = new QLabel(card);
    error_label_->setWordWrap(true);
    error_label_->setProperty("cssClass", QStringLiteral("error"));
    error_label_->setAlignment(Qt::AlignCenter);
    card_layout->addWidget(error_label_);

    // 底部分隔线 + 加密信息
    card_layout->addSpacing(20);
    auto* divider = new QFrame(card);
    divider->setFixedHeight(1);
    divider->setProperty("cssClass", QStringLiteral("divider"));
    card_layout->addWidget(divider);

    auto* footer_row = new QHBoxLayout();
    footer_row->setSpacing(6);
    footer_row->setContentsMargins(0, 12, 0, 0);
    auto* footer_icon = new QLabel(card);
    footer_icon->setPixmap(
        tinted_pixmap(QStringLiteral(":/icons/shield.svg"), IconRole::Normal, QSize(12, 12)));
    footer_icon->setProperty("cssClass", QStringLiteral("inlineIcon"));
    footer_row->addWidget(footer_icon);

    auto* footer_text = new QLabel(
        QStringLiteral("本地加密 · AES-256-GCM · Argon2id"), card);
    footer_text->setProperty("cssClass", QStringLiteral("caption"));
    footer_row->addWidget(footer_text);
    footer_row->addStretch(1);
    card_layout->addLayout(footer_row);

    overlay_layout->addWidget(card);
    root_layout->addWidget(overlay);

    // 信号槽
    connect(visibility_btn_, &QPushButton::clicked,
            this, &UnlockView::on_visibility_toggled);
    connect(submit_button_, &QPushButton::clicked,
            this, &UnlockView::on_submit_clicked);
    connect(password_edit_, &QLineEdit::returnPressed,
            this, &UnlockView::on_submit_clicked);
    connect(password_edit_, &QLineEdit::textChanged,
            this, &UnlockView::on_password_changed);

    password_edit_->setFocus();
}

void UnlockView::closeEvent(QCloseEvent* event) {
    if (!unlock_succeeded_) {
        emit rejected();
    }
    QWidget::closeEvent(event);
}

void UnlockView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && !unlock_succeeded_) {
        emit rejected();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

// ---------------------------------------------------------------------------
// 槽函数
// ---------------------------------------------------------------------------

void UnlockView::on_visibility_toggled() {
    password_visible_ = !password_visible_;
    password_edit_->setEchoMode(
        password_visible_ ? QLineEdit::Normal : QLineEdit::Password);
    visibility_btn_->setIcon(tinted_icon(password_visible_
        ? QStringLiteral(":/icons/eye-off.svg")
        : QStringLiteral(":/icons/eye.svg"), IconRole::Normal));
}

void UnlockView::on_password_changed(const QString& text) {
    (void)text;
    // 清空错误提示
    if (error_label_) error_label_->setText(QString());
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
        update_attempts_display();

        QString msg;
        if (result.ok()) {
            msg = QString::fromStdString(result.value().error_message);
        } else {
            msg = QString::fromStdString(result.error().what());
        }
        set_error(msg.isEmpty()
                      ? QStringLiteral("解锁失败，请检查程序密码。")
                      : QStringLiteral("解锁失败：%1").arg(msg));

        if (remaining_attempts_ <= 0) {
            if (submit_button_) submit_button_->setEnabled(false);
            set_error(QStringLiteral("尝试次数已用尽，请稍后再试。"));
        }

        password_edit_->clear();
        password_edit_->setFocus();
    }
}

// ---------------------------------------------------------------------------
// 辅助
// ---------------------------------------------------------------------------

void UnlockView::set_error(const QString& message) {
    if (error_label_) error_label_->setText(message);
}

void UnlockView::update_attempts_display() {
    if (!attempts_label_) return;
    if (remaining_attempts_ > 0) {
        attempts_label_->setText(
            QStringLiteral("剩余尝试 %1/5").arg(remaining_attempts_));
        attempts_label_->setProperty("cssClass", QStringLiteral("caption"));
    } else {
        attempts_label_->setText(QStringLiteral("已锁定"));
        attempts_label_->setProperty("cssClass", QStringLiteral("error"));
    }
    // 切换 dynamic property 后必须 unpolish + polish 才能让 QSS 重新生效
    attempts_label_->style()->unpolish(attempts_label_);
    attempts_label_->style()->polish(attempts_label_);
}

}  // namespace pwdvault::ui
