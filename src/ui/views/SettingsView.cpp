// coding: utf-8
// =============================================================================
// SettingsView.cpp
//
// PwdVault 设置视图实现（新设计）。
// 卡片分节布局：安全 / 外观 / 存储 / 关于 / 危险操作。
// =============================================================================
#include "SettingsView.h"
#include "IpcClient.h"
#include "ProgramPasswordDialog.h"
#include "GeneratorHistoryDialog.h"
#include "Theme.h"
#include "IconKit.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QString>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

namespace pwdvault::ui {

namespace {

/// 应用版本号。
constexpr const char* kAppVersion = "3.2.0";

/// GitHub 项目主页 URL。
constexpr const char* kGitHubUrl = "https://github.com/YuliannusYin/PWDVault";

/// 开源许可 URL。
constexpr const char* kLicenseUrl = "https://opensource.org/licenses/MIT";

/// 返回数据存储目录路径（%APPDATA%\PwdVault）。
QString data_storage_path() {
    const QStringList locs = QStandardPaths::standardLocations(
        QStandardPaths::AppDataLocation);
    if (!locs.isEmpty()) {
        QString path = locs.first();
        if (path.endsWith(QStringLiteral("/PwdVault/PwdVault"),
                          Qt::CaseInsensitive) ||
            path.endsWith(QStringLiteral("\\PwdVault\\PwdVault"),
                          Qt::CaseInsensitive)) {
            int idx = path.lastIndexOf(QLatin1Char('/'));
            if (idx < 0) idx = path.lastIndexOf(QLatin1Char('\\'));
            if (idx > 0) path = path.left(idx);
        }
        return path;
    }
    // fallback：QStandardPaths 极少返回空，但万一发生用 APPDATA 环境变量兜底，
    // 避免返回字面量 "%APPDATA%" 让 QUrl 解析失败。
    const QString appdata = qEnvironmentVariable("APPDATA");
    if (!appdata.isEmpty()) {
        return QDir(appdata).filePath(QStringLiteral("PwdVault"));
    }
    return QStringLiteral("C:\\PwdVault");
}

}  // namespace

SettingsView::SettingsView(IpcClient* client, QWidget* parent)
    : QWidget(parent), client_(client)
{
    setObjectName(QStringLiteral("settingsView"));
    build_ui();
    sync_theme_segment();
    // 监听主题切换：顶栏切换主题后同步分段控件选中项，
    // 否则用户打开设置页会看到旧的选中状态。
    if (auto* theme = Theme::instance()) {
        connect(theme, &Theme::theme_changed, this, &SettingsView::sync_theme_segment);
    }
}

SettingsView::~SettingsView() = default;

// ---------------------------------------------------------------------------
// UI 构建
// ---------------------------------------------------------------------------

void SettingsView::build_ui() {
    // 外层：可滚动区域
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(
        QStringLiteral("QScrollArea { background: transparent; border: none; }"));
    outer->addWidget(scroll);

    auto* content = new QWidget(scroll);
    // 不设 setStyleSheet("background: transparent;")：widget 级样式表优先级
    // 高于 qss 文件，无选择器的 "background: transparent" 会级联到所有子 widget，
    // 覆盖 card 的 background-color。content 继承全局 QWidget 底色即可（与
    // 右侧栏一致，scroll 已设 transparent 让滚动区不遮挡）。
    auto* content_layout = new QVBoxLayout(content);
    content_layout->setContentsMargins(24, 24, 24, 24);
    content_layout->setSpacing(16);

    // 水平居中容器：左右 stretch + center_container（max-width 720px）。
    // 垂直方向从顶部开始排列（仅水平居中，不垂直居中），内容多时滚动。
    auto* hbox = new QHBoxLayout();
    hbox->setContentsMargins(0, 0, 0, 0);
    hbox->setSpacing(0);
    auto* center_container = new QWidget(content);
    center_container->setMaximumWidth(720);
    auto* center_layout = new QVBoxLayout(center_container);
    center_layout->setContentsMargins(0, 0, 0, 0);
    center_layout->setSpacing(16);
    hbox->addStretch(1);
    hbox->addWidget(center_container);
    hbox->addStretch(1);
    content_layout->addLayout(hbox);

    // ── 安全 section ──
    {
        auto* section = make_section(QStringLiteral(":/icons/shield-check.svg"),
                                     QStringLiteral("安全"), false, center_container,
                                     QColor(QStringLiteral("#2bd576")));
        center_layout->addWidget(section);
        auto* section_layout = qobject_cast<QVBoxLayout*>(section->layout());

        // 程序密码行
        pp_badge_ = new QLabel(section);
        pp_badge_->setProperty("cssClass", QStringLiteral("badgeSuccess"));
        // pp_desc_ 作为本行描述文本：通过 add_row 的 description 参数安置，
        // 避免无 layout 管理时飘在 card 左上角。
        manage_pp_btn_ = new QPushButton(section);
        manage_pp_btn_->setIcon(tinted_icon(QStringLiteral(":/icons/settings-2.svg"), IconRole::Normal));
        manage_pp_btn_->setIconSize(QSize(16, 16));
        manage_pp_btn_->setText(QStringLiteral("管理程序密码"));
        manage_pp_btn_->setCursor(Qt::PointingHandCursor);
        manage_pp_btn_->setFixedHeight(40);
        manage_pp_btn_->setProperty("cssClass", QStringLiteral("outline"));

        // 右侧组合：badge + 按钮
        auto* right_row = new QWidget(section);
        auto* right_layout = new QHBoxLayout(right_row);
        right_layout->setContentsMargins(0, 0, 0, 0);
        right_layout->setSpacing(8);
        right_layout->addWidget(pp_badge_);
        right_layout->addWidget(manage_pp_btn_);
        // pp_desc_ 通过 out_desc 回传，供 refresh_password_badge 动态更新文本
        add_row(section_layout, QStringLiteral("程序密码"),
                QStringLiteral("未启用 · 保险库明文存储"), right_row, &pp_desc_);

        // 自动锁定行（占位，下拉不接业务逻辑）
        autolock_combo_ = new QComboBox(section);
        autolock_combo_->setObjectName(QStringLiteral("settingsAutolock"));
        autolock_combo_->addItem(QStringLiteral("不自动锁定"));
        autolock_combo_->addItem(QStringLiteral("1 分钟"));
        autolock_combo_->addItem(QStringLiteral("5 分钟"));
        autolock_combo_->addItem(QStringLiteral("15 分钟"));
        autolock_combo_->addItem(QStringLiteral("30 分钟"));
        autolock_combo_->setCurrentIndex(2);
        autolock_combo_->setFixedHeight(40);
        autolock_combo_->setMinimumWidth(120);
        add_row(section_layout, QStringLiteral("自动锁定"),
                QStringLiteral("空闲后自动锁定保险库"), autolock_combo_);
    }

    // ── 外观 section ──
    {
        auto* section = make_section(QStringLiteral(":/icons/palette.svg"),
                                     QStringLiteral("外观"), false, center_container);
        center_layout->addWidget(section);
        auto* section_layout = qobject_cast<QVBoxLayout*>(section->layout());
        auto* seg = build_theme_segmented();
        seg->setParent(section);
        add_row(section_layout, QStringLiteral("主题"),
                QStringLiteral("切换深色 / 浅色模式"), seg);
    }

    // ── 生成器 section ──
    {
        auto* section = make_section(QStringLiteral(":/icons/wand-2.svg"),
                                     QStringLiteral("生成器"), false, center_container,
                                     QColor(QStringLiteral("#3B6BFF")));
        center_layout->addWidget(section);
        auto* section_layout = qobject_cast<QVBoxLayout*>(section->layout());

        // 历史记录行：左描述动态显示「已保存 N 条记录 / 暂无记录」
        view_history_btn_ = new QPushButton(section);
        view_history_btn_->setIcon(tinted_icon(QStringLiteral(":/icons/clock.svg"), IconRole::Normal));
        view_history_btn_->setIconSize(QSize(16, 16));
        view_history_btn_->setText(QStringLiteral("查看记录"));
        view_history_btn_->setCursor(Qt::PointingHandCursor);
        view_history_btn_->setFixedHeight(40);
        view_history_btn_->setProperty("cssClass", QStringLiteral("outline"));
        add_row(section_layout, QStringLiteral("密码生成记录"),
                QStringLiteral("加载中…"), view_history_btn_, &gen_history_desc_);

        // 上限下拉：item data 携带实际数值（0 = 无限制）
        gen_limit_combo_ = new QComboBox(section);
        gen_limit_combo_->setObjectName(QStringLiteral("settingsGenLimit"));
        gen_limit_combo_->addItem(QStringLiteral("无限制"), QVariant(0));
        gen_limit_combo_->addItem(QStringLiteral("10 条"), QVariant(10));
        gen_limit_combo_->addItem(QStringLiteral("20 条"), QVariant(20));
        gen_limit_combo_->addItem(QStringLiteral("50 条"), QVariant(50));
        gen_limit_combo_->addItem(QStringLiteral("100 条"), QVariant(100));
        gen_limit_combo_->addItem(QStringLiteral("200 条"), QVariant(200));
        gen_limit_combo_->setCurrentIndex(0);
        gen_limit_combo_->setFixedHeight(40);
        gen_limit_combo_->setMinimumWidth(120);
        add_row(section_layout, QStringLiteral("记录上限"),
                QStringLiteral("保留最近 N 条生成记录，超出自动清理"), gen_limit_combo_);
    }

    // ── 存储 section ──
    {
        auto* section = make_section(QStringLiteral(":/icons/database.svg"),
                                     QStringLiteral("存储"), false, center_container);
        center_layout->addWidget(section);
        auto* section_layout = qobject_cast<QVBoxLayout*>(section->layout());
        // 存储路径行
        storage_path_label_ = new QLabel(data_storage_path(), section);
        storage_path_label_->setObjectName(QStringLiteral("settingsPathBox"));
        storage_path_label_->setProperty("cssClass", QStringLiteral("pathBox"));
        storage_path_label_->setFixedHeight(40);
        storage_path_label_->setMinimumWidth(240);
        open_storage_btn_ = new QPushButton(section);
        open_storage_btn_->setIcon(tinted_icon(QStringLiteral(":/icons/folder-open.svg"), IconRole::Normal));
        open_storage_btn_->setIconSize(QSize(16, 16));
        open_storage_btn_->setText(QStringLiteral("打开"));
        open_storage_btn_->setCursor(Qt::PointingHandCursor);
        open_storage_btn_->setFixedHeight(40);
        open_storage_btn_->setProperty("cssClass", QStringLiteral("outline"));

        auto* right_row1 = new QWidget(section);
        auto* right_layout1 = new QHBoxLayout(right_row1);
        right_layout1->setContentsMargins(0, 0, 0, 0);
        right_layout1->setSpacing(8);
        right_layout1->addWidget(storage_path_label_);
        right_layout1->addWidget(open_storage_btn_);
        add_row(section_layout, QStringLiteral("存储路径"),
                QStringLiteral("保险库数据文件位置"), right_row1);

        // 条目数量行
        entry_count_label_ = new QLabel(QStringLiteral("-"), section);
        entry_count_label_->setProperty("cssClass", QStringLiteral("fieldLabel"));
        add_row(section_layout, QStringLiteral("条目数量"),
                QStringLiteral("已保存的密码记录"), entry_count_label_);
    }

    // ── 关于 section ──
    {
        auto* section = make_section(QStringLiteral(":/icons/info.svg"),
                                     QStringLiteral("关于"), false, center_container);
        center_layout->addWidget(section);
        auto* section_layout = qobject_cast<QVBoxLayout*>(section->layout());
        version_value_ = new QLabel(
            QStringLiteral("v%1").arg(QString::fromLatin1(kAppVersion)), section);
        version_value_->setProperty("cssClass", QStringLiteral("fieldLabel"));
        add_row(section_layout, QStringLiteral("版本"),
                QStringLiteral("PwdVault"), version_value_);

        auto* enc_value = new QLabel(QStringLiteral("AES-256-GCM · Argon2id"), section);
        enc_value->setProperty("cssClass", QStringLiteral("fieldLabel"));
        add_row(section_layout, QStringLiteral("加密方案"),
                QStringLiteral("数据加密与密钥派生"), enc_value);

        license_btn_ = new QPushButton(section);
        license_btn_->setIcon(tinted_icon(QStringLiteral(":/icons/external-link.svg"), IconRole::Normal));
        license_btn_->setIconSize(QSize(16, 16));
        license_btn_->setText(QStringLiteral("查看"));
        license_btn_->setCursor(Qt::PointingHandCursor);
        license_btn_->setFixedHeight(40);
        license_btn_->setProperty("cssClass", QStringLiteral("outline"));
        add_row(section_layout, QStringLiteral("开源许可"),
                QStringLiteral("MIT License"), license_btn_);

        github_btn_ = new QPushButton(section);
        github_btn_->setIcon(tinted_icon(QStringLiteral(":/icons/github.svg"), IconRole::Normal));
        github_btn_->setIconSize(QSize(16, 16));
        github_btn_->setText(QStringLiteral("GitHub"));
        github_btn_->setCursor(Qt::PointingHandCursor);
        github_btn_->setFixedHeight(40);
        github_btn_->setProperty("cssClass", QStringLiteral("outline"));
        add_row(section_layout, QStringLiteral("项目主页"),
                QStringLiteral("源代码与问题反馈"), github_btn_);
    }

    // ── 危险操作 section ──
    {
        auto* section = make_section(QStringLiteral(":/icons/shield-off.svg"),
                                     QStringLiteral("危险操作"),
                                     /*danger=*/true, center_container);
        center_layout->addWidget(section);
        auto* section_layout = qobject_cast<QVBoxLayout*>(section->layout());
        lock_now_btn_ = new QPushButton(section);
        lock_now_btn_->setIcon(tinted_icon(QStringLiteral(":/icons/lock.svg"), IconRole::Danger));
        lock_now_btn_->setIconSize(QSize(16, 16));
        lock_now_btn_->setText(QStringLiteral("立即锁定"));
        lock_now_btn_->setCursor(Qt::PointingHandCursor);
        lock_now_btn_->setFixedHeight(40);
        lock_now_btn_->setProperty("cssClass", QStringLiteral("danger"));
        add_row(section_layout, QStringLiteral("锁定保险库"),
                QStringLiteral("立即锁定，需重新输入程序密码"), lock_now_btn_);
    }

    // content 不再设 maxWidth：由 center_container->setMaximumWidth(720)
    // 控制模块宽度，配合 hbox stretch 实现水平居中。
    scroll->setWidget(content);

    // 信号槽
    connect(manage_pp_btn_, &QPushButton::clicked,
            this, &SettingsView::on_manage_password_clicked);
    connect(open_storage_btn_, &QPushButton::clicked,
            this, &SettingsView::on_open_storage_clicked);
    connect(license_btn_, &QPushButton::clicked,
            this, &SettingsView::on_view_license_clicked);
    connect(github_btn_, &QPushButton::clicked,
            this, &SettingsView::on_open_github_clicked);
    connect(lock_now_btn_, &QPushButton::clicked,
            this, &SettingsView::on_lock_now_clicked);
    connect(view_history_btn_, &QPushButton::clicked,
            this, &SettingsView::on_view_generator_history_clicked);
    connect(gen_limit_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsView::on_generator_limit_changed);
}

QFrame* SettingsView::make_section(const QString& icon_resource,
                                   const QString& title, bool danger,
                                   QWidget* parent,
                                   const QColor& icon_color) {
    auto* section = new QFrame(parent);
    section->setProperty("cssClass", danger
        ? QStringLiteral("cardDanger")
        : QStringLiteral("card"));

    auto* layout = new QVBoxLayout(section);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(0);

    // 标题行（图标 + 标题）
    auto* header_layout = new QHBoxLayout();
    header_layout->setContentsMargins(0, 0, 0, 12);
    header_layout->setSpacing(10);
    auto* icon_lbl = new QLabel(section);
    // 图标颜色优先级：调用方显式传入 > danger 红 > 品牌蓝
    QColor clr = icon_color.isValid() ? icon_color
        : (danger ? QColor(QStringLiteral("#f56363"))
                  : QColor(QStringLiteral("#3B6BFF")));
    icon_lbl->setPixmap(tinted_pixmap(icon_resource, clr, QSize(18, 18)));
    icon_lbl->setProperty("cssClass", QStringLiteral("inlineIcon"));
    header_layout->addWidget(icon_lbl);
    auto* title_lbl = new QLabel(title, section);
    title_lbl->setProperty("cssClass", QStringLiteral("sectionTitle"));
    header_layout->addWidget(title_lbl);
    header_layout->addStretch(1);
    layout->addLayout(header_layout);

    return section;
}

void SettingsView::add_row(QVBoxLayout* section_layout, const QString& title,
                            const QString& description, QWidget* right_widget,
                            QLabel** out_desc) {
    auto* parent_widget = section_layout->parentWidget();
    auto* row = new QFrame(parent_widget);
    row->setProperty("cssClass", QStringLiteral("fieldRow"));
    auto* row_layout = new QHBoxLayout(row);
    row_layout->setContentsMargins(0, 12, 0, 12);
    row_layout->setSpacing(12);

    // 左侧：标题 + 描述
    auto* left = new QWidget(row);
    auto* left_layout = new QVBoxLayout(left);
    left_layout->setContentsMargins(0, 0, 0, 0);
    left_layout->setSpacing(2);
    auto* title_lbl = new QLabel(title, left);
    title_lbl->setProperty("cssClass", QStringLiteral("fieldLabel"));
    left_layout->addWidget(title_lbl);
    QLabel* desc_lbl = nullptr;
    if (!description.isEmpty()) {
        desc_lbl = new QLabel(description, left);
        desc_lbl->setWordWrap(true);
        desc_lbl->setProperty("cssClass", QStringLiteral("caption"));
        left_layout->addWidget(desc_lbl);
    }
    row_layout->addWidget(left, 1);
    if (right_widget) {
        right_widget->setParent(row);
        row_layout->addWidget(right_widget, 0, Qt::AlignRight | Qt::AlignVCenter);
    }
    section_layout->addWidget(row);
    if (out_desc) *out_desc = desc_lbl;
}

QFrame* SettingsView::build_theme_segmented() {
    auto* frame = new QFrame();
    frame->setObjectName(QStringLiteral("settingsThemeSeg"));
    frame->setProperty("cssClass", QStringLiteral("segmented"));
    auto* layout = new QHBoxLayout(frame);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    theme_group_ = new QButtonGroup(this);
    theme_group_->setExclusive(true);

    auto make_btn = [](const QString& text, QWidget* parent) {
        auto* btn = new QPushButton(text, parent);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setCheckable(true);
        btn->setFixedHeight(32);
        btn->setMinimumWidth(64);
        btn->setProperty("cssClass", QStringLiteral("segmentedItem"));
        return btn;
    };

    theme_light_btn_ = make_btn(QStringLiteral("浅色"), frame);
    theme_dark_btn_ = make_btn(QStringLiteral("深色"), frame);
    theme_system_btn_ = make_btn(QStringLiteral("跟随系统"), frame);

    theme_group_->addButton(theme_light_btn_, 0);
    theme_group_->addButton(theme_dark_btn_, 1);
    theme_group_->addButton(theme_system_btn_, 2);

    layout->addWidget(theme_light_btn_);
    layout->addWidget(theme_dark_btn_);
    layout->addWidget(theme_system_btn_);

    connect(theme_group_, &QButtonGroup::idClicked,
            this, &SettingsView::on_theme_segment_clicked);

    return frame;
}

void SettingsView::sync_theme_segment() {
    if (!theme_group_) return;
    const auto mode = Theme::current_mode();
    const int id = (mode == Theme::Mode::Light) ? 0
                  : (mode == Theme::Mode::Dark) ? 1 : 2;
    QSignalBlocker b(theme_group_);
    auto* btn = theme_group_->button(id);
    if (btn) btn->setChecked(true);
}

void SettingsView::refresh_password_badge() {
    if (!pp_badge_) return;
    if (password_enabled_) {
        pp_badge_->setText(QStringLiteral("已启用"));
        pp_badge_->setProperty("cssClass", QStringLiteral("badgeSuccess"));
        if (pp_desc_) pp_desc_->setText(QStringLiteral("已启用 · 保险库加密存储"));
    } else {
        pp_badge_->setText(QStringLiteral("未启用"));
        pp_badge_->setProperty("cssClass", QStringLiteral("badge"));
        if (pp_desc_) pp_desc_->setText(QStringLiteral("未启用 · 保险库明文存储"));
    }
    // 明文模式（未启用程序密码）下隐藏「立即锁定」按钮：
    // 无加密即无可锁，按钮可见会让用户误操作后报错。
    if (lock_now_btn_) lock_now_btn_->setVisible(password_enabled_);
    // 刷新 dynamic property 样式
    pp_badge_->style()->unpolish(pp_badge_);
    pp_badge_->style()->polish(pp_badge_);
}

void SettingsView::refresh_entry_count() {
    if (!entry_count_label_) return;
    if (!client_) {
        entry_count_label_->setText(QStringLiteral("-"));
        return;
    }
    auto result = client_->list_entries();
    if (result.ok()) {
        entry_count_label_->setText(
            QStringLiteral("%1 条").arg(result.value().entries.size()));
    } else {
        entry_count_label_->setText(QStringLiteral("-"));
    }
}

void SettingsView::refresh_generator_settings() {
    if (!client_) {
        if (gen_history_desc_) gen_history_desc_->setText(QStringLiteral("-"));
        return;
    }

    // 历史记录条数
    auto list_result = client_->list_generated_records();
    if (gen_history_desc_) {
        if (list_result.ok()) {
            const int n = static_cast<int>(list_result.value().records.size());
            gen_history_desc_->setText(
                n == 0 ? QStringLiteral("暂无记录")
                       : QStringLiteral("已保存 %1 条记录").arg(n));
        } else {
            gen_history_desc_->setText(QStringLiteral("-"));
        }
    }

    // 上限下拉：以 service 端持久化的值为准
    auto settings_result = client_->get_generator_settings();
    if (!settings_result.ok()) return;
    const int32_t limit = settings_result.value().history_limit;
    // 找到与 limit 匹配的项；无匹配时（数值不在候选中）回退到「无限制」
    int target_index = 0;
    if (gen_limit_combo_) {
        gen_limit_syncing_ = true;
        for (int i = 0; i < gen_limit_combo_->count(); ++i) {
            const int v = gen_limit_combo_->itemData(i).toInt();
            if (v == limit) {
                target_index = i;
                break;
            }
        }
        QSignalBlocker blocker(gen_limit_combo_);
        gen_limit_combo_->setCurrentIndex(target_index);
        gen_limit_syncing_ = false;
    }
}

// ---------------------------------------------------------------------------
// 状态刷新
// ---------------------------------------------------------------------------

void SettingsView::refresh_status() {
    if (!client_) {
        password_enabled_ = false;
        refresh_password_badge();
        return;
    }

    auto result = client_->get_vault_status();
    if (!result.ok()) {
        password_enabled_ = false;
        refresh_password_badge();
        if (entry_count_label_) entry_count_label_->setText(QStringLiteral("-"));
        return;
    }

    password_enabled_ = result.value().password_enabled;
    refresh_password_badge();
    refresh_entry_count();
    refresh_generator_settings();
}

// ---------------------------------------------------------------------------
// 槽函数
// ---------------------------------------------------------------------------

void SettingsView::on_manage_password_clicked() {
    if (!client_) return;
    // 根据当前密码状态选择默认 Tab：已启用 → 修改；未启用 → 启用
    const auto initial_mode = password_enabled_
        ? ProgramPasswordDialog::Mode::Change
        : ProgramPasswordDialog::Mode::Enable;
    auto* dlg = new ProgramPasswordDialog(client_, initial_mode, this);
    connect(dlg, &ProgramPasswordDialog::succeeded, this, [this, dlg]() {
        dlg->deleteLater();
        refresh_status();
        emit password_state_changed(password_enabled_);
    });
    connect(dlg, &ProgramPasswordDialog::rejected, dlg, &QWidget::deleteLater);
    dlg->show();
}

void SettingsView::on_open_storage_clicked() {
    const QString path = data_storage_path();
    const QUrl url = QUrl::fromLocalFile(path);
    if (!QDesktopServices::openUrl(url)) {
        QMessageBox::warning(this, QStringLiteral("打开失败"),
            QStringLiteral("无法打开存储目录：%1").arg(path));
    }
}

void SettingsView::on_view_license_clicked() {
    QDesktopServices::openUrl(QUrl(QString::fromLatin1(kLicenseUrl)));
}

void SettingsView::on_open_github_clicked() {
    QDesktopServices::openUrl(QUrl(QString::fromLatin1(kGitHubUrl)));
}

void SettingsView::on_lock_now_clicked() {
    if (!client_) return;
    auto result = client_->lock();
    if (result.ok()) {
        emit lock_requested();
    } else {
        const QString msg = QString::fromStdString(result.error().what());
        QMessageBox::warning(this, QStringLiteral("锁定失败"),
            msg.isEmpty() ? QStringLiteral("锁定密码库失败。")
                          : QStringLiteral("锁定失败：%1").arg(msg));
    }
}

void SettingsView::on_theme_segment_clicked(int idx) {
    switch (idx) {
        case 0: Theme::set_mode(Theme::Mode::Light); break;
        case 1: Theme::set_mode(Theme::Mode::Dark); break;
        case 2: Theme::set_mode(Theme::Mode::System); break;
        default: break;
    }
}

void SettingsView::on_view_generator_history_clicked() {
    if (!client_) return;
    auto* dlg = new GeneratorHistoryDialog(client_, this);
    // 关闭后自动清理 + 刷新本页「已保存 N 条记录」描述
    connect(dlg, &QDialog::finished, this, [this]() {
        // 重新查 service：可能用户在弹窗里删除/清空了记录
        if (client_) {
            auto list_result = client_->list_generated_records();
            if (gen_history_desc_ && list_result.ok()) {
                const int n = static_cast<int>(list_result.value().records.size());
                gen_history_desc_->setText(
                    n == 0 ? QStringLiteral("暂无记录")
                           : QStringLiteral("已保存 %1 条记录").arg(n));
            }
        }
    });
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void SettingsView::on_generator_limit_changed(int index) {
    if (gen_limit_syncing_) return;
    if (!client_ || !gen_limit_combo_) return;
    if (index < 0) return;
    const int limit = gen_limit_combo_->itemData(index).toInt();
    auto result = client_->set_generator_limit(static_cast<int32_t>(limit));
    if (!result.ok()) {
        QMessageBox::warning(this, QStringLiteral("设置失败"),
            QString::fromStdString(result.error().what()));
        // 失败时回退下拉到 service 端实际值
        refresh_generator_settings();
    }
    // 成功时同步刷新历史记录条数描述（清理可能减少记录数）
    if (gen_history_desc_ && limit > 0) {
        auto list_result = client_->list_generated_records();
        if (list_result.ok()) {
            const int n = static_cast<int>(list_result.value().records.size());
            gen_history_desc_->setText(
                n == 0 ? QStringLiteral("暂无记录")
                       : QStringLiteral("已保存 %1 条记录").arg(n));
        }
    }
}

}  // namespace pwdvault::ui
