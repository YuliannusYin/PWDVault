// coding: utf-8
// =============================================================================
// SettingsView.cpp
//
// PwdVault 设置视图实现。显示版本与数据路径，处理锁定与关于。
// =============================================================================
#include "SettingsView.h"
#include "IpcClient.h"

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
        // 取第一项，剥离最后一级组织名重复
        QString path = locs.first();
        // 若以 PwdVault/PwdVault 结尾，回退到上一级
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

    // 按钮区
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

    connect(lock_button_, &QPushButton::clicked,
            this, &SettingsView::on_lock_clicked);
    connect(about_button_, &QPushButton::clicked,
            this, &SettingsView::on_about_clicked);
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
