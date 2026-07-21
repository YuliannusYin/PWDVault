// coding: utf-8
// =============================================================================
// PasswordBookView.h
//
// PwdVault 密码本视图。显示密码条目列表，支持搜索、查看详情、编辑、删除、
// 复制密码。进入视图时自动调用 list_entries 加载。
//
// 布局：
//   - 顶部：搜索框 + 字段下拉 + 搜索按钮 + 刷新按钮
//   - 中间：QTableWidget（网站 / 用户名 / 备注 / 更新时间）
//   - 底部：按钮栏（查看详情 / 编辑 / 删除 / 复制密码）
// =============================================================================
#pragma once

#include <QWidget>

#include <vector>

#include "Types.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;

namespace pwdvault::ui {

class IpcClient;

class PasswordBookView : public QWidget {
    Q_OBJECT
public:
    explicit PasswordBookView(IpcClient* client, QWidget* parent = nullptr);
    ~PasswordBookView() override;

    /// 重新加载列表（调用 list_entries）。
    void refresh();

signals:
    /// 编辑条目时触发，MainWindow 可用于切换视图或刷新。
    void entry_updated(int64_t id);

private slots:
    void on_search_clicked();
    void on_refresh_clicked();
    void on_item_selection_changed();
    void on_item_double_clicked(int row, int column);
    void on_view_detail_clicked();
    void on_edit_clicked();
    void on_delete_clicked();
    void on_copy_password_clicked();

private:
    void build_ui();
    void populate_table(const std::vector<core::PasswordEntry>& entries);
    void set_buttons_enabled(bool enabled);
    int64_t selected_id() const;
    const core::PasswordEntry* selected_entry() const;
    void show_detail_dialog(const core::PasswordEntry& entry);

    IpcClient* client_;
    std::vector<core::PasswordEntry> entries_;

    QLineEdit* search_edit_ = nullptr;
    QComboBox* field_combo_ = nullptr;
    QPushButton* search_button_ = nullptr;
    QPushButton* refresh_button_ = nullptr;
    QTableWidget* table_ = nullptr;
    QPushButton* view_detail_button_ = nullptr;
    QPushButton* edit_button_ = nullptr;
    QPushButton* delete_button_ = nullptr;
    QPushButton* copy_password_button_ = nullptr;
    QLabel* status_label_ = nullptr;
};

}  // namespace pwdvault::ui
