// coding: utf-8
// =============================================================================
// GeneratorHistoryDialog.cpp
//
// PwdVault 生成器历史记录对话框实现。
// 模态遮罩 + 720px 居中卡片 + 工具栏 + 表格 + 尾部按钮。
// =============================================================================
#include "GeneratorHistoryDialog.h"
#include "IpcClient.h"
#include "IconKit.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

namespace pwdvault::ui {

namespace {

/// 默认的密码遮罩字符，按密码长度重复填充。
/// 用全角字符 U+2022 与 QSS 配合以与项目其他位置一致。
QString masked_password(int length) {
    if (length <= 0) return QStringLiteral("•");
    return QString(QStringLiteral("•")).repeated(length);
}

QString format_time(int64_t ts) {
    if (ts <= 0) return QStringLiteral("-");
    return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(ts))
        .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

}  // namespace

GeneratorHistoryDialog::GeneratorHistoryDialog(IpcClient* client, QWidget* parent)
    : QDialog(parent), client_(client)
{
    // 模态 + 无原生标题栏，自定义遮罩 + 卡片
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setWindowModality(Qt::ApplicationModal);
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowTitle(QStringLiteral("生成器历史记录"));

    // 自动覆盖父窗口大小
    if (parent) {
        setGeometry(parent->window()->geometry());
    }

    build_ui();
}

GeneratorHistoryDialog::~GeneratorHistoryDialog() = default;

void GeneratorHistoryDialog::closeEvent(QCloseEvent* event) {
    QDialog::closeEvent(event);
}

void GeneratorHistoryDialog::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QDialog::keyPressEvent(event);
}

// ---------------------------------------------------------------------------
// UI 构建
// ---------------------------------------------------------------------------

void GeneratorHistoryDialog::build_ui() {
    // 根布局：半透明遮罩
    auto* root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(0, 0, 0, 0);
    root_layout->setSpacing(0);

    auto* overlay = new QFrame(this);
    overlay->setProperty("cssClass", QStringLiteral("modalOverlay"));
    auto* overlay_layout = new QVBoxLayout(overlay);
    overlay_layout->setContentsMargins(16, 16, 16, 16);
    overlay_layout->setAlignment(Qt::AlignCenter);

    // ── 720px 卡片 ──
    auto* card = new QFrame(overlay);
    card->setFixedWidth(720);
    card->setProperty("cssClass", QStringLiteral("modal"));
    auto* card_layout = new QVBoxLayout(card);
    card_layout->setContentsMargins(0, 0, 0, 0);
    card_layout->setSpacing(0);

    // ── 头部（56px 高） ──
    auto* header = new QFrame(card);
    header->setFixedHeight(56);
    header->setProperty("cssClass", QStringLiteral("modalHeader"));
    auto* header_layout = new QHBoxLayout(header);
    header_layout->setContentsMargins(24, 0, 12, 0);
    header_layout->setSpacing(10);

    auto* clock_icon = new QLabel(header);
    clock_icon->setPixmap(tinted_pixmap(QStringLiteral(":/icons/clock.svg"),
                                       IconRole::Normal, QSize(18, 18)));
    clock_icon->setProperty("cssClass", QStringLiteral("inlineIcon"));
    header_layout->addWidget(clock_icon);

    auto* title = new QLabel(QStringLiteral("生成器历史记录"), header);
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

    // ── 体 ──
    auto* body = new QFrame(card);
    body->setProperty("cssClass", QStringLiteral("modalBody"));
    auto* body_layout = new QVBoxLayout(body);
    body_layout->setContentsMargins(24, 20, 24, 20);
    body_layout->setSpacing(12);

    // 工具栏：[显示密码] + stretch + [状态标签] + [刷新]
    auto* toolbar = new QHBoxLayout();
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSpacing(12);

    show_password_check_ = new QCheckBox(QStringLiteral("显示密码"), body);
    show_password_check_->setCursor(Qt::PointingHandCursor);
    toolbar->addWidget(show_password_check_);
    toolbar->addStretch(1);

    status_label_ = new QLabel(body);
    status_label_->setProperty("cssClass", QStringLiteral("caption"));
    toolbar->addWidget(status_label_);

    refresh_btn_ = new QPushButton(body);
    refresh_btn_->setIcon(tinted_icon(QStringLiteral(":/icons/refresh-cw.svg"), IconRole::Normal));
    refresh_btn_->setIconSize(QSize(16, 16));
    refresh_btn_->setText(QStringLiteral("刷新"));
    refresh_btn_->setCursor(Qt::PointingHandCursor);
    refresh_btn_->setFixedHeight(36);
    refresh_btn_->setProperty("cssClass", QStringLiteral("outline"));
    toolbar->addWidget(refresh_btn_);

    body_layout->addLayout(toolbar);

    // ── 表格 ──
    table_ = new QTableWidget(body);
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels(
        QStringList() << QStringLiteral("#")
                       << QStringLiteral("时间")
                       << QStringLiteral("长度")
                       << QStringLiteral("密码")
                       << QStringLiteral("操作"));
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setFocusPolicy(Qt::NoFocus);
    table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    // 列宽：固定列 + 密码列随表拉伸
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    table_->setMinimumHeight(320);
    // 行高与单元格内嵌按钮配合（按钮 32px + 8 padding）
    table_->verticalHeader()->setDefaultSectionSize(40);
    body_layout->addWidget(table_, 1);

    card_layout->addWidget(body, 1);

    // ── 尾部 ──
    auto* footer = new QFrame(card);
    footer->setProperty("cssClass", QStringLiteral("modalFooter"));
    footer->setFixedHeight(64);
    auto* footer_layout = new QHBoxLayout(footer);
    footer_layout->setContentsMargins(24, 0, 24, 0);
    footer_layout->setSpacing(12);

    clear_all_btn_ = new QPushButton(footer);
    clear_all_btn_->setIcon(tinted_icon(QStringLiteral(":/icons/eraser.svg"), IconRole::Danger));
    clear_all_btn_->setIconSize(QSize(16, 16));
    clear_all_btn_->setText(QStringLiteral("清空全部"));
    clear_all_btn_->setCursor(Qt::PointingHandCursor);
    clear_all_btn_->setFixedHeight(40);
    clear_all_btn_->setProperty("cssClass", QStringLiteral("danger"));
    footer_layout->addWidget(clear_all_btn_);

    footer_layout->addStretch(1);

    close_btn_ = new QPushButton(footer);
    close_btn_->setText(QStringLiteral("关闭"));
    close_btn_->setCursor(Qt::PointingHandCursor);
    close_btn_->setFixedHeight(40);
    close_btn_->setProperty("cssClass", QStringLiteral("primary"));
    footer_layout->addWidget(close_btn_);

    card_layout->addWidget(footer);
    overlay_layout->addWidget(card);
    root_layout->addWidget(overlay);

    // 信号槽
    connect(show_password_check_, &QCheckBox::toggled,
            this, &GeneratorHistoryDialog::on_show_password_toggled);
    connect(refresh_btn_, &QPushButton::clicked,
            this, &GeneratorHistoryDialog::on_refresh_clicked);
    connect(clear_all_btn_, &QPushButton::clicked,
            this, &GeneratorHistoryDialog::on_clear_all_clicked);
    connect(close_btn_, &QPushButton::clicked,
            this, &GeneratorHistoryDialog::on_close_clicked);
    // 头部 X 关闭按钮也调用 close_dialog（与 close_btn 同语义）
    connect(close_btn, &QPushButton::clicked, this, &QDialog::close);

    // 初次加载
    populate_table();
}

// ---------------------------------------------------------------------------
// 加载与渲染
// ---------------------------------------------------------------------------

void GeneratorHistoryDialog::reload() {
    populate_table();
}

void GeneratorHistoryDialog::populate_table() {
    records_.clear();
    table_->setRowCount(0);

    if (!client_) {
        set_status(QStringLiteral("未连接到 service"), /*is_error=*/true);
        return;
    }

    auto result = client_->list_generated_records();
    if (!result.ok()) {
        set_status(QString::fromStdString(result.error().what()), /*is_error=*/true);
        return;
    }

    records_ = std::move(result.value().records);

    table_->setRowCount(static_cast<int>(records_.size()));
    for (int row = 0; row < static_cast<int>(records_.size()); ++row) {
        const auto& rec = records_[row];
        // # 行号（从 1 起，按时间倒序）
        auto* idx_item = new QTableWidgetItem(QString::number(row + 1));
        idx_item->setTextAlignment(Qt::AlignCenter);
        table_->setItem(row, 0, idx_item);

        // 时间
        auto* time_item = new QTableWidgetItem(format_time(rec.created_at));
        time_item->setTextAlignment(Qt::AlignCenter);
        table_->setItem(row, 1, time_item);

        // 长度
        auto* len_item = new QTableWidgetItem(QString::number(rec.length));
        len_item->setTextAlignment(Qt::AlignCenter);
        table_->setItem(row, 2, len_item);

        // 密码（默认遮罩）
        auto* pwd_item = new QTableWidgetItem(masked_password(rec.length));
        // 用户不应能复制遮罩文本：禁用 selectable 即可
        pwd_item->setFlags(pwd_item->flags() & ~Qt::ItemIsEditable);
        table_->setItem(row, 3, pwd_item);

        // 操作（复制 / 删除）
        install_action_widget(row, rec.id);
    }

    refresh_password_cells();

    if (records_.empty()) {
        set_status(QStringLiteral("暂无记录"), /*is_error=*/false);
    } else {
        set_status(QStringLiteral("共 %1 条记录").arg(records_.size()),
                   /*is_error=*/false);
    }
}

void GeneratorHistoryDialog::refresh_password_cells() {
    if (!table_) return;
    const bool show = show_password_check_ && show_password_check_->isChecked();
    for (int row = 0; row < static_cast<int>(records_.size()); ++row) {
        QString text = show ? QString::fromStdString(records_[row].password)
                           : masked_password(records_[row].length);
        auto* item = table_->item(row, 3);
        if (item) {
            item->setText(text);
        }
    }
}

void GeneratorHistoryDialog::install_action_widget(int row, int64_t record_id) {
    auto* widget = new QWidget(table_);
    auto* layout = new QHBoxLayout(widget);
    layout->setContentsMargins(4, 0, 4, 0);
    layout->setSpacing(6);

    auto* copy_btn = new QPushButton(widget);
    copy_btn->setIcon(tinted_icon(QStringLiteral(":/icons/copy.svg"), IconRole::Normal));
    copy_btn->setIconSize(QSize(14, 14));
    copy_btn->setToolTip(QStringLiteral("复制密码"));
    copy_btn->setCursor(Qt::PointingHandCursor);
    copy_btn->setFixedSize(32, 32);
    copy_btn->setProperty("cssClass", QStringLiteral("icon"));
    layout->addWidget(copy_btn);

    auto* del_btn = new QPushButton(widget);
    del_btn->setIcon(tinted_icon(QStringLiteral(":/icons/trash-2.svg"), IconRole::Danger));
    del_btn->setIconSize(QSize(14, 14));
    del_btn->setToolTip(QStringLiteral("删除此条"));
    del_btn->setCursor(Qt::PointingHandCursor);
    del_btn->setFixedSize(32, 32);
    del_btn->setProperty("cssClass", QStringLiteral("icon"));
    layout->addWidget(del_btn);

    // 捕获 record_id 用于回调
    connect(copy_btn, &QPushButton::clicked, this, [this, record_id]() {
        // 从 records_ 中找到对应密码
        for (const auto& rec : records_) {
            if (rec.id == record_id) {
                copy_secure_to_clipboard(QString::fromStdString(rec.password));
                set_status(QStringLiteral("已复制到剪贴板（30 秒后自动清空）"),
                           /*is_error=*/false);
                return;
            }
        }
    });
    connect(del_btn, &QPushButton::clicked, this, [this, record_id]() {
        delete_record(record_id);
    });

    table_->setCellWidget(row, 4, widget);
}

void GeneratorHistoryDialog::delete_record(int64_t record_id) {
    if (!client_) return;
    auto result = client_->remove_generated_record(record_id);
    if (!result.ok()) {
        QMessageBox::warning(this, QStringLiteral("删除失败"),
            QString::fromStdString(result.error().what()));
        return;
    }
    populate_table();
}

void GeneratorHistoryDialog::set_status(const QString& message, bool is_error) {
    if (!status_label_) return;
    status_label_->setText(message);
    // 切换 cssClass 以反映成功 / 错误状态
    status_label_->setProperty("cssClass",
        is_error ? QStringLiteral("error") : QStringLiteral("caption"));
    // unpolish/polish 让 QSS 重新应用
    status_label_->style()->unpolish(status_label_);
    status_label_->style()->polish(status_label_);
}

// ---------------------------------------------------------------------------
// 槽函数
// ---------------------------------------------------------------------------

void GeneratorHistoryDialog::on_show_password_toggled(bool checked) {
    (void)checked;
    refresh_password_cells();
}

void GeneratorHistoryDialog::on_refresh_clicked() {
    populate_table();
}

void GeneratorHistoryDialog::on_clear_all_clicked() {
    if (records_.empty()) return;
    const auto ret = QMessageBox::question(
        this, QStringLiteral("清空全部记录"),
        QStringLiteral("确认删除全部 %1 条生成记录？此操作不可撤销。")
            .arg(records_.size()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    if (!client_) return;
    auto result = client_->clear_generated_records();
    if (!result.ok()) {
        QMessageBox::warning(this, QStringLiteral("清空失败"),
            QString::fromStdString(result.error().what()));
        return;
    }
    populate_table();
}

void GeneratorHistoryDialog::on_close_clicked() {
    close();
}

}  // namespace pwdvault::ui
