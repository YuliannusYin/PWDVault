// coding: utf-8
// =============================================================================
// MainWindow.cpp
//
// PwdVault 主窗口实现。构建侧边栏 + 内容区，处理菜单、状态栏、解锁流程、
// 视图间联动与 IPC 断连。
// =============================================================================
#include "MainWindow.h"
#include "IpcClient.h"
#include "views/GeneratorView.h"
#include "views/InputView.h"
#include "views/UnlockView.h"
#include "views/PasswordBookView.h"
#include "views/SettingsView.h"

#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QFont>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QStackedWidget>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

namespace pwdvault::ui {

// 侧边栏条目索引
namespace {
constexpr int kSidebarBook = 0;
constexpr int kSidebarInput = 1;
constexpr int kSidebarGenerator = 2;
constexpr int kSidebarSettings = 3;
}  // namespace

MainWindow::MainWindow(IpcClient* client, QWidget* parent)
    : QMainWindow(parent), client_(client)
{
    setWindowTitle(QStringLiteral("PwdVault - 密码管理器"));
    setMinimumSize(800, 500);
    resize(900, 600);

    // 中心区：左侧 sidebar + 右侧 stacked content
    auto* central = new QWidget(this);
    auto* layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    sidebar_ = new QListWidget(central);
    sidebar_->setObjectName(QStringLiteral("sidebar"));
    sidebar_->setFixedWidth(160);
    QFont sidebar_font = sidebar_->font();
    sidebar_font.setPointSize(11);
    sidebar_->setFont(sidebar_font);
    layout->addWidget(sidebar_);

    content_stack_ = new QStackedWidget(central);
    layout->addWidget(content_stack_, 1);

    setCentralWidget(central);

    build_sidebar();
    build_menu();
    build_status_bar();

    // 信号槽：侧边栏切换 → 切换内容区
    connect(sidebar_, &QListWidget::currentRowChanged,
            this, &MainWindow::on_sidebar_item_changed);
    sidebar_->setCurrentRow(0);

    if (client_) {
        connect(client_, &IpcClient::disconnected,
                this, &MainWindow::on_ipc_disconnected);
        connect(client_, &IpcClient::error_occurred,
                this, &MainWindow::on_ipc_error);
    }

    update_connection_status();

    // 启动流程：查询 vault 状态，若程序密码已启用且已锁定则显示解锁视图
    if (should_show_unlock()) {
        show_unlock();
    } else {
        // 明文模式或已解锁：直接进入主界面
        if (book_view_) book_view_->refresh();
        if (settings_view_) settings_view_->refresh_status();
    }
}

MainWindow::~MainWindow() = default;

// ---------------------------------------------------------------------------
// 子组件构建
// ---------------------------------------------------------------------------

void MainWindow::build_sidebar() {
    // 侧边栏 4 个条目
    sidebar_->addItem(QStringLiteral("密码本"));
    sidebar_->addItem(QStringLiteral("录入"));
    sidebar_->addItem(QStringLiteral("生成器"));
    sidebar_->addItem(QStringLiteral("设置"));

    // 创建 4 个功能视图并加入 stacked widget
    book_view_ = new PasswordBookView(client_, this);
    input_view_ = new InputView(client_, this);
    generator_view_ = new GeneratorView(client_, this);
    settings_view_ = new SettingsView(client_, this);

    content_stack_->addWidget(book_view_);        // index 0
    content_stack_->addWidget(input_view_);       // index 1
    content_stack_->addWidget(generator_view_);   // index 2
    content_stack_->addWidget(settings_view_);    // index 3

    // 视图间信号联动
    // InputView: 新增条目 → 刷新密码本 + 切换到密码本视图
    connect(input_view_, &InputView::entry_added,
            this, &MainWindow::on_entry_added);
    // InputView: 请求生成器 → 切换到生成器视图
    connect(input_view_, &InputView::password_generator_requested,
            this, &MainWindow::on_password_generator_requested);
    // GeneratorView: 生成密码 → 传给 InputView（若由 InputView 请求则切回）
    connect(generator_view_, &GeneratorView::password_generated,
            this, &MainWindow::on_password_generated);
    // SettingsView: 锁定 → 显示解锁视图
    connect(settings_view_, &SettingsView::lock_requested,
            this, &MainWindow::on_lock_requested);
    // SettingsView: 程序密码状态变化 → 通知 MainWindow
    connect(settings_view_, &SettingsView::password_state_changed,
            this, &MainWindow::on_password_state_changed);
    // PasswordBookView: 编辑成功 → 通知 MainWindow（当前仅刷新，未来可扩展）
    connect(book_view_, &PasswordBookView::entry_updated,
            this, [this](int64_t) { update_last_op_time(); });
}

void MainWindow::build_menu() {
    auto* file_menu = menuBar()->addMenu(QStringLiteral("文件(&F)"));
    auto* quit_action = file_menu->addAction(QStringLiteral("退出(&X)"));
    quit_action->setShortcut(QKeySequence::Quit);
    connect(quit_action, &QAction::triggered, this, &MainWindow::on_quit_triggered);

    auto* help_menu = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
    auto* about_action = help_menu->addAction(QStringLiteral("关于(&A)"));
    connect(about_action, &QAction::triggered, this, &MainWindow::on_about_triggered);
}

void MainWindow::build_status_bar() {
    status_label_ = new QLabel;
    last_op_label_ = new QLabel;
    statusBar()->addWidget(status_label_, 1);
    statusBar()->addPermanentWidget(last_op_label_);
}

// ---------------------------------------------------------------------------
// 状态更新
// ---------------------------------------------------------------------------

void MainWindow::update_connection_status() {
    if (client_ && client_->is_connected()) {
        status_label_->setText(QStringLiteral("● 已连接"));
        status_label_->setStyleSheet(QStringLiteral("color: green;"));
    } else {
        status_label_->setText(QStringLiteral("● 未连接"));
        status_label_->setStyleSheet(QStringLiteral("color: red;"));
    }
    update_last_op_time();
}

void MainWindow::update_last_op_time() {
    const QString now = QDateTime::currentDateTime().toString(
        QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    last_op_label_->setText(QStringLiteral("最后操作：%1").arg(now));
}

void MainWindow::attempt_reconnect() {
    if (!client_) return;
    if (client_->connect_to_service()) {
        update_connection_status();
        // 重连成功后检查 vault 状态：若已锁定则显示解锁视图
        if (should_show_unlock()) {
            QMessageBox::information(this, QStringLiteral("重连"),
                QStringLiteral("已重新连接到 service，但密码库已锁定，请输入程序密码解锁。"));
            show_unlock();
            return;
        }
        if (book_view_) book_view_->refresh();
        if (settings_view_) settings_view_->refresh_status();
        QMessageBox::information(this, QStringLiteral("重连"),
            QStringLiteral("已重新连接到 service。"));
    } else {
        update_connection_status();
        QMessageBox::warning(this, QStringLiteral("重连失败"),
            QStringLiteral("无法连接到 service，请稍后重试或手动启动 service。"));
    }
}

// ---------------------------------------------------------------------------
// 解锁流程
// ---------------------------------------------------------------------------

bool MainWindow::should_show_unlock() const {
    if (!client_ || !client_->is_connected()) return false;
    auto result = client_->get_vault_status();
    if (!result.ok()) return false;
    // 程序密码已启用且密码库已锁定时才需要显示解锁视图
    return result.value().password_enabled && result.value().is_locked;
}

void MainWindow::show_unlock() {
    // 清理旧的 UnlockView
    if (unlock_view_) {
        unlock_view_->hide();
        unlock_view_->deleteLater();
        unlock_view_ = nullptr;
    }

    unlock_view_ = new UnlockView(client_, this);
    connect(unlock_view_, &UnlockView::unlock_succeeded,
            this, &MainWindow::on_unlock_succeeded);
    connect(unlock_view_, &UnlockView::rejected,
            this, &MainWindow::on_unlock_rejected);
    unlock_view_->show();
}

void MainWindow::on_unlock_succeeded() {
    if (unlock_view_) {
        unlock_view_->hide();
        unlock_view_->deleteLater();
        unlock_view_ = nullptr;
    }
    // 刷新密码本视图（加载条目列表）
    if (book_view_) book_view_->refresh();
    // 刷新设置视图的程序密码状态显示
    if (settings_view_) settings_view_->refresh_status();
    // 确保主窗口可见并处于前台
    show();
    raise();
    activateWindow();
    update_connection_status();
}

void MainWindow::on_unlock_rejected() {
    // 用户关闭解锁对话框但未解锁成功 → 退出应用
    QApplication::quit();
}

void MainWindow::on_lock_requested() {
    // 隐藏主界面，显示解锁视图
    hide();
    show_unlock();
}

void MainWindow::on_password_state_changed(bool enabled) {
    // 程序密码状态变化：刷新设置视图（按钮可见性已由 SettingsView 内部处理）
    // 此处可用于主窗口层面的状态联动（如菜单项可用性等）
    (void)enabled;
    if (settings_view_) settings_view_->refresh_status();
}

// ---------------------------------------------------------------------------
// 视图间联动
// ---------------------------------------------------------------------------

void MainWindow::on_entry_added(int64_t id) {
    (void)id;
    // 刷新密码本并切换到密码本视图
    if (book_view_) book_view_->refresh();
    switch_to_view(kSidebarBook);
}

void MainWindow::on_password_generator_requested() {
    // 标记请求来源为 InputView，生成后自动切回
    generator_from_input_ = (content_stack_->currentWidget() == input_view_);
    switch_to_view(kSidebarGenerator);
}

void MainWindow::on_password_generated(const QString& password) {
    // 将生成的密码传给 InputView
    if (input_view_) input_view_->set_password(password);
    // 若由 InputView 请求打开生成器，自动切回录入视图
    if (generator_from_input_) {
        generator_from_input_ = false;
        switch_to_view(kSidebarInput);
    }
}

void MainWindow::switch_to_view(int row) {
    if (sidebar_) sidebar_->setCurrentRow(row);
}

// ---------------------------------------------------------------------------
// 槽函数
// ---------------------------------------------------------------------------

void MainWindow::on_sidebar_item_changed(int row) {
    if (row < 0 || row >= content_stack_->count()) return;
    content_stack_->setCurrentIndex(row);
    // 进入密码本视图时自动刷新
    if (row == kSidebarBook && book_view_) {
        book_view_->refresh();
    }
    // 进入设置视图时刷新程序密码状态
    if (row == kSidebarSettings && settings_view_) {
        settings_view_->refresh_status();
    }
    update_last_op_time();
}

void MainWindow::on_about_triggered() {
    QMessageBox::about(this, QStringLiteral("关于 PwdVault"),
        QStringLiteral(
            "<h3>PwdVault - 密码管理器</h3>"
            "<p>本地存储、加密保护的密码管理工具。</p>"
            "<p>版本：0.1.0</p>"
        ));
}

void MainWindow::on_quit_triggered() {
    QApplication::quit();
}

void MainWindow::on_ipc_disconnected() {
    update_connection_status();
    const auto answer = QMessageBox::warning(
        this,
        QStringLiteral("连接断开"),
        QStringLiteral("与 service 的连接已断开。是否尝试重连？"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::Yes);
    if (answer == QMessageBox::Yes) {
        attempt_reconnect();
    }
}

void MainWindow::on_ipc_error(const QString& message) {
    statusBar()->showMessage(message, 5000);
}

}  // namespace pwdvault::ui
