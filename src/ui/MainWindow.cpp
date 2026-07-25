// coding: utf-8
// =============================================================================
// MainWindow.cpp
//
// PwdVault 主窗口实现。构建 232px 侧边栏 + 56px 顶栏 + 内容区，
// 处理解锁流程、视图间联动、IPC 断连与主题切换。
// =============================================================================
#include "MainWindow.h"
#include "IconKit.h"
#include "IpcClient.h"
#include "Theme.h"
#include "views/GeneratorView.h"
#include "views/InputView.h"
#include "views/UnlockView.h"
#include "views/PasswordBookView.h"
#include "views/SettingsView.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QString>
#include <QStyle>
#include <QVBoxLayout>
#include <QWidget>

#if defined(Q_OS_WIN)
#  include <qt_windows.h>
#  include <windowsx.h>
#  include <dwmapi.h>
#  pragma comment(lib, "dwmapi.lib")
#endif

namespace pwdvault::ui {

#if defined(Q_OS_WIN)
namespace {
/// Windows 11 DWM 圆角偏好常量（DWMWINDOWATTRACON.corner_preference）。
/// Win10 头文件中无此定义，这里手写避免编译失败。
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
constexpr DWORD DWMWA_WINDOW_CORNER_PREFERENCE = 33;
#endif
/// DWM_WINDOW_CORNER_PREFERENCE 枚举值。
constexpr int DWMWCP_DEFAULT    = 0;
constexpr int DWMWCP_DONOTROUND = 1;
constexpr int DWMWCP_ROUND      = 2;
constexpr int DWMWCP_ROUNDSMALL = 3;

/// 启用 Windows 11 原生窗口圆角（系统默认半径，约 8px）。
/// Win10 上调用会失败但不报错，自动退化为直角。
void enable_win11_rounded_corners(HWND hwnd) {
    if (!hwnd) return;
    const int preference = DWMWCP_ROUND;
    ::DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE,
                            &preference, sizeof(preference));
}
}  // namespace
#endif

// 侧边栏条目索引
namespace {
constexpr int kSidebarBook = 0;
constexpr int kSidebarInput = 1;
constexpr int kSidebarGenerator = 2;
constexpr int kSidebarSettings = 3;

/// 创建带左侧高亮指示条的导航按钮。
/// 图标由 MainWindow::refresh_nav_icons() 统一着色设置，此处不设图标。
QPushButton* make_nav_button(const QString& text, QWidget* parent) {
    auto* btn = new QPushButton(parent);
    btn->setCheckable(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setText(text);
    btn->setIconSize(QSize(18, 18));
    btn->setProperty("cssClass", QStringLiteral("navItem"));
    // 样式由 QSS 中 QPushButton[cssClass="navItem"] 提供
    return btn;
}

/// 创建 36x36 图标按钮。
/// 图标由 MainWindow::refresh_topbar_icons() 统一着色设置，此处不设图标。
QPushButton* make_icon_button(const QString& tooltip, QWidget* parent) {
    auto* btn = new QPushButton(parent);
    btn->setIconSize(QSize(18, 18));
    btn->setCursor(Qt::PointingHandCursor);
    btn->setToolTip(tooltip);
    btn->setProperty("cssClass", QStringLiteral("icon"));
    btn->setFixedSize(36, 36);
    return btn;
}

}  // namespace

MainWindow::MainWindow(IpcClient* client, QWidget* parent)
    : QMainWindow(parent), client_(client)
{
    // 自定义无边框窗口：移除 Windows 原生标题栏/边框，但保留任务栏图标、
    // Aero Snap（通过 nativeEvent 中的 WM_NCHITTEST 实现）和右键菜单。
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    setWindowTitle(QStringLiteral("PwdVault - 密码管理器"));
    // 固定默认尺寸 1200x800。「固定大小」通过 WM_NCHITTEST 阻止边缘调整实现，
    // 而非 setFixedSize / setMaximumSize —— 后两者会一并禁用 Aero Snap 最大化。
    // 这样：① 不能拖边缘调整大小 ② 拖顶栏到屏幕顶部仍可 Aero Snap 最大化。
    setMinimumSize(1200, 800);
    resize(1200, 800);

    // 中央区：左侧 sidebar + 右侧 (顶栏 + 内容区)
    auto* central = new QWidget(this);
    auto* root_layout = new QHBoxLayout(central);
    root_layout->setContentsMargins(0, 0, 0, 0);
    root_layout->setSpacing(0);

    // ── 侧边栏 ──
    auto* sidebar_frame = new QFrame(central);
    sidebar_frame->setObjectName(QStringLiteral("sidebarFrame"));
    sidebar_frame->setProperty("cssClass", QStringLiteral("sidebar"));
    sidebar_frame->setFixedWidth(232);
    auto* sidebar_layout = new QVBoxLayout(sidebar_frame);
    sidebar_layout->setContentsMargins(0, 0, 0, 0);
    sidebar_layout->setSpacing(0);
    build_sidebar(sidebar_frame);
    root_layout->addWidget(sidebar_frame);

    // ── 右侧主区 ──
    auto* main_frame = new QFrame(central);
    main_frame->setObjectName(QStringLiteral("mainFrame"));
    auto* main_layout = new QVBoxLayout(main_frame);
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->setSpacing(0);

    build_topbar(main_frame);

    content_stack_ = new QStackedWidget(main_frame);
    content_stack_->setContentsMargins(0, 0, 0, 0);
    main_layout->addWidget(content_stack_, 1);

    root_layout->addWidget(main_frame, 1);

    setCentralWidget(central);

    // 创建 4 个功能视图并加入 stacked widget
    book_view_ = new PasswordBookView(client_, this);
    input_view_ = new InputView(client_, this);
    generator_view_ = new GeneratorView(client_, this);
    settings_view_ = new SettingsView(client_, this);

    content_stack_->addWidget(book_view_);        // index 0
    content_stack_->addWidget(input_view_);       // index 1
    content_stack_->addWidget(generator_view_);   // index 2
    content_stack_->addWidget(settings_view_);    // index 3

    // 默认选中密码本
    nav_book_btn_->setChecked(true);
    content_stack_->setCurrentIndex(kSidebarBook);
    update_topbar_for_view(kSidebarBook);

    // 视图间信号联动
    connect(input_view_, &InputView::entry_added,
            this, &MainWindow::on_entry_added);
    connect(input_view_, &InputView::password_generator_requested,
            this, &MainWindow::on_password_generator_requested);
    connect(generator_view_, &GeneratorView::password_generated,
            this, &MainWindow::on_password_generated);
    connect(settings_view_, &SettingsView::lock_requested,
            this, &MainWindow::on_lock_requested);
    connect(settings_view_, &SettingsView::password_state_changed,
            this, &MainWindow::on_password_state_changed);
    connect(book_view_, &PasswordBookView::entry_updated,
            this, [this](int64_t) { /* 预留 */ });
    connect(book_view_, &PasswordBookView::entry_count_changed,
            this, &MainWindow::on_entry_count_changed);

    if (client_) {
        connect(client_, &IpcClient::disconnected,
                this, &MainWindow::on_ipc_disconnected);
        connect(client_, &IpcClient::error_occurred,
                this, &MainWindow::on_ipc_error);
    }

#if defined(Q_OS_WIN)
    // 启用 Windows 11 原生窗口圆角（Win10 自动退化为直角）。
    // 必须在窗口创建后调用，所以放在构造函数末尾。
    enable_win11_rounded_corners(reinterpret_cast<HWND>(winId()));
#endif

    update_connection_status();
    // 初始着色所有图标（导航 + 顶栏），需在按钮创建后调用
    refresh_nav_icons();
    refresh_topbar_icons();

    // 启动流程：查询 vault 状态，若程序密码已启用且已锁定则显示解锁视图
    if (should_show_unlock()) {
        show_unlock();
    } else {
        if (book_view_) book_view_->refresh();
        if (settings_view_) settings_view_->refresh_status();
    }
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent* event) {
    // 关闭时若有解锁视图，一起清理
    if (unlock_view_) {
        unlock_view_->deleteLater();
        unlock_view_ = nullptr;
    }
    QMainWindow::closeEvent(event);
}

bool MainWindow::nativeEvent(const QByteArray& eventType, void* message,
                              qintptr* result) {
#if defined(Q_OS_WIN)
    if (eventType == "windows_generic_MSG" && message) {
        MSG* msg = static_cast<MSG*>(message);

        // 处理 WM_NCHITTEST：让窗口顶部 56 DIP 区域（顶栏空白处 + 侧边栏
        // 品牌区）返回 HTCAPTION，系统会把它当作标题栏 —— 这样拖动可移动
        // 窗口，且自动支持 Aero Snap（拖到屏幕边缘排列）和任务栏右键菜单。
        // 边缘不返回 HTLEFT/HTRIGHT/HTTOP/HTBOTTOM 等，因此不能拖边调整大小。
        if (msg->message == WM_NCHITTEST) {
            const LONG x = GET_X_LPARAM(msg->lParam);
            const LONG y = GET_Y_LPARAM(msg->lParam);

            // 使用 Win32 ScreenToClient 获取窗口客户区坐标（物理像素），比 Qt 的
            // mapFromGlobal 更可靠：后者依赖 Qt 内部缓存的窗口几何信息，在窗口
            // 状态切换（最大化/还原/Aero Snap）期间可能返回过时坐标，导致命中测试
            // 偶尔返回 HTCLIENT 而非 HTCAPTION，使拖拽无法启动。
            POINT pt{ x, y };
            if (msg->hwnd && ::ScreenToClient(msg->hwnd, &pt)) {
                const qreal dpr = devicePixelRatioF();
                if (dpr > 0) {
                    // 转换为设备无关像素（DIP），与 Qt widget 坐标一致
                    const qreal dip_x = pt.x / dpr;
                    const qreal dip_y = pt.y / dpr;

                    // 标题栏区域 = 窗口顶部 56 DIP（顶栏与侧边栏品牌区等高）
                    if (dip_y >= 0 && dip_y < 56) {
                        if (dip_x < 232) {
                            // 侧边栏品牌区域：无按钮，始终可拖拽
                            *result = HTCAPTION;
                            return true;
                        }
                        // 顶栏区域：检查鼠标下方是否为按钮（QPushButton），
                        // 是则交给 Qt 处理点击；否则视为标题栏。
                        if (topbar_frame_) {
                            const QPoint topbar_local(
                                static_cast<int>(dip_x - 232),
                                static_cast<int>(dip_y));
                            QWidget* hit = topbar_frame_->childAt(topbar_local);
                            bool on_button = false;
                            while (hit && hit != topbar_frame_) {
                                if (qobject_cast<QPushButton*>(hit)) {
                                    on_button = true;
                                    break;
                                }
                                hit = hit->parentWidget();
                            }
                            if (!on_button) {
                                *result = HTCAPTION;
                                return true;
                            }
                        }
                    }
                }
            }
            // 其他区域（含顶栏按钮）：交给 Qt 默认处理（视为 client area）
            return false;
        }

        // 阻止双击顶部标题栏区域自动最大化（用户选择「双击不切换最大化」）
        if (msg->message == WM_NCLBUTTONDBLCLK) {
            if (msg->wParam == HTCAPTION) {
                *result = 0;
                return true;
            }
        }
    }
#else
    Q_UNUSED(eventType); Q_UNUSED(message); Q_UNUSED(result);
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}

void MainWindow::on_minimize_clicked() {
    showMinimized();
}

void MainWindow::on_close_clicked() {
    close();
}

// ---------------------------------------------------------------------------
// 子组件构建
// ---------------------------------------------------------------------------

void MainWindow::build_sidebar(QWidget* parent) {
    auto* layout = qobject_cast<QVBoxLayout*>(parent->layout());
    if (!layout) {
        layout = new QVBoxLayout(parent);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
    }

    // 品牌块
    auto* brand_frame = new QFrame(parent);
    brand_frame->setFixedHeight(56);
    auto* brand_layout = new QHBoxLayout(brand_frame);
    // 左 26 / 上 16：logo + name 稍往右下偏移，与顶栏标题对齐更协调
    brand_layout->setContentsMargins(26, 16, 18, 0);
    brand_layout->setSpacing(10);

    auto* brand_icon = new QLabel(brand_frame);
    brand_icon->setPixmap(QIcon(QStringLiteral(":/logo.png"))
                              .pixmap(QSize(28, 28)));
    brand_layout->addWidget(brand_icon);

    auto* brand_label = new QLabel(QStringLiteral("PwdVault"), brand_frame);
    brand_label->setProperty("cssClass", QStringLiteral("brand"));
    brand_layout->addWidget(brand_label);
    brand_layout->addStretch(1);

    layout->addWidget(brand_frame);

    // 导航
    auto* nav_frame = new QFrame(parent);
    nav_frame->setProperty("cssClass", QStringLiteral("navContainer"));
    auto* nav_layout = new QVBoxLayout(nav_frame);
    nav_layout->setContentsMargins(12, 12, 12, 12);
    nav_layout->setSpacing(4);

    nav_group_ = new QButtonGroup(this);
    nav_group_->setExclusive(true);

    nav_book_btn_ = make_nav_button(
        QStringLiteral("密码本"), nav_frame);
    nav_input_btn_ = make_nav_button(
        QStringLiteral("录入"), nav_frame);
    nav_generator_btn_ = make_nav_button(
        QStringLiteral("生成器"), nav_frame);
    nav_settings_btn_ = make_nav_button(
        QStringLiteral("设置"), nav_frame);

    nav_group_->addButton(nav_book_btn_, kSidebarBook);
    nav_group_->addButton(nav_input_btn_, kSidebarInput);
    nav_group_->addButton(nav_generator_btn_, kSidebarGenerator);
    nav_group_->addButton(nav_settings_btn_, kSidebarSettings);

    nav_layout->addWidget(nav_book_btn_);
    nav_layout->addWidget(nav_input_btn_);
    nav_layout->addWidget(nav_generator_btn_);
    nav_layout->addWidget(nav_settings_btn_);
    nav_layout->addStretch(1);

    layout->addWidget(nav_frame, 1);

    // 底部状态 + 锁定
    auto* bottom_frame = new QFrame(parent);
    auto* bottom_layout = new QVBoxLayout(bottom_frame);
    bottom_layout->setContentsMargins(18, 16, 18, 16);
    bottom_layout->setSpacing(12);

    auto* status_row = new QHBoxLayout();
    status_row->setSpacing(8);
    service_status_dot_ = new QLabel(bottom_frame);
    service_status_dot_->setFixedSize(8, 8);
    service_status_dot_->setProperty("cssClass", QStringLiteral("statusDotOk"));
    status_row->addWidget(service_status_dot_);

    service_status_label_ = new QLabel(
        QStringLiteral("服务已连接"), bottom_frame);
    service_status_label_->setProperty("cssClass", QStringLiteral("caption"));
    status_row->addWidget(service_status_label_);
    status_row->addStretch(1);
    bottom_layout->addLayout(status_row);

    sidebar_lock_btn_ = new QPushButton(bottom_frame);
    sidebar_lock_btn_->setIconSize(QSize(16, 16));
    sidebar_lock_btn_->setText(QStringLiteral("锁定保险库"));
    sidebar_lock_btn_->setCursor(Qt::PointingHandCursor);
    sidebar_lock_btn_->setFixedHeight(40);
    sidebar_lock_btn_->setProperty("cssClass", QStringLiteral("outline"));
    bottom_layout->addWidget(sidebar_lock_btn_);

    layout->addWidget(bottom_frame);

    // 信号槽
    connect(nav_group_, &QButtonGroup::idClicked,
            this, &MainWindow::on_nav_clicked);
    connect(sidebar_lock_btn_, &QPushButton::clicked,
            this, &MainWindow::on_lock_clicked);
}

void MainWindow::build_topbar(QWidget* parent) {
    auto* layout = qobject_cast<QVBoxLayout*>(parent->layout());
    if (!layout) {
        layout = new QVBoxLayout(parent);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
    }

    topbar_frame_ = new QFrame(parent);
    topbar_frame_->setObjectName(QStringLiteral("topbarFrame"));
    topbar_frame_->setProperty("cssClass", QStringLiteral("topbar"));
    topbar_frame_->setFixedHeight(56);
    auto* topbar_layout = new QHBoxLayout(topbar_frame_);
    // 右侧留出窗口控制按钮位置（最小化/关闭按钮直接贴右边缘）
    topbar_layout->setContentsMargins(24, 0, 8, 0);
    topbar_layout->setSpacing(12);

    // 左侧：标题 + 副标题
    topbar_title_ = new QLabel(topbar_frame_);
    topbar_title_->setProperty("cssClass", QStringLiteral("title"));
    topbar_layout->addWidget(topbar_title_);

    topbar_subtitle_ = new QLabel(topbar_frame_);
    topbar_subtitle_->setProperty("cssClass", QStringLiteral("muted"));
    topbar_layout->addWidget(topbar_subtitle_);

    // 条目数 badge
    topbar_count_badge_ = new QLabel(topbar_frame_);
    topbar_count_badge_->setProperty("cssClass", QStringLiteral("badge"));
    topbar_count_badge_->setAlignment(Qt::AlignCenter);
    topbar_count_badge_->hide();
    topbar_layout->addWidget(topbar_count_badge_);

    topbar_layout->addStretch(1);

    // 右侧：主题切换 + 锁定 + 新增
    theme_toggle_btn_ = make_icon_button(
        QStringLiteral("切换主题"), topbar_frame_);
    topbar_layout->addWidget(theme_toggle_btn_);

    topbar_lock_btn_ = make_icon_button(
        QStringLiteral("锁定保险库"), topbar_frame_);
    topbar_layout->addWidget(topbar_lock_btn_);

    topbar_add_btn_ = new QPushButton(topbar_frame_);
    topbar_add_btn_->setIconSize(QSize(16, 16));
    topbar_add_btn_->setText(QStringLiteral("新增"));
    topbar_add_btn_->setCursor(Qt::PointingHandCursor);
    topbar_add_btn_->setFixedHeight(40);
    topbar_add_btn_->setProperty("cssClass", QStringLiteral("primary"));
    topbar_layout->addWidget(topbar_add_btn_);

    // ── 窗口控制按钮（贴右边缘） ──
    // 与功能按钮之间留 4px 间隔
    topbar_layout->addSpacing(4);

    minimize_btn_ = make_icon_button(
        QStringLiteral("最小化"), topbar_frame_);
    topbar_layout->addWidget(minimize_btn_);

    close_btn_ = make_icon_button(
        QStringLiteral("关闭"), topbar_frame_);
    // 关闭按钮 hover 时使用红色背景，更接近 Windows 原生体验
    close_btn_->setProperty("cssClass", QStringLiteral("closeBtn"));
    topbar_layout->addWidget(close_btn_);

    layout->addWidget(topbar_frame_);

    connect(theme_toggle_btn_, &QPushButton::clicked,
            this, &MainWindow::on_theme_toggle);
    connect(topbar_lock_btn_, &QPushButton::clicked,
            this, &MainWindow::on_lock_clicked);
    connect(topbar_add_btn_, &QPushButton::clicked,
            this, &MainWindow::on_add_entry_clicked);
    connect(minimize_btn_, &QPushButton::clicked,
            this, &MainWindow::on_minimize_clicked);
    connect(close_btn_, &QPushButton::clicked,
            this, &MainWindow::on_close_clicked);
}

void MainWindow::update_topbar_for_view(int row) {
    switch (row) {
        case kSidebarBook:
            topbar_title_->setText(QStringLiteral("密码本"));
            topbar_subtitle_->setText(QString());
            topbar_count_badge_->show();
            topbar_add_btn_->show();
            break;
        case kSidebarInput:
            topbar_title_->setText(QStringLiteral("录入"));
            topbar_subtitle_->setText(QStringLiteral("新建一条密码记录"));
            topbar_count_badge_->hide();
            topbar_add_btn_->hide();
            break;
        case kSidebarGenerator:
            topbar_title_->setText(QStringLiteral("生成器"));
            topbar_subtitle_->setText(QStringLiteral("生成高强度随机密码"));
            topbar_count_badge_->hide();
            topbar_add_btn_->hide();
            break;
        case kSidebarSettings:
            topbar_title_->setText(QStringLiteral("设置"));
            topbar_subtitle_->setText(QStringLiteral("版本 3.1.0"));
            topbar_count_badge_->hide();
            topbar_add_btn_->hide();
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// 状态更新
// ---------------------------------------------------------------------------

void MainWindow::update_connection_status() {
    const bool connected = client_ && client_->is_connected();
    if (connected) {
        service_status_dot_->setProperty("cssClass", QStringLiteral("statusDotOk"));
        service_status_label_->setText(QStringLiteral("服务已连接"));
    } else {
        service_status_dot_->setProperty("cssClass", QStringLiteral("statusDotErr"));
        service_status_label_->setText(QStringLiteral("服务未连接"));
    }
    // 切换 dynamic property 后必须 unpolish + polish 才能让 QSS 重新生效
    service_status_dot_->style()->unpolish(service_status_dot_);
    service_status_dot_->style()->polish(service_status_dot_);
}

void MainWindow::attempt_reconnect() {
    if (!client_) return;
    if (client_->connect_to_service()) {
        update_connection_status();
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

void MainWindow::refresh_nav_icons() {
    // 按选中状态着色：选中项用 Active（主文字色），其余用 Normal（muted）。
    struct Nav { QPushButton* btn; const char* svg; };
    const Nav navs[] = {
        { nav_book_btn_,      ":/icons/key-round.svg" },
        { nav_input_btn_,     ":/icons/file-plus-2.svg" },
        { nav_generator_btn_, ":/icons/wand-2.svg" },
        { nav_settings_btn_,  ":/icons/settings.svg" },
    };
    for (const auto& n : navs) {
        if (!n.btn) continue;
        const IconRole role = n.btn->isChecked() ? IconRole::Active : IconRole::Normal;
        n.btn->setIcon(tinted_icon(QString::fromLatin1(n.svg), role));
    }
}

void MainWindow::refresh_topbar_icons() {
    // 主题切换按钮：深色时显示太阳（点击切到浅色），浅色时显示月亮
    const QString theme_svg = Theme::is_dark()
        ? QStringLiteral(":/icons/sun.svg")
        : QStringLiteral(":/icons/moon.svg");
    if (theme_toggle_btn_)
        theme_toggle_btn_->setIcon(tinted_icon(theme_svg, IconRole::Normal));
    if (topbar_lock_btn_)
        topbar_lock_btn_->setIcon(tinted_icon(QStringLiteral(":/icons/lock.svg"), IconRole::Normal));
    if (minimize_btn_)
        minimize_btn_->setIcon(tinted_icon(QStringLiteral(":/icons/minus.svg"), IconRole::Normal));
    // close 按钮：常态用 Normal 色（hover 时 QSS 切红背景，中灰图标在红底上仍可读）
    if (close_btn_)
        close_btn_->setIcon(tinted_icon(QStringLiteral(":/icons/x.svg"), IconRole::Normal));
    if (sidebar_lock_btn_)
        sidebar_lock_btn_->setIcon(tinted_icon(QStringLiteral(":/icons/lock.svg"), IconRole::Normal));
    // 新增按钮：primary 背景上用白色图标
    if (topbar_add_btn_)
        topbar_add_btn_->setIcon(tinted_icon(QStringLiteral(":/icons/plus.svg"), IconRole::OnPrimary));
}

// ---------------------------------------------------------------------------
// 解锁流程
// ---------------------------------------------------------------------------

bool MainWindow::should_show_unlock() const {
    if (!client_ || !client_->is_connected()) return false;
    auto result = client_->get_vault_status();
    if (!result.ok()) return false;
    return result.value().password_enabled && result.value().is_locked;
}

void MainWindow::show_unlock() {
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
    if (book_view_) book_view_->refresh();
    if (settings_view_) settings_view_->refresh_status();
    show();
    raise();
    activateWindow();
    update_connection_status();
}

void MainWindow::on_unlock_rejected() {
    QApplication::quit();
}

void MainWindow::on_lock_requested() {
    show_unlock();
}

void MainWindow::on_password_state_changed(bool enabled) {
    // 明文模式（未启用程序密码）下隐藏所有「锁定」按钮：
    // 无加密即无可锁，按钮可见会让用户误操作后报错。
    if (sidebar_lock_btn_) sidebar_lock_btn_->setVisible(enabled);
    if (topbar_lock_btn_) topbar_lock_btn_->setVisible(enabled);
    if (settings_view_) settings_view_->refresh_status();
}

// ---------------------------------------------------------------------------
// 视图间联动
// ---------------------------------------------------------------------------

void MainWindow::on_entry_added(int64_t id) {
    (void)id;
    if (book_view_) book_view_->refresh();
    switch_to_view(kSidebarBook);
}

void MainWindow::on_password_generator_requested() {
    generator_from_input_ = (content_stack_->currentWidget() == input_view_);
    switch_to_view(kSidebarGenerator);
}

void MainWindow::on_password_generated(const QString& password) {
    // 仅在 InputView 请求生成时才回填，避免独立使用生成器时覆盖 InputView 已填内容
    if (generator_from_input_) {
        generator_from_input_ = false;
        if (input_view_) input_view_->set_password(password);
        switch_to_view(kSidebarInput);
    }
}

void MainWindow::on_entry_count_changed(int count) {
    if (topbar_count_badge_) {
        topbar_count_badge_->setText(QStringLiteral("%1 条").arg(count));
    }
}

void MainWindow::switch_to_view(int row) {
    auto* btn = nav_group_->button(row);
    if (btn) btn->setChecked(true);
    content_stack_->setCurrentIndex(row);
    update_topbar_for_view(row);
    if (row == kSidebarBook && book_view_) {
        book_view_->refresh();
    }
    if (row == kSidebarSettings && settings_view_) {
        settings_view_->refresh_status();
    }
}

// ---------------------------------------------------------------------------
// 槽函数
// ---------------------------------------------------------------------------

void MainWindow::on_nav_clicked(int row) {
    content_stack_->setCurrentIndex(row);
    update_topbar_for_view(row);
    refresh_nav_icons();
    if (row == kSidebarBook && book_view_) {
        book_view_->refresh();
    }
    if (row == kSidebarSettings && settings_view_) {
        settings_view_->refresh_status();
    }
}

void MainWindow::on_theme_toggle() {
    Theme::toggle();
    // 主题切换后所有图标颜色都变化，需全部重新着色
    refresh_nav_icons();
    refresh_topbar_icons();
}

void MainWindow::on_lock_clicked() {
    if (!client_) return;
    auto result = client_->lock();
    if (result.ok()) {
        show_unlock();
    } else {
        const QString msg = QString::fromStdString(result.error().what());
        QMessageBox::warning(this, QStringLiteral("锁定失败"),
            msg.isEmpty() ? QStringLiteral("锁定密码库失败。")
                          : QStringLiteral("锁定失败：%1").arg(msg));
    }
}

void MainWindow::on_add_entry_clicked() {
    switch_to_view(kSidebarInput);
    if (input_view_) input_view_->focus_first_field();
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
    // 新设计无状态栏，错误用 tooltip 或弹窗（这里静默处理，避免打扰）
    (void)message;
}

}  // namespace pwdvault::ui
