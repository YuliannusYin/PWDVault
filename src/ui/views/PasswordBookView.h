// coding: utf-8
// =============================================================================
// PasswordBookView.h
//
// PwdVault 密码本视图（新设计）。master-detail 布局：
//   - 顶部搜索行：搜索框（带 search 图标前缀）+ 字段下拉 + 刷新按钮
//   - 左侧 340px 列表：每条目 = 头像（首字母）+ 网站名 + 用户名 + chevron-right
//   - 右侧详情：头部（大头像 + 标题 + 副标题 + 复制/编辑/删除按钮）
//                + 字段网格（网站/用户名/密码（带可见性切换）/备注/时间）
//                + 外部链接按钮
//
// 进入视图时自动调用 list_entries 加载；条目数变化时 emit entry_count_changed。
// =============================================================================
#pragma once

#include <QWidget>

#include <vector>

#include "Types.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QScrollArea;

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

    /// 条目数量变化时触发，MainWindow 用于更新顶栏 badge。
    void entry_count_changed(int count);

private slots:
    void on_search_clicked();
    void on_refresh_clicked();
    void on_search_text_changed(const QString& text);
    void on_list_item_changed(QListWidgetItem* current, QListWidgetItem* previous);
    void on_copy_username_clicked();
    void on_copy_password_clicked();
    void on_toggle_password_clicked();
    void on_edit_clicked();
    void on_delete_clicked();
    void on_open_external_clicked();

private:
    void build_ui();
    void populate_list(const std::vector<core::PasswordEntry>& entries);
    void clear_detail();
    void load_detail(const core::PasswordEntry& entry);
    void set_detail_actions_enabled(bool enabled);
    const core::PasswordEntry* current_entry() const;
    void show_empty_state(const QString& message);

    IpcClient* client_;
    std::vector<core::PasswordEntry> entries_;
    int64_t current_id_ = 0;
    bool password_visible_ = false;

    // 顶部搜索行
    QLineEdit* search_edit_ = nullptr;
    QComboBox* field_combo_ = nullptr;
    QPushButton* refresh_button_ = nullptr;

    // 左侧列表
    QListWidget* list_ = nullptr;

    // 右侧详情
    QScrollArea* detail_scroll_ = nullptr;
    QWidget* detail_container_ = nullptr;
    QLabel* detail_avatar_ = nullptr;
    QLabel* detail_title_ = nullptr;
    QLabel* detail_subtitle_ = nullptr;
    QPushButton* copy_user_btn_ = nullptr;
    QPushButton* copy_pwd_btn_ = nullptr;
    QPushButton* edit_btn_ = nullptr;
    QPushButton* delete_btn_ = nullptr;

    QLabel* field_website_ = nullptr;
    QLabel* field_username_ = nullptr;
    QLabel* field_password_ = nullptr;
    QLabel* field_note_ = nullptr;
    QLabel* field_created_ = nullptr;
    QLabel* field_updated_ = nullptr;
    QLabel* strength_badge_ = nullptr;
    QPushButton* toggle_pwd_btn_ = nullptr;
    QPushButton* copy_website_btn_ = nullptr;
    QPushButton* copy_user_field_btn_ = nullptr;
    QPushButton* copy_pwd_field_btn_ = nullptr;
    QPushButton* open_external_btn_ = nullptr;

    // 空状态提示
    QLabel* empty_hint_ = nullptr;
};

}  // namespace pwdvault::ui
