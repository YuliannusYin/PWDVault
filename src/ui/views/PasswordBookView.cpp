// coding: utf-8
// =============================================================================
// PasswordBookView.cpp
//
// PwdVault 密码本视图实现（新设计）。master-detail 布局。
// =============================================================================
#include "PasswordBookView.h"
#include "EditEntryDialog.h"
#include "IpcClient.h"
#include "IconKit.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QString>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdint>
#include <string>
#include <vector>

namespace pwdvault::ui {

namespace {

/// Unix 时间戳（秒）→ 可读字符串。
QString format_time(int64_t ts) {
    if (ts <= 0) return QStringLiteral("-");
    return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(ts))
        .toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

/// 取网站名首字母（大写）作为头像字符。
QString avatar_letter(const std::string& website) {
    if (website.empty()) return QStringLiteral("?");
    // 跳过协议前缀
    std::string s = website;
    for (const char* prefix : {"https://", "http://", "www."}) {
        const size_t len = std::char_traits<char>::length(prefix);
        if (s.size() > len && s.compare(0, len, prefix) == 0) {
            s = s.substr(len);
        }
    }
    if (s.empty()) return QStringLiteral("?");
    QChar ch = QChar::fromLatin1(s[0]).toUpper();
    return ch;
}

/// 强度颜色（< 40 红、40-80 黄、> 80 绿）。
QString strength_text_for_bits(int bits) {
    if (bits < 40) return QStringLiteral("弱");
    if (bits < 80) return QStringLiteral("中");
    return QStringLiteral("强");
}

/// 强度对应的 badge cssClass。
QString strength_badge_class(int bits) {
    if (bits < 40) return QStringLiteral("badgeDanger");
    if (bits < 80) return QStringLiteral("badgeWarning");
    return QStringLiteral("badgeSuccess");
}

/// 36x36 图标按钮（无文字）。
QPushButton* make_icon_btn(const QString& svg_path, IconRole role,
                           const QString& tooltip, QWidget* parent) {
    auto* btn = new QPushButton(parent);
    btn->setIcon(tinted_icon(svg_path, role));
    btn->setIconSize(QSize(16, 16));
    btn->setCursor(Qt::PointingHandCursor);
    btn->setToolTip(tooltip);
    btn->setFixedSize(36, 36);
    btn->setProperty("cssClass", QStringLiteral("icon"));
    return btn;
}

/// 28x28 小图标按钮。
QPushButton* make_small_icon_btn(const QString& svg_path, IconRole role,
                                 const QString& tooltip, QWidget* parent) {
    auto* btn = new QPushButton(parent);
    btn->setIcon(tinted_icon(svg_path, role));
    btn->setIconSize(QSize(14, 14));
    btn->setCursor(Qt::PointingHandCursor);
    btn->setToolTip(tooltip);
    btn->setFixedSize(28, 28);
    btn->setProperty("cssClass", QStringLiteral("iconSm"));
    return btn;
}

}  // namespace

PasswordBookView::PasswordBookView(IpcClient* client, QWidget* parent)
    : QWidget(parent), client_(client)
{
    build_ui();
    set_detail_actions_enabled(false);
}

PasswordBookView::~PasswordBookView() = default;

void PasswordBookView::build_ui() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 16, 24, 16);
    layout->setSpacing(12);

    // ── 顶部搜索行 ──
    auto* search_row = new QHBoxLayout();
    search_row->setSpacing(10);

    // 搜索输入框容器（search 图标 + 输入框）
    auto* search_container = new QFrame(this);
    search_container->setFixedHeight(40);
    search_container->setProperty("cssClass", QStringLiteral("inputField"));
    auto* search_layout = new QHBoxLayout(search_container);
    search_layout->setContentsMargins(0, 0, 0, 0);
    search_layout->setSpacing(0);

    auto* search_icon = new QLabel(search_container);
    search_icon->setPixmap(tinted_pixmap(QStringLiteral(":/icons/search.svg"), IconRole::Normal, QSize(18, 18)));
    search_icon->setProperty("cssClass", QStringLiteral("inlineIcon"));
    search_icon->setFixedSize(36, 40);
    search_icon->setAlignment(Qt::AlignCenter);
    search_layout->addWidget(search_icon);

    search_edit_ = new QLineEdit(search_container);
    search_edit_->setPlaceholderText(QStringLiteral("搜索网站、用户名、备注…"));
    search_edit_->setProperty("cssClass", QStringLiteral("inlineEdit"));
    search_layout->addWidget(search_edit_, 1);
    search_row->addWidget(search_container, 1);

    // 字段下拉
    field_combo_ = new QComboBox(this);
    field_combo_->setFixedSize(140, 40);
    field_combo_->addItem(QStringLiteral("全部字段"), QStringLiteral("all"));
    field_combo_->addItem(QStringLiteral("网站"), QStringLiteral("website"));
    field_combo_->addItem(QStringLiteral("用户名"), QStringLiteral("username"));
    field_combo_->addItem(QStringLiteral("备注"), QStringLiteral("note"));
    search_row->addWidget(field_combo_);

    // 刷新按钮
    refresh_button_ = make_icon_btn(
        QStringLiteral(":/icons/refresh-cw.svg"),
        IconRole::Normal, QStringLiteral("刷新"), this);
    refresh_button_->setFixedSize(40, 40);
    search_row->addWidget(refresh_button_);

    layout->addLayout(search_row);

    // ── master-detail ──
    auto* md_layout = new QHBoxLayout();
    md_layout->setSpacing(12);

    // 左侧列表
    auto* list_card = new QFrame(this);
    list_card->setProperty("cssClass", QStringLiteral("card"));
    list_card->setFixedWidth(340);
    auto* list_layout = new QVBoxLayout(list_card);
    list_layout->setContentsMargins(0, 0, 0, 0);
    list_layout->setSpacing(0);

    list_ = new QListWidget(list_card);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setFocusPolicy(Qt::NoFocus);
    list_->setCursor(Qt::PointingHandCursor);
    list_layout->addWidget(list_);
    md_layout->addWidget(list_card, 0);

    // 右侧详情（卡片 + 滚动）
    auto* detail_card = new QFrame(this);
    detail_card->setProperty("cssClass", QStringLiteral("card"));
    auto* detail_card_layout = new QVBoxLayout(detail_card);
    detail_card_layout->setContentsMargins(0, 0, 0, 0);
    detail_card_layout->setSpacing(0);

    detail_scroll_ = new QScrollArea(detail_card);
    detail_scroll_->setWidgetResizable(true);
    detail_scroll_->setFrameShape(QFrame::NoFrame);
    detail_scroll_->setStyleSheet(
        QStringLiteral("QScrollArea { background-color: transparent; border: none; }"));

    detail_container_ = new QWidget(detail_scroll_);
    detail_container_->setStyleSheet(QStringLiteral("background-color: transparent;"));
    auto* detail_layout = new QVBoxLayout(detail_container_);
    detail_layout->setContentsMargins(28, 28, 28, 28);
    detail_layout->setSpacing(0);

    // 空状态提示
    empty_hint_ = new QLabel(detail_container_);
    empty_hint_->setAlignment(Qt::AlignCenter);
    empty_hint_->setProperty("cssClass", QStringLiteral("emptyHint"));
    empty_hint_->setText(QStringLiteral("从左侧选择一条记录查看详情"));
    detail_layout->addWidget(empty_hint_);
    detail_layout->addStretch(1);

    detail_scroll_->setWidget(detail_container_);
    detail_card_layout->addWidget(detail_scroll_);
    md_layout->addWidget(detail_card, 1);

    layout->addLayout(md_layout, 1);

    // ── 详情头部（默认隐藏，load_detail 时显示） ──
    // 这里预先创建所有详情控件，但默认不显示。load_detail 时填入数据并显示。
    // 使用一个 header_frame 包裹头部和字段网格，作为整体显示/隐藏。
    // 为简化实现，把详情控件直接放在 detail_layout 顶部，通过 setVisible 控制可见性。

    // 头部 row
    auto* header_row = new QHBoxLayout();
    header_row->setSpacing(16);

    detail_avatar_ = new QLabel(detail_container_);
    detail_avatar_->setFixedSize(56, 56);
    detail_avatar_->setAlignment(Qt::AlignCenter);
    detail_avatar_->setProperty("cssClass", QStringLiteral("avatarLg"));
    header_row->addWidget(detail_avatar_);

    auto* title_col = new QVBoxLayout();
    title_col->setSpacing(2);
    detail_title_ = new QLabel(detail_container_);
    detail_title_->setProperty("cssClass", QStringLiteral("titleLg"));
    title_col->addWidget(detail_title_);
    detail_subtitle_ = new QLabel(detail_container_);
    detail_subtitle_->setProperty("cssClass", QStringLiteral("muted"));
    title_col->addWidget(detail_subtitle_);
    title_col->addStretch(1);
    header_row->addLayout(title_col, 1);

    // 头部右侧操作按钮
    copy_user_btn_ = make_icon_btn(
        QStringLiteral(":/icons/copy.svg"),
        IconRole::Normal, QStringLiteral("复制用户名"), detail_container_);
    header_row->addWidget(copy_user_btn_);

    copy_pwd_btn_ = make_icon_btn(
        QStringLiteral(":/icons/copy.svg"),
        IconRole::Normal, QStringLiteral("复制密码"), detail_container_);
    header_row->addWidget(copy_pwd_btn_);

    edit_btn_ = new QPushButton(detail_container_);
    edit_btn_->setIcon(tinted_icon(QStringLiteral(":/icons/pencil.svg"), IconRole::OnPrimary));
    edit_btn_->setIconSize(QSize(14, 14));
    edit_btn_->setText(QStringLiteral("编辑"));
    edit_btn_->setCursor(Qt::PointingHandCursor);
    edit_btn_->setFixedHeight(40);
    edit_btn_->setProperty("cssClass", QStringLiteral("primary"));
    header_row->addWidget(edit_btn_);

    delete_btn_ = make_icon_btn(
        QStringLiteral(":/icons/trash-2.svg"),
        IconRole::Danger, QStringLiteral("删除"), detail_container_);
    // 保持 cssClass="icon" 以维持 36x36 透明图标按钮样式，
    // 图标本身已用 IconRole::Danger 着色为红色，无需再用 danger 按钮样式。
    header_row->addWidget(delete_btn_);

    detail_layout->addLayout(header_row);

    detail_layout->addSpacing(28);

    // 字段网格（网站 / 用户名 / 密码 / 备注 / 时间）
    auto* fields_layout = new QVBoxLayout();
    fields_layout->setSpacing(16);

    auto add_field_row = [&](const QString& caption, QLabel*& value_label,
                              QPushButton*& copy_btn) {
        auto* row = new QFrame(detail_container_);
        row->setProperty("cssClass", QStringLiteral("fieldRow"));
        auto* row_layout = new QVBoxLayout(row);
        row_layout->setContentsMargins(0, 0, 0, 12);
        row_layout->setSpacing(6);

        auto* caption_label = new QLabel(caption, row);
        caption_label->setProperty("cssClass", QStringLiteral("fieldCaption"));
        row_layout->addWidget(caption_label);

        auto* value_row = new QHBoxLayout();
        value_row->setSpacing(8);
        value_label = new QLabel(row);
        value_label->setProperty("cssClass", QStringLiteral("fieldLabel"));
        value_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        value_row->addWidget(value_label, 1);
        copy_btn = make_small_icon_btn(
            QStringLiteral(":/icons/copy.svg"),
            IconRole::Normal, QStringLiteral("复制"), row);
        value_row->addWidget(copy_btn, 0, Qt::AlignRight);
        row_layout->addLayout(value_row);
        fields_layout->addWidget(row);
        return row;
    };

    // 网站
    add_field_row(QStringLiteral("网站"), field_website_, copy_website_btn_);
    // 用户名
    add_field_row(QStringLiteral("用户名"), field_username_, copy_user_field_btn_);

    // 密码行（特殊：带可见性切换 + 强度 badge）
    auto* pwd_row = new QFrame(detail_container_);
    pwd_row->setProperty("cssClass", QStringLiteral("fieldRow"));
    auto* pwd_row_layout = new QVBoxLayout(pwd_row);
    pwd_row_layout->setContentsMargins(0, 0, 0, 12);
    pwd_row_layout->setSpacing(6);

    auto* pwd_caption = new QLabel(QStringLiteral("密码"), pwd_row);
    pwd_caption->setProperty("cssClass", QStringLiteral("fieldCaption"));
    pwd_row_layout->addWidget(pwd_caption);

    auto* pwd_value_row = new QHBoxLayout();
    pwd_value_row->setSpacing(8);
    field_password_ = new QLabel(pwd_row);
    field_password_->setProperty("cssClass", QStringLiteral("textMono"));
    pwd_value_row->addWidget(field_password_, 1);

    toggle_pwd_btn_ = make_small_icon_btn(
        QStringLiteral(":/icons/eye.svg"),
        IconRole::Normal, QStringLiteral("显示密码"), pwd_row);
    pwd_value_row->addWidget(toggle_pwd_btn_);

    copy_pwd_field_btn_ = make_small_icon_btn(
        QStringLiteral(":/icons/copy.svg"),
        IconRole::Normal, QStringLiteral("复制密码"), pwd_row);
    pwd_value_row->addWidget(copy_pwd_field_btn_);

    strength_badge_ = new QLabel(pwd_row);
    strength_badge_->setProperty("cssClass", QStringLiteral("badgeSuccess"));
    strength_badge_->setAlignment(Qt::AlignCenter);
    pwd_value_row->addWidget(strength_badge_);
    pwd_row_layout->addLayout(pwd_value_row);
    fields_layout->addWidget(pwd_row);

    // 备注
    auto* note_row = new QFrame(detail_container_);
    note_row->setProperty("cssClass", QStringLiteral("fieldRow"));
    auto* note_row_layout = new QVBoxLayout(note_row);
    note_row_layout->setContentsMargins(0, 0, 0, 12);
    note_row_layout->setSpacing(6);

    auto* note_caption = new QLabel(QStringLiteral("备注"), note_row);
    note_caption->setProperty("cssClass", QStringLiteral("fieldCaption"));
    note_row_layout->addWidget(note_caption);

    field_note_ = new QLabel(note_row);
    field_note_->setWordWrap(true);
    field_note_->setProperty("cssClass", QStringLiteral("fieldLabel"));
    field_note_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    note_row_layout->addWidget(field_note_);
    fields_layout->addWidget(note_row);

    // 时间行
    auto* time_row = new QHBoxLayout();
    time_row->setSpacing(16);

    auto* created_col = new QVBoxLayout();
    created_col->setSpacing(6);
    auto* created_caption = new QLabel(QStringLiteral("创建时间"), detail_container_);
    created_caption->setProperty("cssClass", QStringLiteral("fieldCaption"));
    created_col->addWidget(created_caption);
    field_created_ = new QLabel(detail_container_);
    field_created_->setProperty("cssClass", QStringLiteral("caption"));
    created_col->addWidget(field_created_);
    time_row->addLayout(created_col);

    auto* updated_col = new QVBoxLayout();
    updated_col->setSpacing(6);
    auto* updated_caption = new QLabel(QStringLiteral("更新时间"), detail_container_);
    updated_caption->setProperty("cssClass", QStringLiteral("fieldCaption"));
    updated_col->addWidget(updated_caption);
    field_updated_ = new QLabel(detail_container_);
    field_updated_->setProperty("cssClass", QStringLiteral("caption"));
    updated_col->addWidget(field_updated_);
    time_row->addLayout(updated_col);
    time_row->addStretch(1);
    fields_layout->addLayout(time_row);

    detail_layout->addLayout(fields_layout);

    // 外部链接按钮
    open_external_btn_ = new QPushButton(detail_container_);
    open_external_btn_->setIcon(tinted_icon(QStringLiteral(":/icons/external-link.svg"), IconRole::Normal));
    open_external_btn_->setIconSize(QSize(14, 14));
    open_external_btn_->setCursor(Qt::PointingHandCursor);
    open_external_btn_->setFixedHeight(40);
    open_external_btn_->setProperty("cssClass", QStringLiteral("outline"));
    detail_layout->addSpacing(24);
    detail_layout->addWidget(open_external_btn_);
    detail_layout->addStretch(1);

    // 默认隐藏所有详情控件
    clear_detail();

    // ── 信号槽 ──
    connect(search_edit_, &QLineEdit::returnPressed,
            this, &PasswordBookView::on_search_clicked);
    connect(search_edit_, &QLineEdit::textChanged,
            this, &PasswordBookView::on_search_text_changed);
    connect(refresh_button_, &QPushButton::clicked,
            this, &PasswordBookView::on_refresh_clicked);
    connect(list_, &QListWidget::currentItemChanged,
            this, &PasswordBookView::on_list_item_changed);
    connect(copy_user_btn_, &QPushButton::clicked,
            this, &PasswordBookView::on_copy_username_clicked);
    connect(copy_pwd_btn_, &QPushButton::clicked,
            this, &PasswordBookView::on_copy_password_clicked);
    connect(copy_website_btn_, &QPushButton::clicked,
            this, [this]() {
                if (current_id_ == 0) return;
                QApplication::clipboard()->setText(
                    QString::fromStdString(current_entry()->website));
            });
    connect(copy_user_field_btn_, &QPushButton::clicked,
            this, &PasswordBookView::on_copy_username_clicked);
    connect(copy_pwd_field_btn_, &QPushButton::clicked,
            this, &PasswordBookView::on_copy_password_clicked);
    connect(toggle_pwd_btn_, &QPushButton::clicked,
            this, &PasswordBookView::on_toggle_password_clicked);
    connect(edit_btn_, &QPushButton::clicked,
            this, &PasswordBookView::on_edit_clicked);
    connect(delete_btn_, &QPushButton::clicked,
            this, &PasswordBookView::on_delete_clicked);
    connect(open_external_btn_, &QPushButton::clicked,
            this, &PasswordBookView::on_open_external_clicked);
}

// ---------------------------------------------------------------------------
// 列表加载与搜索
// ---------------------------------------------------------------------------

void PasswordBookView::refresh() {
    if (!client_) {
        show_empty_state(QStringLiteral("IPC 客户端不可用"));
        return;
    }
    auto result = client_->list_entries();
    if (result.ok()) {
        populate_list(result.value().entries);
    } else {
        populate_list({});
        show_empty_state(
            QStringLiteral("加载失败：%1")
                .arg(QString::fromStdString(result.error().what())));
    }
}

void PasswordBookView::on_search_clicked() {
    if (!client_) return;
    const QString text = search_edit_->text().trimmed();
    if (text.isEmpty()) {
        refresh();
        return;
    }

    core::SearchQuery query;
    query.text = text.toStdString();
    query.case_sensitive = false;

    const QString field = field_combo_->currentData().toString();
    if (field == QStringLiteral("website")) {
        query.fields = {"website"};
    } else if (field == QStringLiteral("username")) {
        query.fields = {"username"};
    } else if (field == QStringLiteral("note")) {
        query.fields = {"note"};
    }

    auto result = client_->search_entries(query);
    if (result.ok()) {
        populate_list(result.value().entries);
    } else {
        populate_list({});
        show_empty_state(
            QStringLiteral("搜索失败：%1")
                .arg(QString::fromStdString(result.error().what())));
    }
}

void PasswordBookView::on_refresh_clicked() {
    // 用 QSignalBlocker 阻止 clear() 触发 textChanged → on_search_text_changed → refresh()，
    // 避免与下面的显式 refresh() 重复发起 IPC 请求。
    {
        QSignalBlocker blocker(search_edit_);
        search_edit_->clear();
    }
    field_combo_->setCurrentIndex(0);
    refresh();
}

void PasswordBookView::on_search_text_changed(const QString& text) {
    // 文本清空时自动刷新全量
    if (text.isEmpty()) {
        refresh();
    }
}

void PasswordBookView::populate_list(const std::vector<core::PasswordEntry>& new_entries) {
    entries_ = new_entries;
    list_->clear();

    for (const auto& e : entries_) {
        auto* item = new QListWidgetItem(list_);
        item->setData(Qt::UserRole, static_cast<qint64>(e.id));
        item->setSizeHint(QSize(320, 60));

        // 自定义 item widget：头像 + 名称 + 用户名 + chevron
        auto* widget = new QWidget(list_);
        widget->setStyleSheet(QStringLiteral("background: transparent;"));
        auto* row = new QHBoxLayout(widget);
        row->setContentsMargins(12, 8, 12, 8);
        row->setSpacing(10);

        auto* avatar = new QLabel(widget);
        avatar->setFixedSize(40, 40);
        avatar->setAlignment(Qt::AlignCenter);
        avatar->setProperty("cssClass", QStringLiteral("avatar"));
        avatar->setText(avatar_letter(e.website));
        row->addWidget(avatar);

        auto* text_col = new QVBoxLayout();
        text_col->setSpacing(2);
        auto* name_label = new QLabel(
            QString::fromStdString(e.website), widget);
        name_label->setProperty("cssClass", QStringLiteral("fieldLabel"));
        // 截断过长文本
        name_label->setWordWrap(false);
        text_col->addWidget(name_label);

        auto* user_label = new QLabel(
            QString::fromStdString(e.username), widget);
        user_label->setProperty("cssClass", QStringLiteral("caption"));
        user_label->setWordWrap(false);
        text_col->addWidget(user_label);
        text_col->addStretch(1);
        row->addLayout(text_col, 1);

        auto* chevron = new QLabel(widget);
        chevron->setPixmap(tinted_pixmap(QStringLiteral(":/icons/chevron-right.svg"), IconRole::Normal, QSize(16, 16)));
        chevron->setProperty("cssClass", QStringLiteral("inlineIcon"));
        row->addWidget(chevron);

        list_->setItemWidget(item, widget);
    }

    emit entry_count_changed(static_cast<int>(entries_.size()));

    if (entries_.empty()) {
        show_empty_state(QStringLiteral("暂无密码条目"));
    } else {
        // 默认选中第一条
        list_->setCurrentRow(0);
    }
}

// ---------------------------------------------------------------------------
// 详情显示
// ---------------------------------------------------------------------------

void PasswordBookView::show_empty_state(const QString& message) {
    empty_hint_->setText(message);
    empty_hint_->show();
    clear_detail();
}

void PasswordBookView::clear_detail() {
    current_id_ = 0;
    password_visible_ = false;
    if (detail_avatar_) detail_avatar_->hide();
    if (detail_title_) detail_title_->hide();
    if (detail_subtitle_) detail_subtitle_->hide();
    if (copy_user_btn_) copy_user_btn_->hide();
    if (copy_pwd_btn_) copy_pwd_btn_->hide();
    if (edit_btn_) edit_btn_->hide();
    if (delete_btn_) delete_btn_->hide();
    if (field_website_ && field_website_->parentWidget()) field_website_->parentWidget()->hide();
    if (field_username_ && field_username_->parentWidget()) field_username_->parentWidget()->hide();
    if (field_password_ && field_password_->parentWidget()) field_password_->parentWidget()->hide();
    if (field_note_ && field_note_->parentWidget()) field_note_->parentWidget()->hide();
    if (field_created_) field_created_->hide();
    if (field_updated_) field_updated_->hide();
    if (strength_badge_) strength_badge_->hide();
    if (toggle_pwd_btn_) toggle_pwd_btn_->hide();
    if (copy_website_btn_) copy_website_btn_->hide();
    if (copy_user_field_btn_) copy_user_field_btn_->hide();
    if (copy_pwd_field_btn_) copy_pwd_field_btn_->hide();
    if (open_external_btn_) open_external_btn_->hide();
    set_detail_actions_enabled(false);
}

void PasswordBookView::load_detail(const core::PasswordEntry& entry) {
    current_id_ = entry.id;
    password_visible_ = false;

    empty_hint_->hide();

    // 头部
    detail_avatar_->setText(avatar_letter(entry.website));
    detail_avatar_->show();
    detail_title_->setText(QString::fromStdString(entry.website));
    detail_title_->show();
    detail_subtitle_->setText(QString::fromStdString(entry.username));
    detail_subtitle_->show();

    copy_user_btn_->show();
    copy_pwd_btn_->show();
    edit_btn_->show();
    delete_btn_->show();

    // 字段
    field_website_->setText(QString::fromStdString(entry.website));
    field_website_->parentWidget()->show();
    field_username_->setText(QString::fromStdString(entry.username));
    field_username_->parentWidget()->show();

    // 密码：默认掩码
    field_password_->setText(QStringLiteral("••••••••••••"));
    field_password_->parentWidget()->show();
    toggle_pwd_btn_->setIcon(tinted_icon(QStringLiteral(":/icons/eye.svg"), IconRole::Normal));
    toggle_pwd_btn_->setToolTip(QStringLiteral("显示密码"));
    toggle_pwd_btn_->show();
    copy_pwd_field_btn_->show();

    // 强度 badge（仅当 client 可用时查询）
    if (client_) {
        auto r = client_->estimate_strength(entry.password);
        if (r.ok()) {
            const int bits = r.value().strength_bits;
            strength_badge_->setText(strength_text_for_bits(bits));
            strength_badge_->setProperty("cssClass", strength_badge_class(bits));
            strength_badge_->style()->unpolish(strength_badge_);
            strength_badge_->style()->polish(strength_badge_);
            strength_badge_->show();
        } else {
            strength_badge_->hide();
        }
    } else {
        strength_badge_->hide();
    }

    // 备注
    const QString note_text = entry.note.empty()
        ? QStringLiteral("（无）")
        : QString::fromStdString(entry.note);
    field_note_->setText(note_text);
    field_note_->parentWidget()->show();

    // 时间
    field_created_->setText(format_time(entry.created_at));
    field_created_->show();
    field_updated_->setText(format_time(entry.updated_at));
    field_updated_->show();

    copy_website_btn_->show();
    copy_user_field_btn_->show();

    // 外部链接
    open_external_btn_->setText(
        QStringLiteral("打开 %1").arg(QString::fromStdString(entry.website)));
    open_external_btn_->show();

    set_detail_actions_enabled(true);
}

void PasswordBookView::set_detail_actions_enabled(bool enabled) {
    if (copy_user_btn_) copy_user_btn_->setEnabled(enabled);
    if (copy_pwd_btn_) copy_pwd_btn_->setEnabled(enabled);
    if (edit_btn_) edit_btn_->setEnabled(enabled);
    if (delete_btn_) delete_btn_->setEnabled(enabled);
    if (toggle_pwd_btn_) toggle_pwd_btn_->setEnabled(enabled);
    if (copy_website_btn_) copy_website_btn_->setEnabled(enabled);
    if (copy_user_field_btn_) copy_user_field_btn_->setEnabled(enabled);
    if (copy_pwd_field_btn_) copy_pwd_field_btn_->setEnabled(enabled);
    if (open_external_btn_) open_external_btn_->setEnabled(enabled);
}

const core::PasswordEntry* PasswordBookView::current_entry() const {
    if (current_id_ == 0) return nullptr;
    for (const auto& e : entries_) {
        if (e.id == current_id_) return &e;
    }
    return nullptr;
}

void PasswordBookView::on_list_item_changed(QListWidgetItem* current, QListWidgetItem* previous) {
    (void)previous;
    if (!current) {
        clear_detail();
        return;
    }
    const int64_t id = static_cast<int64_t>(current->data(Qt::UserRole).toLongLong());
    for (const auto& e : entries_) {
        if (e.id == id) {
            load_detail(e);
            return;
        }
    }
    clear_detail();
}

// ---------------------------------------------------------------------------
// 操作
// ---------------------------------------------------------------------------

void PasswordBookView::on_copy_username_clicked() {
    const auto* entry = current_entry();
    if (!entry) return;
    QApplication::clipboard()->setText(QString::fromStdString(entry->username));
}

void PasswordBookView::on_copy_password_clicked() {
    const auto* entry = current_entry();
    if (!entry || !client_) return;
    // 调用 get_entry 拿最新明文密码（保险起见）
    auto r = client_->get_entry(entry->id);
    if (r.ok()) {
        copy_secure_to_clipboard(
            QString::fromStdString(r.value().entry.password));
    } else {
        // 回退到列表缓存
        copy_secure_to_clipboard(
            QString::fromStdString(entry->password));
    }
}

void PasswordBookView::on_toggle_password_clicked() {
    const auto* entry = current_entry();
    if (!entry) return;
    password_visible_ = !password_visible_;
    if (password_visible_) {
        // 调用 get_entry 拿明文密码（列表中可能已过期）
        QString pwd = QString::fromStdString(entry->password);
        if (client_) {
            auto r = client_->get_entry(entry->id);
            if (r.ok()) {
                pwd = QString::fromStdString(r.value().entry.password);
            }
        }
        field_password_->setText(pwd);
        toggle_pwd_btn_->setIcon(tinted_icon(QStringLiteral(":/icons/eye-off.svg"), IconRole::Normal));
        toggle_pwd_btn_->setToolTip(QStringLiteral("隐藏密码"));
    } else {
        field_password_->setText(QStringLiteral("••••••••••••"));
        toggle_pwd_btn_->setIcon(tinted_icon(QStringLiteral(":/icons/eye.svg"), IconRole::Normal));
        toggle_pwd_btn_->setToolTip(QStringLiteral("显示密码"));
    }
}

void PasswordBookView::on_edit_clicked() {
    const auto* entry = current_entry();
    if (!entry) return;

    // 调用 get_entry 拿最新完整数据（含明文密码）
    core::PasswordEntry current = *entry;
    if (client_) {
        auto r = client_->get_entry(entry->id);
        if (r.ok()) {
            current = r.value().entry;
        }
    }

    EditEntryDialog dlg(client_, current, this);
    connect(&dlg, &EditEntryDialog::entry_updated,
            this, &PasswordBookView::entry_updated);
    if (dlg.exec() == QDialog::Accepted) {
        refresh();
    }
}

void PasswordBookView::on_delete_clicked() {
    if (current_id_ == 0) return;
    const auto* entry = current_entry();
    const QString name = entry
        ? QString::fromStdString(entry->website)
        : QStringLiteral("id=%1").arg(current_id_);

    const auto answer = QMessageBox::question(
        this,
        QStringLiteral("确认删除"),
        QStringLiteral("确定要删除条目「%1」吗？此操作不可撤销。").arg(name),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    if (!client_) return;
    auto result = client_->remove_entry(current_id_);
    if (result.ok()) {
        refresh();
    } else {
        const QString msg = QString::fromStdString(result.error().what());
        QMessageBox::warning(this, QStringLiteral("删除失败"),
            msg.isEmpty() ? QStringLiteral("删除条目失败。")
                          : QStringLiteral("删除失败：%1").arg(msg));
    }
}

void PasswordBookView::on_open_external_clicked() {
    const auto* entry = current_entry();
    if (!entry) return;
    QString url = QString::fromStdString(entry->website);
    if (!url.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) &&
        !url.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
        url = QStringLiteral("https://") + url;
    }
    QDesktopServices::openUrl(QUrl(url));
}

}  // namespace pwdvault::ui
