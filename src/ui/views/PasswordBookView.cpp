// coding: utf-8
// =============================================================================
// PasswordBookView.cpp
//
// PwdVault 密码本视图实现。管理列表加载、搜索、详情、编辑、删除、复制。
// =============================================================================
#include "PasswordBookView.h"
#include "EditEntryDialog.h"
#include "IpcClient.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdint>
#include <string>
#include <vector>

namespace pwdvault::ui {

namespace {

/// 列索引。
constexpr int kColWebsite = 0;
constexpr int kColUsername = 1;
constexpr int kColNote = 2;
constexpr int kColUpdated = 3;

/// Unix 时间戳（秒）→ 可读字符串。
QString format_time(int64_t ts) {
    if (ts <= 0) return QStringLiteral("-");
    return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(ts))
        .toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

}  // namespace

PasswordBookView::PasswordBookView(IpcClient* client, QWidget* parent)
    : QWidget(parent), client_(client)
{
    build_ui();
    set_buttons_enabled(false);
}

PasswordBookView::~PasswordBookView() = default;

void PasswordBookView::build_ui() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // 顶部搜索栏
    auto* top_bar = new QHBoxLayout();
    search_edit_ = new QLineEdit(this);
    search_edit_->setPlaceholderText(QStringLiteral("输入关键词搜索..."));
    top_bar->addWidget(search_edit_, 1);

    field_combo_ = new QComboBox(this);
    field_combo_->addItem(QStringLiteral("全部"), QStringLiteral("all"));
    field_combo_->addItem(QStringLiteral("网站"), QStringLiteral("website"));
    field_combo_->addItem(QStringLiteral("用户名"), QStringLiteral("username"));
    field_combo_->addItem(QStringLiteral("备注"), QStringLiteral("note"));
    top_bar->addWidget(field_combo_);

    search_button_ = new QPushButton(QStringLiteral("搜索"), this);
    top_bar->addWidget(search_button_);

    refresh_button_ = new QPushButton(QStringLiteral("刷新"), this);
    top_bar->addWidget(refresh_button_);

    layout->addLayout(top_bar);

    // 中间表格
    table_ = new QTableWidget(this);
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels(
        {QStringLiteral("网站"),
         QStringLiteral("用户名"),
         QStringLiteral("备注"),
         QStringLiteral("更新时间")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionResizeMode(kColWebsite, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(kColUsername, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(kColNote, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(kColUpdated, QHeaderView::ResizeToContents);
    table_->verticalHeader()->setVisible(false);
    table_->setAlternatingRowColors(true);
    layout->addWidget(table_, 1);

    // 底部按钮栏
    auto* bottom_bar = new QHBoxLayout();
    view_detail_button_ = new QPushButton(QStringLiteral("查看详情"), this);
    edit_button_ = new QPushButton(QStringLiteral("编辑"), this);
    delete_button_ = new QPushButton(QStringLiteral("删除"), this);
    copy_password_button_ = new QPushButton(QStringLiteral("复制密码"), this);
    bottom_bar->addWidget(view_detail_button_);
    bottom_bar->addWidget(edit_button_);
    bottom_bar->addWidget(delete_button_);
    bottom_bar->addStretch(1);
    bottom_bar->addWidget(copy_password_button_);
    layout->addLayout(bottom_bar);

    // 状态标签
    status_label_ = new QLabel(this);
    status_label_->setStyleSheet(QStringLiteral("color: #555;"));
    layout->addWidget(status_label_);

    // 信号槽
    connect(search_button_, &QPushButton::clicked,
            this, &PasswordBookView::on_search_clicked);
    connect(refresh_button_, &QPushButton::clicked,
            this, &PasswordBookView::on_refresh_clicked);
    connect(table_, &QTableWidget::itemSelectionChanged,
            this, &PasswordBookView::on_item_selection_changed);
    connect(table_, &QTableWidget::cellDoubleClicked,
            this, &PasswordBookView::on_item_double_clicked);
    connect(view_detail_button_, &QPushButton::clicked,
            this, &PasswordBookView::on_view_detail_clicked);
    connect(edit_button_, &QPushButton::clicked,
            this, &PasswordBookView::on_edit_clicked);
    connect(delete_button_, &QPushButton::clicked,
            this, &PasswordBookView::on_delete_clicked);
    connect(copy_password_button_, &QPushButton::clicked,
            this, &PasswordBookView::on_copy_password_clicked);

    // 回车触发搜索
    connect(search_edit_, &QLineEdit::returnPressed,
            this, &PasswordBookView::on_search_clicked);
}

// ---------------------------------------------------------------------------
// 列表加载与搜索
// ---------------------------------------------------------------------------

void PasswordBookView::refresh() {
    if (!client_) {
        status_label_->setText(QStringLiteral("IPC 客户端不可用。"));
        return;
    }
    auto result = client_->list_entries();
    if (result.ok()) {
        populate_table(result.value().entries);
        status_label_->setText(
            QStringLiteral("已加载 %1 条记录。").arg(entries_.size()));
    } else {
        populate_table({});
        const QString msg = QString::fromStdString(result.error().what());
        status_label_->setText(QStringLiteral("加载失败：%1").arg(msg));
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
    // field == "all" → fields 为空，搜索全部字段

    auto result = client_->search_entries(query);
    if (result.ok()) {
        populate_table(result.value().entries);
        status_label_->setText(
            QStringLiteral("找到 %1 条匹配记录。").arg(entries_.size()));
    } else {
        populate_table({});
        const QString msg = QString::fromStdString(result.error().what());
        status_label_->setText(QStringLiteral("搜索失败：%1").arg(msg));
    }
}

void PasswordBookView::on_refresh_clicked() {
    search_edit_->clear();
    field_combo_->setCurrentIndex(0);
    refresh();
}

void PasswordBookView::populate_table(const std::vector<core::PasswordEntry>& new_entries) {
    entries_ = new_entries;

    table_->setRowCount(static_cast<int>(entries_.size()));
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        const auto& e = entries_[i];

        auto* website_item = new QTableWidgetItem(QString::fromStdString(e.website));
        // 在第一列保存 id（Qt::UserRole）
        website_item->setData(Qt::UserRole, static_cast<qint64>(e.id));

        auto* username_item = new QTableWidgetItem(QString::fromStdString(e.username));
        auto* note_item = new QTableWidgetItem(QString::fromStdString(e.note));
        auto* updated_item = new QTableWidgetItem(format_time(e.updated_at));

        table_->setItem(i, kColWebsite, website_item);
        table_->setItem(i, kColUsername, username_item);
        table_->setItem(i, kColNote, note_item);
        table_->setItem(i, kColUpdated, updated_item);
    }
    table_->resizeColumnsToContents();
    table_->resizeRowsToContents();

    set_buttons_enabled(false);
}

// ---------------------------------------------------------------------------
// 选择与按钮状态
// ---------------------------------------------------------------------------

void PasswordBookView::on_item_selection_changed() {
    const bool has_selection = !table_->selectedItems().isEmpty();
    set_buttons_enabled(has_selection);
}

void PasswordBookView::on_item_double_clicked(int /*row*/, int /*column*/) {
    const auto* entry = selected_entry();
    if (entry) {
        show_detail_dialog(*entry);
    }
}

void PasswordBookView::set_buttons_enabled(bool enabled) {
    if (view_detail_button_) view_detail_button_->setEnabled(enabled);
    if (edit_button_) edit_button_->setEnabled(enabled);
    if (delete_button_) delete_button_->setEnabled(enabled);
    if (copy_password_button_) copy_password_button_->setEnabled(enabled);
}

int64_t PasswordBookView::selected_id() const {
    const int row = table_->currentRow();
    if (row < 0) return 0;
    auto* item = table_->item(row, kColWebsite);
    if (!item) return 0;
    return static_cast<int64_t>(item->data(Qt::UserRole).toLongLong());
}

const core::PasswordEntry* PasswordBookView::selected_entry() const {
    const int64_t id = selected_id();
    if (id == 0) return nullptr;
    for (const auto& e : entries_) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// 操作：详情 / 编辑 / 删除 / 复制
// ---------------------------------------------------------------------------

void PasswordBookView::on_view_detail_clicked() {
    const auto* entry = selected_entry();
    if (entry) {
        show_detail_dialog(*entry);
    }
}

void PasswordBookView::show_detail_dialog(const core::PasswordEntry& entry) {
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("条目详情"));
    dlg.setMinimumSize(380, 280);

    auto* layout = new QVBoxLayout(&dlg);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(8);

    auto* form = new QFormLayout();
    form->setSpacing(6);

    auto* website_value = new QLabel(QString::fromStdString(entry.website), &dlg);
    website_value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(QStringLiteral("网站/应用："), website_value);

    auto* username_value = new QLabel(QString::fromStdString(entry.username), &dlg);
    username_value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(QStringLiteral("用户名："), username_value);

    auto* password_value = new QLineEdit(QString::fromStdString(entry.password), &dlg);
    password_value->setEchoMode(QLineEdit::Password);
    password_value->setReadOnly(true);
    form->addRow(QStringLiteral("密码："), password_value);

    auto* show_pwd = new QCheckBox(QStringLiteral("显示密码"), &dlg);
    form->addRow(QString(), show_pwd);

    auto* note_value = new QLabel(
        entry.note.empty() ? QStringLiteral("(无)") : QString::fromStdString(entry.note), &dlg);
    note_value->setWordWrap(true);
    note_value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(QStringLiteral("备注："), note_value);

    form->addRow(QStringLiteral("创建时间："),
                 new QLabel(format_time(entry.created_at), &dlg));
    form->addRow(QStringLiteral("更新时间："),
                 new QLabel(format_time(entry.updated_at), &dlg));

    layout->addLayout(form);

    layout->addStretch(1);

    // 操作按钮
    auto* btn_box = new QDialogButtonBox(Qt::Horizontal, &dlg);
    auto* copy_pwd_btn = btn_box->addButton(
        QStringLiteral("复制密码"), QDialogButtonBox::ActionRole);
    auto* copy_user_btn = btn_box->addButton(
        QStringLiteral("复制用户名"), QDialogButtonBox::ActionRole);
    btn_box->addButton(QStringLiteral("关闭"), QDialogButtonBox::AcceptRole);
    layout->addWidget(btn_box);

    QObject::connect(show_pwd, &QCheckBox::toggled,
        [password_value](bool checked) {
            password_value->setEchoMode(checked ? QLineEdit::Normal : QLineEdit::Password);
        });
    QObject::connect(copy_pwd_btn, &QPushButton::clicked, [&entry]() {
        QApplication::clipboard()->setText(QString::fromStdString(entry.password));
    });
    QObject::connect(copy_user_btn, &QPushButton::clicked, [&entry]() {
        QApplication::clipboard()->setText(QString::fromStdString(entry.username));
    });
    QObject::connect(btn_box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);

    dlg.exec();
}

void PasswordBookView::on_edit_clicked() {
    const auto* entry = selected_entry();
    if (!entry) return;

    EditEntryDialog dlg(client_, *entry, this);
    connect(&dlg, &EditEntryDialog::entry_updated,
            this, &PasswordBookView::entry_updated);
    if (dlg.exec() == QDialog::Accepted) {
        // 编辑成功后刷新列表
        refresh();
    }
}

void PasswordBookView::on_delete_clicked() {
    const int64_t id = selected_id();
    if (id == 0) return;

    const auto* entry = selected_entry();
    const QString name = entry
        ? QString::fromStdString(entry->website)
        : QStringLiteral("id=%1").arg(id);

    const auto answer = QMessageBox::question(
        this,
        QStringLiteral("确认删除"),
        QStringLiteral("确定要删除条目「%1」吗？此操作不可撤销。").arg(name),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (answer != QMessageBox::Yes) return;

    if (!client_) return;
    auto result = client_->remove_entry(id);
    if (result.ok()) {
        status_label_->setText(QStringLiteral("已删除条目「%1」。").arg(name));
        refresh();
    } else {
        const QString msg = QString::fromStdString(result.error().what());
        QMessageBox::warning(this, QStringLiteral("删除失败"),
            msg.isEmpty() ? QStringLiteral("删除条目失败。")
                          : QStringLiteral("删除失败：%1").arg(msg));
    }
}

void PasswordBookView::on_copy_password_clicked() {
    const int64_t id = selected_id();
    if (id == 0 || !client_) return;

    // 调用 get_entry 获取最新完整 entry（包含明文密码）
    auto result = client_->get_entry(id);
    if (result.ok()) {
        QApplication::clipboard()->setText(
            QString::fromStdString(result.value().entry.password));
        status_label_->setText(QStringLiteral("密码已复制到剪贴板。"));
    } else {
        const QString msg = QString::fromStdString(result.error().what());
        QMessageBox::warning(this, QStringLiteral("复制失败"),
            msg.isEmpty() ? QStringLiteral("获取密码失败。")
                          : QStringLiteral("复制失败：%1").arg(msg));
    }
}

}  // namespace pwdvault::ui
