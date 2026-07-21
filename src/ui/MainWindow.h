// coding: utf-8
// =============================================================================
// MainWindow.h
//
// PwdVault 主窗口。侧边栏 + 内容区布局（类似火绒主界面）：
//   - 左侧 QListWidget 侧边栏：密码本 / 录入 / 生成器 / 设置
//   - 右侧 QStackedWidget：4 个功能视图
//   - 顶部菜单栏：文件（退出）、帮助（关于）
//   - 底部状态栏：连接状态 + 最后操作时间
//   - 启动时查询 vault 状态：若程序密码已启用且已锁定，则显示 UnlockView
//   - 程序密码未启用时直接进入主界面（明文模式）
//   - 当 IPC 断开时弹窗提示并尝试重连
// =============================================================================
#pragma once

#include <QMainWindow>
#include <QString>

class QLabel;
class QListWidget;
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

private slots:
    void on_sidebar_item_changed(int row);
    void on_about_triggered();
    void on_quit_triggered();
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

private:
    void build_sidebar();
    void build_menu();
    void build_status_bar();
    void update_connection_status();
    void update_last_op_time();
    void attempt_reconnect();

    /// 查询 vault 状态判断是否需要显示解锁视图。
    /// 程序密码已启用且密码库已锁定时返回 true。
    bool should_show_unlock() const;

    /// 显示解锁视图（模态对话框）。
    void show_unlock();

    /// 切换到指定侧边栏视图。
    void switch_to_view(int row);

    IpcClient* client_;

    // 侧边栏与内容区
    QListWidget* sidebar_ = nullptr;
    QStackedWidget* content_stack_ = nullptr;

    // 功能视图
    PasswordBookView* book_view_ = nullptr;
    InputView* input_view_ = nullptr;
    GeneratorView* generator_view_ = nullptr;
    SettingsView* settings_view_ = nullptr;

    // 解锁视图（模态层，按需创建/销毁）
    UnlockView* unlock_view_ = nullptr;

    // 标记生成器是否由 InputView 请求打开（用于生成后自动切回录入视图）
    bool generator_from_input_ = false;

    // 状态栏
    QLabel* status_label_ = nullptr;
    QLabel* last_op_label_ = nullptr;
};

}  // namespace pwdvault::ui
