// coding: utf-8
// =============================================================================
// main.cpp
//
// PwdVault UI 进程入口。负责：
//   - 创建 QApplication 与事件循环
//   - 创建 IpcClient 并连接 service
//   - 连接失败时拉起 service 进程（QProcess::startDetached）
//   - 重试连接 3 次（由 connect_to_service 内部完成）
//   - 创建 MainWindow 显示
// =============================================================================
#include "IpcClient.h"
#include "MainWindow.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QString>
#include <QStringList>

namespace {

/// service 可执行文件名。
constexpr const char* kServiceExeName = "pwdvault-service.exe";

/// 返回与 UI 同目录的 service 可执行文件路径。
QString service_executable_path() {
    const QString app_dir = QCoreApplication::applicationDirPath();
    return QDir(app_dir).absoluteFilePath(QString::fromLatin1(kServiceExeName));
}

/// 尝试拉起 service 进程（与 UI 同目录的 pwdvault-service.exe）。
/// \return 是否成功 startDetached
bool launch_service() {
    const QString exe = service_executable_path();
    if (!QFileInfo::exists(exe)) {
        return false;
    }
    return QProcess::startDetached(exe, QStringList{});
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("PwdVault"));
    QApplication::setApplicationDisplayName(QStringLiteral("PwdVault"));
    QApplication::setOrganizationName(QStringLiteral("PwdVault"));

    pwdvault::ui::IpcClient client;

    // 1) 先尝试连接已在运行的 service。
    //    connect_to_service 内部会重试 3 次（间隔 500ms）。
    if (!client.connect_to_service()) {
        // 2) 拉起 service 进程
        if (!launch_service()) {
            QMessageBox::critical(nullptr,
                QStringLiteral("启动失败"),
                QStringLiteral("无法连接 service 且未找到 pwdvault-service.exe。\n"
                               "请确认 service 已安装在与 UI 同目录下。"));
        } else {
            // 3) 再次重试连接（connect_to_service 内部 3 次重试）
            client.connect_to_service();
        }
    }

    pwdvault::ui::MainWindow window(&client);
    window.show();

    return app.exec();
}
