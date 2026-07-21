// coding: utf-8
// =============================================================================
// SettingsView.cpp
//
// PwdVault 设置视图实现。构建版本/路径/程序密码管理/关于四区，处理各按钮点击。
// =============================================================================
#include "SettingsView.h"
#include "IpcClient.h"
#include "ProgramPasswordDialog.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

namespace pwdvault::ui {

namespace {

/// 应用版本号。
constexpr const char* kAppVersion = "0.1.0";

/// 返回数据存储目录路径（%APPDATA%\PwdVault）。
QString data_storage_path() {
    // Windows: QStandardPaths::AppDataLocation 通常返回
    //   C:/Users/<user>/AppData/Roaming/PwdVault/PwdVault
    // 这里取父级 C:/Users/<user>/AppData/Roaming/PwdVault 以匹配设计文档。
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
    return QStringLiteral("%APPDATA%\\PwdVault");
}

}  // namespace

SettingsView::SettingsView(IpcClient* client, QWidget* parent)
    : QWidget(parent), client_(client)
{
    build_ui();
}

SettingsView::~SettingsView() = default;

void SettingsView::build_ui() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    auto* title = new QLabel(QStringLiteral("设置"), this);
    QFont tf = title->font();
    tf.setPointSize(13);
    tf.setBold(true);
    title->setFont(tf);
    layout->addWidget(title);

    // 版本信息
    version_label_ = new QLabel(
        QStringLiteral("版本：%1").arg(QString::fromLatin1(kAppVersion)), this);
    layout->addWidget(version_label_);

    // 数据存储路径
    storage_path_label_ = new QLabel(
        QStringLiteral("数据存储路径：%1").arg(data_storage_path()), this);
    storage_path_label_->setWordWrap(true);
    storage_path_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(storage_path_label_);

    layout->addSpacing(12);

    // 程序密码管理区
    auto* pp_title = new QLabel(QStringLiteral("程序密码"), this);
    QFont pp_tf = pp_title->font();
    pp_tf.setBold(true);
    pp_title->setFont(pp_tf);
    layout->addWidget(pp_title);

    password_status_label_ = new QLabel(
        QStringLiteral("状态：查询中..."), this);
    password_status_label_->setStyleSheet(QStringLiteral("color: #555;"));
    layout->addWidget(password_status_label_);

    auto* pp_btn_row = new QHBoxLayout();
    enable_button_ = new QPushButton(QStringLiteral("启用程序密码"), this);
    enable_button_->setMinimumHeight(32);
    disable_button_ = new QPushButton(QStringLiteral("禁用程序密码"), this);
    disable_button_->setMinimumHeight(32);
    change_button_ = new QPushButton(QStringLiteral("修改密码"), this);
    change_button_->setMinimumHeight(32);
    pp_btn_row->addWidget(enable_button_);
    pp_btn_row->addWidget(disable_button_);
    pp_btn_row->addWidget(change_button_);
    pp_btn_row->addStretch(1);
    layout->addLayout(pp_btn_row);

    layout->addSpacing(12);

    // 锁定与关于按钮
    auto* btn_row = new QHBoxLayout();
    lock_button_ = new QPushButton(QStringLiteral("锁定密码库"), this);
    lock_button_->setMinimumHeight(32);
    about_button_ = new QPushButton(QStringLiteral("关于 PwdVault"), this);
    about_button_->setMinimumHeight(32);
    btn_row->addWidget(lock_button_);
    btn_row->addWidget(about_button_);
    btn_row->addStretch(1);
    layout->addLayout(btn_row);

    layout->addStretch(1);

    connect(enable_button_, &QPushButton::clicked,
            this, &SettingsView::on_enable_clicked);
    connect(disable_button_, &QPushButton::clicked,
            this, &SettingsView::on_disable_clicked);
    connect(change_button_, &QPushButton::clicked,
            this, &SettingsView::on_change_clicked);
    connect(lock_button_, &QPushButton::clicked,
            this, &SettingsView::on_lock_clicked);
    connect(about_button_, &QPushButton::clicked,
            this, &SettingsView::on_about_clicked);
}

// ---------------------------------------------------------------------------
// 状态刷新
// ---------------------------------------------------------------------------

void SettingsView::refresh_status() {
    if (!client_) {
        password_status_label_->setText(QStringLiteral("状态：IPC 客户端不可用"));
        password_enabled_ = false;
        update_button_visibility();
        return;
    }

    auto result = client_->get_vault_status();
    if (!result.ok()) {
        password_status_label_->setText(
            QStringLiteral("状态：查询失败 - %1")
                .arg(QString::fromStdString(result.error().what())));
        return;
    }

    password_enabled_ = result.value().password_enabled;
    if (password_enabled_) {
        password_status_label_->setText(
            QStringLiteral("状态：已启用（密码库已加密）"));
        password_status_label_->setStyleSheet(QStringLiteral("color: green;"));
    } else {
        password_status_label_->setText(
            QStringLiteral("状态：未启用（密码库以明文存储）"));
        password_status_label_->setStyleSheet(QStringLiteral("color: #555;"));
    }

    update_button_visibility();
}

void SettingsView::update_button_visibility() {
    enable_button_->setVisible(!password_enabled_);
    disable_button_->setVisible(password_enabled_);
    change_button_->setVisible(password_enabled_);
    lock_button_->setEnabled(password_enabled_);
}

// ---------------------------------------------------------------------------
// 槽函数
// ---------------------------------------------------------------------------

void SettingsView::on_enable_clicked() {
    if (!client_) return;
    auto* dlg = new ProgramPasswordDialog(
        client_, ProgramPasswordDialog::Mode::Enable, this);
    connect(dlg, &ProgramPasswordDialog::succeeded, this, [this, dlg]() {
        dlg->deleteLater();
        refresh_status();
        emit password_state_changed(true);
    });
    connect(dlg, &ProgramPasswordDialog::rejected, dlg, &QWidget::deleteLater);
    dlg->show();
}

void SettingsView::on_disable_clicked() {
    if (!client_) return;
    auto* dlg = new ProgramPasswordDialog(
        client_, ProgramPasswordDialog::Mode::Disable, this);
    connect(dlg, &ProgramPasswordDialog::succeeded, this, [this, dlg]() {
        dlg->deleteLater();
        refresh_status();
        emit password_state_changed(false);
    });
    connect(dlg, &ProgramPasswordDialog::rejected, dlg, &QWidget::deleteLater);
    dlg->show();
}

void SettingsView::on_change_clicked() {
    if (!client_) return;
    auto* dlg = new ProgramPasswordDialog(
        client_, ProgramPasswordDialog::Mode::Change, this);
    connect(dlg, &ProgramPasswordDialog::succeeded, dlg, [this, dlg]() {
        dlg->deleteLater();
        refresh_status();
    });
    connect(dlg, &ProgramPasswordDialog::rejected, dlg, &QWidget::deleteLater);
    dlg->show();
}

void SettingsView::on_lock_clicked() {
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

void SettingsView::on_about_clicked() {
    QMessageBox::about(this, QStringLiteral("关于 PwdVault"),
        QStringLiteral(
            "<h3>PwdVault - 密码管理器</h3>"
            "<p>本地存储、加密保护的密码管理工具。</p>"
            "<p>版本：%1</p>"
            "<p>版权所有 &copy; 2026 PwdVault Contributors</p>"
        ).arg(QString::fromLatin1(kAppVersion)));
}

}  // namespace pwdvault::ui
