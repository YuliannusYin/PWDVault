// coding: utf-8
// =============================================================================
// EditEntryDialog.cpp
//
// PwdVault 编辑条目对话框实现（新设计）。
// 模态遮罩 + 560px 居中卡片 + 头部/表单/尾部三段式布局。
// =============================================================================
#include "EditEntryDialog.h"
#include "IpcClient.h"
#include "IconKit.h"
#include "StrengthUtil.h"

#include <QCloseEvent>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QString>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace pwdvault::ui {

namespace {

QString format_time(int64_t ts) {
    if (ts <= 0) return QStringLiteral("-");
    return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(ts))
        .toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

// 强度等级判定与文案统一通过 StrengthUtil 提供（按 core::StrengthLevel 输入）。

}  // namespace

EditEntryDialog::EditEntryDialog(IpcClient* client, const core::PasswordEntry& entry,
                                 QWidget* parent)
    : QDialog(parent), client_(client), entry_(entry)
{
    // 模态 + 无原生标题栏，自定义遮罩 + 卡片
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setWindowModality(Qt::ApplicationModal);
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowTitle(QStringLiteral("编辑密码条目"));

    // 自动覆盖父窗口大小
    // parent 通常是 PasswordBookView（MainWindow 的子 widget，非 top-level），
    // 其 geometry() 返回的是相对于父 widget 的坐标，不能直接用于 top-level QDialog。
    // 用 window()->geometry() 拿到顶层主窗口的屏幕坐标。
    if (parent) {
        setGeometry(parent->window()->geometry());
    }

    build_ui();
}

EditEntryDialog::~EditEntryDialog() = default;

void EditEntryDialog::build_ui() {
    // 根布局：半透明遮罩
    auto* root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(0, 0, 0, 0);
    root_layout->setSpacing(0);

    auto* overlay = new QFrame(this);
    overlay->setProperty("cssClass", QStringLiteral("modalOverlay"));
    auto* overlay_layout = new QVBoxLayout(overlay);
    overlay_layout->setContentsMargins(16, 16, 16, 16);
    overlay_layout->setAlignment(Qt::AlignCenter);

    // ── 560px 卡片 ──
    auto* card = new QFrame(overlay);
    card->setFixedWidth(560);
    card->setProperty("cssClass", QStringLiteral("modal"));
    auto* card_layout = new QVBoxLayout(card);
    card_layout->setContentsMargins(0, 0, 0, 0);
    card_layout->setSpacing(0);

    // ── 头部（56px 高） ──
    auto* header = new QFrame(card);
    header->setFixedHeight(56);
    header->setProperty("cssClass", QStringLiteral("modalHeader"));
    auto* header_layout = new QHBoxLayout(header);
    header_layout->setContentsMargins(24, 0, 16, 0);
    header_layout->setSpacing(10);

    auto* pencil_icon = new QLabel(header);
    pencil_icon->setPixmap(tinted_pixmap(QStringLiteral(":/icons/pencil.svg"), IconRole::Normal, QSize(18, 18)));
    pencil_icon->setProperty("cssClass", QStringLiteral("inlineIcon"));
    header_layout->addWidget(pencil_icon);

    auto* title = new QLabel(QStringLiteral("编辑密码条目"), header);
    title->setProperty("cssClass", QStringLiteral("sectionTitle"));
    header_layout->addWidget(title);
    header_layout->addStretch(1);

    auto* close_btn = new QPushButton(header);
    close_btn->setIcon(tinted_icon(QStringLiteral(":/icons/x.svg"), IconRole::Normal));
    close_btn->setIconSize(QSize(18, 18));
    close_btn->setCursor(Qt::PointingHandCursor);
    close_btn->setFixedSize(36, 36);
    close_btn->setProperty("cssClass", QStringLiteral("icon"));
    header_layout->addWidget(close_btn);
    card_layout->addWidget(header);

    // ── 体（表单） ──
    auto* body = new QFrame(card);
    body->setProperty("cssClass", QStringLiteral("modalBody"));
    auto* body_layout = new QVBoxLayout(body);
    body_layout->setContentsMargins(24, 20, 24, 20);
    body_layout->setSpacing(16);

    // 字段标签样式
    auto make_label = [body](const QString& text) {
        auto* lbl = new QLabel(text, body);
        lbl->setProperty("cssClass", QStringLiteral("fieldLabel"));
        return lbl;
    };

    // 普通输入框（inputField 容器 + 图标 + 透明 QLineEdit，与密码框风格统一）
    auto make_line_edit = [body](QLineEdit*& edit, const QString& icon_path) {
        auto* container = new QFrame(body);
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
        edit = new QLineEdit(container);
        edit->setProperty("cssClass", QStringLiteral("inlineEdit"));
        layout->addWidget(edit, 1);
        return container;
    };

    // 网站
    body_layout->addWidget(make_label(QStringLiteral("网站")));
    make_line_edit(website_edit_, QStringLiteral(":/icons/globe.svg"));
    website_edit_->setText(QString::fromStdString(entry_.website));
    body_layout->addWidget(website_edit_->parentWidget());

    // 用户名
    body_layout->addWidget(make_label(QStringLiteral("用户名")));
    make_line_edit(username_edit_, QStringLiteral(":/icons/at-sign.svg"));
    username_edit_->setText(QString::fromStdString(entry_.username));
    body_layout->addWidget(username_edit_->parentWidget());

    // 密码（带可见性 + 生成）
    body_layout->addWidget(make_label(QStringLiteral("密码")));
    auto* pwd_container = new QFrame(body);
    pwd_container->setFixedHeight(40);
    pwd_container->setProperty("cssClass", QStringLiteral("inputField"));
    auto* pwd_layout = new QHBoxLayout(pwd_container);
    pwd_layout->setContentsMargins(0, 0, 0, 0);
    pwd_layout->setSpacing(0);

    auto* key_icon = new QLabel(pwd_container);
    key_icon->setPixmap(tinted_pixmap(QStringLiteral(":/icons/key-round.svg"), IconRole::Normal, QSize(16, 16)));
    key_icon->setProperty("cssClass", QStringLiteral("inlineIcon"));
    key_icon->setFixedSize(36, 40);
    key_icon->setAlignment(Qt::AlignCenter);
    pwd_layout->addWidget(key_icon);

    password_edit_ = new QLineEdit(pwd_container);
    password_edit_->setEchoMode(QLineEdit::Password);
    password_edit_->setText(QString::fromStdString(entry_.password));
    password_edit_->setProperty("cssClass", QStringLiteral("inlineEdit"));
    pwd_layout->addWidget(password_edit_, 1);

    // 生成按钮
    generate_btn_ = new QPushButton(pwd_container);
    generate_btn_->setIcon(tinted_icon(QStringLiteral(":/icons/wand-2.svg"), IconRole::Normal));
    generate_btn_->setIconSize(QSize(16, 16));
    generate_btn_->setCursor(Qt::PointingHandCursor);
    generate_btn_->setFixedSize(40, 40);
    generate_btn_->setToolTip(QStringLiteral("生成密码"));
    generate_btn_->setProperty("cssClass", QStringLiteral("inlineBtn"));
    pwd_layout->addWidget(generate_btn_);

    // 可见性按钮
    visibility_btn_ = new QPushButton(pwd_container);
    visibility_btn_->setIcon(tinted_icon(QStringLiteral(":/icons/eye.svg"), IconRole::Normal));
    visibility_btn_->setIconSize(QSize(16, 16));
    visibility_btn_->setCursor(Qt::PointingHandCursor);
    visibility_btn_->setFixedSize(40, 40);
    visibility_btn_->setToolTip(QStringLiteral("显示/隐藏密码"));
    visibility_btn_->setProperty("cssClass", QStringLiteral("inlineBtn"));
    pwd_layout->addWidget(visibility_btn_);
    body_layout->addWidget(pwd_container);

    // 强度行
    auto* strength_row = new QHBoxLayout();
    strength_row->setContentsMargins(0, 4, 0, 0);
    strength_row->setSpacing(8);
    // 用 4 段 QFrame 模拟分段强度条
    // 简化：使用一个 QLabel 显示文字
    strength_label_ = new QLabel(body);
    strength_label_->setProperty("cssClass", QStringLiteral("caption"));
    strength_row->addWidget(strength_label_);
    strength_row->addStretch(1);
    body_layout->addLayout(strength_row);

    // 备注
    body_layout->addWidget(make_label(QStringLiteral("备注")));
    note_edit_ = new QPlainTextEdit(body);
    note_edit_->setPlainText(QString::fromStdString(entry_.note));
    note_edit_->setFixedHeight(96);
    body_layout->addWidget(note_edit_);

    // 更新时间行
    auto* updated_row = new QHBoxLayout();
    updated_row->setContentsMargins(0, 4, 0, 0);
    updated_row->setSpacing(6);
    auto* clock_icon = new QLabel(body);
    clock_icon->setPixmap(tinted_pixmap(QStringLiteral(":/icons/clock.svg"), IconRole::Normal, QSize(14, 14)));
    clock_icon->setProperty("cssClass", QStringLiteral("inlineIcon"));
    updated_row->addWidget(clock_icon);
    updated_label_ = new QLabel(
        QStringLiteral("更新于 %1").arg(format_time(entry_.updated_at)), body);
    updated_label_->setProperty("cssClass", QStringLiteral("caption"));
    updated_row->addWidget(updated_label_);
    updated_row->addStretch(1);
    body_layout->addLayout(updated_row);

    // 错误提示
    error_label_ = new QLabel(body);
    error_label_->setWordWrap(true);
    error_label_->setProperty("cssClass", QStringLiteral("error"));
    error_label_->setVisible(false);
    body_layout->addWidget(error_label_);

    card_layout->addWidget(body);

    // ── 尾部（64px 高） ──
    auto* footer = new QFrame(card);
    footer->setFixedHeight(64);
    footer->setProperty("cssClass", QStringLiteral("modalFooter"));
    auto* footer_layout = new QHBoxLayout(footer);
    footer_layout->setContentsMargins(24, 0, 24, 0);
    footer_layout->setSpacing(12);
    footer_layout->addStretch(1);

    cancel_button_ = new QPushButton(QStringLiteral("取消"), footer);
    cancel_button_->setCursor(Qt::PointingHandCursor);
    cancel_button_->setFixedHeight(40);
    cancel_button_->setProperty("cssClass", QStringLiteral("outline"));
    footer_layout->addWidget(cancel_button_);

    save_button_ = new QPushButton(footer);
    save_button_->setIcon(tinted_icon(QStringLiteral(":/icons/save.svg"), IconRole::OnPrimary));
    save_button_->setIconSize(QSize(16, 16));
    save_button_->setText(QStringLiteral("保存修改"));
    save_button_->setCursor(Qt::PointingHandCursor);
    save_button_->setFixedHeight(40);
    save_button_->setProperty("cssClass", QStringLiteral("primary"));
    footer_layout->addWidget(save_button_);
    card_layout->addWidget(footer);

    overlay_layout->addWidget(card);
    root_layout->addWidget(overlay);

    // 信号槽
    connect(close_btn, &QPushButton::clicked,
            this, &EditEntryDialog::on_cancel_clicked);
    connect(cancel_button_, &QPushButton::clicked,
            this, &EditEntryDialog::on_cancel_clicked);
    connect(save_button_, &QPushButton::clicked,
            this, &EditEntryDialog::on_save_clicked);
    connect(visibility_btn_, &QPushButton::clicked,
            this, &EditEntryDialog::on_toggle_password_clicked);
    connect(generate_btn_, &QPushButton::clicked,
            this, &EditEntryDialog::on_generate_clicked);
    connect(password_edit_, &QLineEdit::textChanged,
            this, &EditEntryDialog::on_password_changed);

    // 强度评估 debounce：每次输入触发 IPC 会卡顿，用 300ms 计时器合并连续输入。
    strength_timer_ = new QTimer(this);
    strength_timer_->setSingleShot(true);
    connect(strength_timer_, &QTimer::timeout, this, [this]() {
        if (password_edit_) update_strength(password_edit_->text());
    });

    // 初始强度
    update_strength(QString::fromStdString(entry_.password));
    website_edit_->setFocus();
}

void EditEntryDialog::closeEvent(QCloseEvent* event) {
    // 关闭 = 取消
    reject();
    QDialog::closeEvent(event);
}

void EditEntryDialog::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        on_cancel_clicked();
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

// ---------------------------------------------------------------------------
// 槽函数
// ---------------------------------------------------------------------------

void EditEntryDialog::on_toggle_password_clicked() {
    password_visible_ = !password_visible_;
    password_edit_->setEchoMode(
        password_visible_ ? QLineEdit::Normal : QLineEdit::Password);
    visibility_btn_->setIcon(tinted_icon(password_visible_
        ? QStringLiteral(":/icons/eye-off.svg")
        : QStringLiteral(":/icons/eye.svg"), IconRole::Normal));
}

void EditEntryDialog::on_generate_clicked() {
    if (!client_) return;
    // 使用默认参数生成
    core::PasswordGeneratorOptions opts;
    opts.length = 20;
    opts.use_uppercase = true;
    opts.use_lowercase = true;
    opts.use_digits = true;
    opts.use_symbols = true;
    auto r = client_->generate_password(opts);
    if (r.ok()) {
        password_edit_->setText(QString::fromStdString(r.value().password));
    } else {
        QMessageBox::warning(this, QStringLiteral("生成失败"),
            QStringLiteral("无法生成密码：%1")
                .arg(QString::fromStdString(r.error().what())));
    }
}

void EditEntryDialog::on_password_changed(const QString& text) {
    (void)text;
    // debounce：重启计时器，300ms 内无新输入才真正发起强度评估 IPC
    if (strength_timer_) strength_timer_->start(300);
}

void EditEntryDialog::on_save_clicked() {
    set_error(QString());

    if (!client_) {
        set_error(QStringLiteral("内部错误：IPC 客户端不可用。"));
        return;
    }

    const QString website = website_edit_->text().trimmed();
    const QString username = username_edit_->text().trimmed();
    const QString password = password_edit_->text();

    if (website.isEmpty()) {
        set_error(QStringLiteral("网站不能为空。"));
        website_edit_->setFocus();
        return;
    }
    if (username.isEmpty()) {
        set_error(QStringLiteral("用户名不能为空。"));
        username_edit_->setFocus();
        return;
    }
    if (password.isEmpty()) {
        set_error(QStringLiteral("密码不能为空。"));
        password_edit_->setFocus();
        return;
    }

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
        set_error(msg.isEmpty()
                      ? QStringLiteral("保存失败。")
                      : QStringLiteral("保存失败：%1").arg(msg));
    }
}

void EditEntryDialog::on_cancel_clicked() {
    reject();
}

// ---------------------------------------------------------------------------
// 辅助
// ---------------------------------------------------------------------------

void EditEntryDialog::update_strength(const QString& password) {
    if (!strength_label_ || !client_) return;
    if (password.isEmpty()) {
        strength_label_->setText(QStringLiteral("强度：-"));
        strength_label_->setProperty("cssClass", QStringLiteral("caption"));
        strength_label_->style()->unpolish(strength_label_);
        strength_label_->style()->polish(strength_label_);
        return;
    }
    auto r = client_->estimate_strength(password.toStdString());
    core::StrengthEstimate estimate;
    if (r.ok()) {
        estimate = r.value().estimate;
    }
    // 通过 cssClass 让 QSS 接管颜色（深浅主题适配）
    const QString label_class = strength_label_class(estimate.level);
    strength_label_->setProperty("cssClass", label_class);
    strength_label_->style()->unpolish(strength_label_);
    strength_label_->style()->polish(strength_label_);
    strength_label_->setText(
        QStringLiteral("强度：%1（%2 bit）").arg(strength_text(estimate.level)).arg(estimate.bits));
}

void EditEntryDialog::set_error(const QString& message) {
    if (!error_label_) return;
    error_label_->setText(message);
    error_label_->setVisible(!message.isEmpty());
}

}  // namespace pwdvault::ui
