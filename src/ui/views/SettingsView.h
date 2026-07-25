// coding: utf-8
// =============================================================================
// SettingsView.h
//
// PwdVault 设置视图（新设计）。
//
// 卡片分节布局（max 720px 居中）：
//   - 安全：程序密码状态 + 「管理程序密码」按钮 + 自动锁定下拉（占位）
//   - 外观：主题分段控件（浅色 / 深色 / 跟随系统）
//   - 存储：存储路径（mono） + 打开按钮 + 条目数量
//   - 关于：版本 / 加密方案 / 开源许可 / 项目主页
//   - 危险操作：锁定保险库按钮（红色边框）
//
// 程序密码管理通过 ProgramPasswordDialog（单 Tab 弹窗）完成。
// 锁定按钮调用 client->lock()，emit lock_requested 由 MainWindow 切回解锁视图。
// =============================================================================
#pragma once

#include <QColor>
#include <QWidget>

class QButtonGroup;
class QComboBox;
class QFrame;
class QLabel;
class QPushButton;
class QVBoxLayout;

namespace pwdvault::ui {

class IpcClient;

class SettingsView : public QWidget {
    Q_OBJECT
public:
    explicit SettingsView(IpcClient* client, QWidget* parent = nullptr);
    ~SettingsView() override;

signals:
    /// 用户点击「锁定」且 lock() 调用成功后触发，
    /// MainWindow 收到后显示 UnlockView。
    void lock_requested();

    /// 程序密码状态发生变化（启用/禁用）后触发，
    /// MainWindow 可据此调整锁定按钮可见性等。
    void password_state_changed(bool enabled);

public slots:
    /// 查询 service 当前 vault 状态并刷新 UI 显示。
    /// 应在视图显示前调用（如主窗口启动后、解锁成功后）。
    void refresh_status();

private slots:
    void on_manage_password_clicked();
    void on_open_storage_clicked();
    void on_view_license_clicked();
    void on_open_github_clicked();
    void on_lock_now_clicked();
    void on_theme_segment_clicked(int idx);

private:
    void build_ui();
    /// 构建「卡片分节」式 section 容器。
    /// \param icon_resource qrc 中 section 标题图标的路径（如 ":/icons/shield-check.svg"）
    /// \param title         section 标题
    /// \param danger        是否使用危险边框样式（红色边框）
    /// \param parent        父 widget（section 将作为子控件）
    /// \return 返回 section（已设置 layout，调用者用 layout() 来 add_row）
    QFrame* make_section(const QString& icon_resource, const QString& title,
                         bool danger, QWidget* parent,
                         const QColor& icon_color = QColor());

    /// 在 section 内追加一行（左：标题 + 描述；右：自定义 widget）。
    /// \param out_desc  非空时，回传创建的描述 QLabel 指针（供后续动态更新文本）
    void add_row(QVBoxLayout* section_layout, const QString& title,
                 const QString& description, QWidget* right_widget,
                 QLabel** out_desc = nullptr);

    /// 主题分段控件：浅色 / 深色 / 跟随系统
    QFrame* build_theme_segmented();

    /// 刷新主题分段控件选中状态（不触发信号）
    void sync_theme_segment();

    /// 刷新程序密码状态显示（badge 文本与样式）
    void refresh_password_badge();

    /// 刷新条目数量显示
    void refresh_entry_count();

    IpcClient* client_;

    // 安全区
    QLabel* pp_badge_ = nullptr;          // 「已启用 / 未启用」徽章
    QLabel* pp_desc_ = nullptr;           // 副标题
    QPushButton* manage_pp_btn_ = nullptr;
    QComboBox* autolock_combo_ = nullptr;

    // 外观区
    QButtonGroup* theme_group_ = nullptr;
    QPushButton* theme_light_btn_ = nullptr;
    QPushButton* theme_dark_btn_ = nullptr;
    QPushButton* theme_system_btn_ = nullptr;

    // 存储区
    QLabel* storage_path_label_ = nullptr;
    QPushButton* open_storage_btn_ = nullptr;
    QLabel* entry_count_label_ = nullptr;

    // 关于区
    QLabel* version_value_ = nullptr;
    QPushButton* license_btn_ = nullptr;
    QPushButton* github_btn_ = nullptr;

    // 危险操作区
    QPushButton* lock_now_btn_ = nullptr;

    /// 缓存当前程序密码是否已启用。
    bool password_enabled_ = false;
};

}  // namespace pwdvault::ui
