// coding: utf-8
// =============================================================================
// MainWindow.h
//
// PwdVault 主窗口（新设计）。
//
// 布局：
//   - 左侧 232px 侧边栏：品牌 logo + 4 个导航项（密码本/录入/生成器/设置）
//     + 底部服务连接状态 + 锁定按钮
//   - 右侧内容区：
//       * 顶部 56px 顶栏：页面标题 + 条目数 badge + 主题切换 + 锁定按钮
//                          + 「新增」按钮（仅密码本视图显示）
//                          + 最小化 / 关闭 按钮（自定义窗口控制）
//       * 下方 QStackedWidget：4 个功能视图
//
// 窗口装饰：
//   - 无原生 Windows 标题栏（Qt::FramelessWindowHint）
//   - 拖动窗口顶部 56px 区域（顶栏空白处 + 侧边栏品牌区）可移动窗口；
//     保留 Windows Aero Snap（拖到屏幕边缘自动排列）与任务栏右键菜单
//   - 拖动窗口边缘不调整大小（固定尺寸，但允许 Aero Snap 最大化/还原）
//   - 双击顶部标题栏区域不切换最大化
//
// 启动流程：
//   - 查询 vault 状态：程序密码已启用且已锁定 → 显示 UnlockView（模态遮罩）
//   - 否则直接进入主界面（明文模式或已解锁）
//   - IPC 断开 → 弹窗提示并尝试重连
// =============================================================================
#pragma once

#include <QMainWindow>
#include <QString>

class QButtonGroup;
class QFrame;
class QLabel;
class QPushButton;
class QStackedWidget;

namespace pwdvault::ui {

class IpcClient;
class UnlockView;
class PasswordBookView;
class InputView;
class GeneratorView;
class SettingsView;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(IpcClient* client, QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    /// 处理 Windows 原生消息，用于无边框窗口的拖动 / Aero Snap / 任务栏右键菜单。
    bool nativeEvent(const QByteArray& eventType, void* message,
                     qintptr* result) override;

private slots:
    void on_nav_clicked(int row);
    void on_theme_toggle();
    void on_lock_clicked();
    void on_add_entry_clicked();
    void on_ipc_disconnected();
    void on_ipc_error(const QString& message);

    // 解锁流程
    void on_unlock_succeeded();
    void on_unlock_rejected();
    void on_lock_requested();

    // 程序密码状态变化（由 SettingsView 触发）
    void on_password_state_changed(bool enabled);

    // 视图间联动
    void on_entry_added(int64_t id);
    void on_password_generator_requested();
    void on_password_generated(const QString& password);
    void on_entry_count_changed(int count);

    // 自定义窗口控制
    void on_minimize_clicked();
    void on_close_clicked();

private:
    void build_sidebar(QWidget* parent);
    void build_topbar(QWidget* parent);
    void update_topbar_for_view(int row);
    void update_connection_status();
    void attempt_reconnect();

    /// 查询 vault 状态判断是否需要显示解锁视图。
    bool should_show_unlock() const;

    /// 显示解锁视图（模态）。
    void show_unlock();

    /// 切换到指定侧边栏视图。
    void switch_to_view(int row);

    /// 重新着色侧边栏导航图标（按选中状态 + 当前主题）。
    void refresh_nav_icons();

    /// 重新着色顶栏图标按钮（按当前主题）。
    void refresh_topbar_icons();

    IpcClient* client_;

    // 侧边栏
    QPushButton* nav_book_btn_ = nullptr;
    QPushButton* nav_input_btn_ = nullptr;
    QPushButton* nav_generator_btn_ = nullptr;
    QPushButton* nav_settings_btn_ = nullptr;
    QButtonGroup* nav_group_ = nullptr;
    QLabel* service_status_dot_ = nullptr;
    QLabel* service_status_label_ = nullptr;
    QPushButton* sidebar_lock_btn_ = nullptr;

    // 顶栏
    QFrame* topbar_frame_ = nullptr;          ///< 顶栏容器（用于 hit-test 判断）
    QLabel* topbar_title_ = nullptr;
    QLabel* topbar_subtitle_ = nullptr;
    QLabel* topbar_count_badge_ = nullptr;
    QPushButton* theme_toggle_btn_ = nullptr;
    QPushButton* topbar_lock_btn_ = nullptr;
    QPushButton* topbar_add_btn_ = nullptr;
    QPushButton* minimize_btn_ = nullptr;     ///< 自定义最小化按钮
    QPushButton* close_btn_ = nullptr;        ///< 自定义关闭按钮（hover 红色背景）

    // 内容区
    QStackedWidget* content_stack_ = nullptr;

    // 功能视图
    PasswordBookView* book_view_ = nullptr;
    InputView* input_view_ = nullptr;
    GeneratorView* generator_view_ = nullptr;
    SettingsView* settings_view_ = nullptr;

    // 解锁视图（模态层，按需创建/销毁）
    UnlockView* unlock_view_ = nullptr;

    // 标记生成器是否由 InputView 请求打开
    bool generator_from_input_ = false;
};

}  // namespace pwdvault::ui
