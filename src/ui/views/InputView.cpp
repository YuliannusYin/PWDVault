// coding: utf-8
// =============================================================================
// InputView.cpp
//
// PwdVault 录入视图实现（新设计）。640px 居中卡片 + 图标前缀输入框 + 强度条。
// =============================================================================
#include "InputView.h"
#include "IconKit.h"
#include "IpcClient.h"
#include "StrengthUtil.h"
#include "TagInputWidget.h"
#include "Theme.h"

#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QTimer>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QString>
#include <QStyle>
#include <QVBoxLayout>
#include <QWidget>

#include <string>

namespace pwdvault::ui {

namespace {

// 强度等级判定与文案统一通过 StrengthUtil 提供（按 core::StrengthLevel 输入）。

/// 创建图标前缀的输入框容器：icon (36px) + QLineEdit + 右侧可选按钮区。
/// 容器自身有 border，输入框透明融入。
QFrame* make_icon_input_field(QWidget* parent, const QString& icon_path,
                               QLineEdit*& out_edit,
                               QPushButton* right_btn1 = nullptr,
                               QPushButton* right_btn2 = nullptr) {
    auto* container = new QFrame(parent);
    container->setFixedHeight(40);
    container->setProperty("cssClass", QStringLiteral("inputField"));
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* icon = new QLabel(container);
    icon->setPixmap(tinted_pixmap(icon_path, IconRole::Normal, QSize(18, 18)));
    icon->setProperty("cssClass", QStringLiteral("inlineIcon"));
    icon->setFixedSize(36, 40);
    icon->setAlignment(Qt::AlignCenter);
    layout->addWidget(icon);

    out_edit = new QLineEdit(container);
    out_edit->setProperty("cssClass", QStringLiteral("inlineEdit"));
    layout->addWidget(out_edit, 1);

    if (right_btn1) {
        layout->addWidget(right_btn1);
    }
    if (right_btn2) {
        layout->addWidget(right_btn2);
    }
    return container;
}

}  // namespace

InputView::InputView(IpcClient* client, QWidget* parent)
    : QWidget(parent), client_(client)
{
    build_ui();
    // 主题切换后重新应用 primary 按钮内联样式
    if (Theme* t = Theme::instance()) {
        connect(t, &Theme::theme_changed, this, [this]() {
            apply_primary_button_style(save_button_);
        });
    }
}

InputView::~InputView() = default;

void InputView::build_ui() {
    // 外层用滚动区域，确保小屏幕下表单可滚动
    auto* outer_layout = new QVBoxLayout(this);
    outer_layout->setContentsMargins(0, 0, 0, 0);
    outer_layout->setSpacing(0);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(
        QStringLiteral("QScrollArea { background-color: transparent; border: none; }"));

    auto* scroll_content = new QWidget(scroll);
    // 不设 setStyleSheet("background: transparent;")：widget 级样式表优先级
    // 高于 qss 文件，无选择器的 "background: transparent" 会级联到所有子 widget，
    // 覆盖 card 的 background-color。content 继承全局 QWidget 底色即可。
    auto* scroll_layout = new QVBoxLayout(scroll_content);
    scroll_layout->setContentsMargins(24, 16, 24, 16);
    scroll_layout->setSpacing(0);
    scroll_layout->addStretch(1);  // 顶部弹性，让卡片垂直居中

    // ── 640px 居中卡片 ──
    auto* card = new QFrame(scroll_content);
    card->setFixedWidth(640);
    card->setObjectName(QStringLiteral("formCard"));
    card->setProperty("cssClass", QStringLiteral("card"));
    auto* card_layout = new QVBoxLayout(card);
    card_layout->setContentsMargins(32, 32, 32, 32);
    card_layout->setSpacing(0);

    // 标题
    auto* title = new QLabel(QStringLiteral("新增密码条目"), card);
    title->setProperty("cssClass", QStringLiteral("sectionTitle"));
    card_layout->addWidget(title);

    auto* subtitle = new QLabel(
        QStringLiteral("所有字段加密存储于本地"), card);
    subtitle->setProperty("cssClass", QStringLiteral("muted"));
    card_layout->addWidget(subtitle);

    card_layout->addSpacing(24);

    // ── *条目名（必填） ──
    auto* entry_name_label = new QLabel(
        QStringLiteral("条目名 <span style=\"color:#f56363;\">*</span>"), card);
    entry_name_label->setProperty("cssClass", QStringLiteral("fieldLabel"));
    entry_name_label->setTextFormat(Qt::RichText);
    card_layout->addWidget(entry_name_label);
    card_layout->addSpacing(6);
    auto* entry_name_field = make_icon_input_field(
        card, QStringLiteral(":/icons/database.svg"), entry_name_edit_);
    card_layout->addWidget(entry_name_field);
    entry_name_edit_->setPlaceholderText(QStringLiteral("如：GitHub、公司邮箱"));

    card_layout->addSpacing(16);

    // ── 用户名（可选） ──
    auto* username_label = new QLabel(QStringLiteral("用户名"), card);
    username_label->setProperty("cssClass", QStringLiteral("fieldLabel"));
    card_layout->addWidget(username_label);
    card_layout->addSpacing(6);
    auto* username_field = make_icon_input_field(
        card, QStringLiteral(":/icons/user.svg"), username_edit_);
    card_layout->addWidget(username_field);
    username_edit_->setPlaceholderText(QStringLiteral("显示名，如：张三"));

    card_layout->addSpacing(16);

    // ── *账号（必填） ──
    auto* account_label = new QLabel(
        QStringLiteral("账号 <span style=\"color:#f56363;\">*</span>"), card);
    account_label->setProperty("cssClass", QStringLiteral("fieldLabel"));
    account_label->setTextFormat(Qt::RichText);
    card_layout->addWidget(account_label);
    card_layout->addSpacing(6);
    auto* account_field = make_icon_input_field(
        card, QStringLiteral(":/icons/at-sign.svg"), account_edit_);
    card_layout->addWidget(account_field);
    account_edit_->setPlaceholderText(QStringLiteral("登录账号或邮箱"));

    card_layout->addSpacing(16);

    // ── *密码（必填，带生成 + 可见性按钮） ──
    auto* pwd_label = new QLabel(
        QStringLiteral("密码 <span style=\"color:#f56363;\">*</span>"), card);
    pwd_label->setProperty("cssClass", QStringLiteral("fieldLabel"));
    pwd_label->setTextFormat(Qt::RichText);
    card_layout->addWidget(pwd_label);
    card_layout->addSpacing(6);

    // 生成按钮（小尺寸，融入输入框右侧）
    generate_button_ = new QPushButton(card);
    generate_button_->setIcon(tinted_icon(QStringLiteral(":/icons/wand-2.svg"), IconRole::Normal));
    generate_button_->setIconSize(QSize(14, 14));
    generate_button_->setText(QStringLiteral("生成"));
    generate_button_->setCursor(Qt::PointingHandCursor);
    generate_button_->setFixedHeight(40);
    generate_button_->setProperty("cssClass", QStringLiteral("inlineTextBtn"));

    // 可见性按钮
    visibility_btn_ = new QPushButton(card);
    visibility_btn_->setIcon(tinted_icon(QStringLiteral(":/icons/eye.svg"), IconRole::Normal));
    visibility_btn_->setIconSize(QSize(16, 16));
    visibility_btn_->setCursor(Qt::PointingHandCursor);
    visibility_btn_->setFixedSize(36, 40);
    visibility_btn_->setProperty("cssClass", QStringLiteral("inlineBtn"));

    auto* pwd_field = make_icon_input_field(
        card, QStringLiteral(":/icons/key.svg"), password_edit_,
        generate_button_, visibility_btn_);
    password_edit_->setEchoMode(QLineEdit::Password);
    password_edit_->setProperty("cssClass", QStringLiteral("inlineEdit"));
    card_layout->addWidget(pwd_field);

    // 强度条
    auto* strength_row = new QHBoxLayout();
    strength_row->setContentsMargins(0, 8, 0, 0);
    strength_row->setSpacing(8);
    strength_bar_ = new QProgressBar(card);
    strength_bar_->setRange(0, 100);
    strength_bar_->setValue(0);
    strength_bar_->setTextVisible(false);
    strength_bar_->setFixedHeight(4);
    strength_bar_->setProperty("strength", QStringLiteral("weak"));
    strength_row->addWidget(strength_bar_, 1);
    strength_label_ = new QLabel(QStringLiteral("强度：-"), card);
    strength_label_->setProperty("cssClass", QStringLiteral("caption"));
    strength_row->addWidget(strength_label_);
    card_layout->addLayout(strength_row);

    card_layout->addSpacing(16);

    // ── 网站（可选） ──
    auto* website_label = new QLabel(QStringLiteral("网站"), card);
    website_label->setProperty("cssClass", QStringLiteral("fieldLabel"));
    card_layout->addWidget(website_label);
    card_layout->addSpacing(6);
    auto* website_field = make_icon_input_field(
        card, QStringLiteral(":/icons/globe.svg"), website_edit_);
    card_layout->addWidget(website_field);
    website_edit_->setPlaceholderText(QStringLiteral("example.com"));

    card_layout->addSpacing(16);

    // ── 标签（可选，芯片流式输入） ──
    auto* tag_label = new QLabel(QStringLiteral("标签"), card);
    tag_label->setProperty("cssClass", QStringLiteral("fieldLabel"));
    card_layout->addWidget(tag_label);
    card_layout->addSpacing(6);
    // 标签输入容器：左侧 tag 图标 + TagInputWidget（融入 inputField 一体化边框）
    auto* tag_field = new QFrame(card);
    tag_field->setProperty("cssClass", QStringLiteral("inputField"));
    auto* tag_field_layout = new QHBoxLayout(tag_field);
    tag_field_layout->setContentsMargins(0, 0, 0, 0);
    tag_field_layout->setSpacing(0);

    auto* tag_icon = new QLabel(tag_field);
    tag_icon->setPixmap(tinted_pixmap(QStringLiteral(":/icons/tag.svg"), IconRole::Normal, QSize(18, 18)));
    tag_icon->setProperty("cssClass", QStringLiteral("inlineIcon"));
    tag_icon->setFixedSize(36, 40);
    tag_icon->setAlignment(Qt::AlignCenter);
    tag_field_layout->addWidget(tag_icon, 0, Qt::AlignVCenter);

    tag_input_ = new TagInputWidget(tag_field);
    tag_input_->setProperty("cssClass", QStringLiteral("tagInput"));
    tag_input_->setMinimumHeight(40);
    tag_field_layout->addWidget(tag_input_, 1);
    card_layout->addWidget(tag_field);

    card_layout->addSpacing(16);

    // ── 备注（可选，markdown 源码） ──
    auto* note_label = new QLabel(QStringLiteral("备注（markdown）"), card);
    note_label->setProperty("cssClass", QStringLiteral("fieldLabel"));
    card_layout->addWidget(note_label);
    card_layout->addSpacing(6);
    note_edit_ = new QPlainTextEdit(card);
    note_edit_->setPlaceholderText(
        QStringLiteral("可选：支持 markdown 语法（# 标题、**粗体**、`代码`、- 列表）"));
    note_edit_->setFixedHeight(96);
    card_layout->addWidget(note_edit_);

    // 错误提示
    card_layout->addSpacing(12);
    error_label_ = new QLabel(card);
    error_label_->setWordWrap(true);
    error_label_->setProperty("cssClass", QStringLiteral("error"));
    error_label_->setVisible(false);
    card_layout->addWidget(error_label_);

    // 底部分隔线 + 操作按钮
    card_layout->addSpacing(20);
    auto* divider = new QFrame(card);
    divider->setFixedHeight(1);
    divider->setProperty("cssClass", QStringLiteral("divider"));
    card_layout->addWidget(divider);
    card_layout->addSpacing(16);

    auto* footer_row = new QHBoxLayout();
    footer_row->setContentsMargins(0, 0, 0, 0);
    footer_row->setSpacing(12);
    footer_row->addStretch(1);

    clear_button_ = new QPushButton(card);
    clear_button_->setIcon(tinted_icon(QStringLiteral(":/icons/eraser.svg"), IconRole::Normal));
    clear_button_->setIconSize(QSize(16, 16));
    clear_button_->setText(QStringLiteral("清除"));
    clear_button_->setCursor(Qt::PointingHandCursor);
    clear_button_->setFixedHeight(40);
    clear_button_->setProperty("cssClass", QStringLiteral("outline"));
    footer_row->addWidget(clear_button_);

    save_button_ = new QPushButton(card);
    save_button_->setIcon(tinted_icon(QStringLiteral(":/icons/save.svg"), IconRole::OnPrimary));
    save_button_->setIconSize(QSize(16, 16));
    save_button_->setText(QStringLiteral("保存条目"));
    save_button_->setCursor(Qt::PointingHandCursor);
    save_button_->setFixedHeight(40);
    apply_primary_button_style(save_button_);
    footer_row->addWidget(save_button_);
    card_layout->addLayout(footer_row);

    scroll_layout->addWidget(card, 0, Qt::AlignCenter);
    scroll_layout->addStretch(1);  // 底部弹性

    scroll->setWidget(scroll_content);
    outer_layout->addWidget(scroll);

    // 信号槽
    connect(generate_button_, &QPushButton::clicked,
            this, &InputView::on_generate_clicked);
    connect(visibility_btn_, &QPushButton::clicked,
            this, &InputView::on_toggle_password_clicked);
    connect(password_edit_, &QLineEdit::textChanged,
            this, &InputView::on_password_changed);
    connect(save_button_, &QPushButton::clicked,
            this, &InputView::on_save_clicked);
    connect(clear_button_, &QPushButton::clicked,
            this, &InputView::on_clear_clicked);

    // 强度评估 debounce：每次输入触发 IPC 会卡顿，用 300ms 计时器合并连续输入。
    strength_timer_ = new QTimer(this);
    strength_timer_->setSingleShot(true);
    connect(strength_timer_, &QTimer::timeout, this, [this]() {
        if (password_edit_) update_strength(password_edit_->text());
    });

    // 加载已有标签供补全
    refresh_existing_tags();
}

void InputView::set_password(const QString& password) {
    if (password_edit_) password_edit_->setText(password);
}

void InputView::focus_first_field() {
    if (entry_name_edit_) entry_name_edit_->setFocus();
}

void InputView::on_generate_clicked() {
    // 通知 MainWindow 切换到生成器视图；生成后由 MainWindow 调用 set_password 回填
    emit password_generator_requested();
}

void InputView::on_toggle_password_clicked() {
    password_visible_ = !password_visible_;
    password_edit_->setEchoMode(
        password_visible_ ? QLineEdit::Normal : QLineEdit::Password);
    visibility_btn_->setIcon(tinted_icon(password_visible_
        ? QStringLiteral(":/icons/eye-off.svg")
        : QStringLiteral(":/icons/eye.svg"), IconRole::Normal));
}

void InputView::on_password_changed(const QString& text) {
    (void)text;
    // debounce：重启计时器，300ms 内无新输入才真正发起强度评估 IPC
    if (strength_timer_) strength_timer_->start(300);
}

void InputView::update_strength(const QString& password) {
    if (!strength_bar_ || !strength_label_) return;

    if (password.isEmpty() || !client_) {
        strength_bar_->setValue(0);
        strength_bar_->setProperty("strength", QStringLiteral("weak"));
        strength_bar_->style()->unpolish(strength_bar_);
        strength_bar_->style()->polish(strength_bar_);
        strength_label_->setText(QStringLiteral("强度：-"));
        strength_label_->setProperty("cssClass", QStringLiteral("caption"));
        strength_label_->style()->unpolish(strength_label_);
        strength_label_->style()->polish(strength_label_);
        return;
    }

    auto result = client_->estimate_strength(password.toStdString());
    core::StrengthEstimate estimate;
    if (result.ok()) {
        estimate = result.value().estimate;
    }

    const int pct = (estimate.bits >= 128) ? 100 : (estimate.bits * 100 / 128);
    strength_bar_->setValue(pct);

    // 通过 dynamic property 让 QSS 接管 chunk 颜色（深浅主题适配）
    const QString strength_key = strength_qss_key(estimate.level);
    const QString label_class = strength_label_class(estimate.level);
    strength_bar_->setProperty("strength", strength_key);
    strength_bar_->style()->unpolish(strength_bar_);
    strength_bar_->style()->polish(strength_bar_);

    strength_label_->setProperty("cssClass", label_class);
    strength_label_->style()->unpolish(strength_label_);
    strength_label_->style()->polish(strength_label_);
    strength_label_->setText(
        QStringLiteral("强度：%1（%2 bit）").arg(strength_text(estimate.level)).arg(estimate.bits));
}

void InputView::on_save_clicked() {
    set_error(QString());

    if (!client_) {
        set_error(QStringLiteral("内部错误：IPC 客户端不可用。"));
        return;
    }

    const QString entry_name = entry_name_edit_->text().trimmed();
    const QString account = account_edit_->text().trimmed();
    const QString password = password_edit_->text();

    // 必填校验：entry_name / account / password
    if (entry_name.isEmpty()) {
        set_error(QStringLiteral("条目名不能为空。"));
        entry_name_edit_->setFocus();
        return;
    }
    if (account.isEmpty()) {
        set_error(QStringLiteral("账号不能为空。"));
        account_edit_->setFocus();
        return;
    }
    if (password.isEmpty()) {
        set_error(QStringLiteral("密码不能为空。"));
        password_edit_->setFocus();
        return;
    }

    core::PasswordEntry entry;
    entry.id = 0;
    entry.entry_name = entry_name.toStdString();
    entry.account = account.toStdString();
    entry.username = username_edit_->text().trimmed().toStdString();
    entry.password = password.toStdString();
    entry.website = website_edit_->text().trimmed().toStdString();
    entry.note = note_edit_->toPlainText().toStdString();
    if (tag_input_) {
        entry.tags = tag_input_->selected_tags();
    }

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
    entry_name_edit_->clear();
    account_edit_->clear();
    username_edit_->clear();
    password_edit_->clear();
    website_edit_->clear();
    if (tag_input_) tag_input_->set_selected_tags({});
    note_edit_->clear();
    if (password_visible_) {
        on_toggle_password_clicked();
    }
    set_error(QString());
    entry_name_edit_->setFocus();
}

void InputView::set_error(const QString& message) {
    if (!error_label_) return;
    error_label_->setText(message);
    error_label_->setVisible(!message.isEmpty());
}

void InputView::refresh_existing_tags() {
    if (!client_ || !tag_input_) return;
    auto result = client_->list_tags();
    if (result.ok()) {
        tag_input_->set_existing_tags(result.value().tags);
    }
}

}  // namespace pwdvault::ui
