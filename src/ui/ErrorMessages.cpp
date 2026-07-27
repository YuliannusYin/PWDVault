// coding: utf-8
// =============================================================================
// ErrorMessages.cpp
//
// core::Error → 中文友好文案 实现。技术细节进 qDebug 日志。
// =============================================================================
#include "ErrorMessages.h"

#include <QDebug>
#include <QCoreApplication>

namespace pwdvault::ui {

QString friendly_message(const core::Error& error) {
    // 技术细节进日志，便于排障但不污染 UI
    const std::string what = error.what();
    if (!what.empty()) {
        qDebug() << "[PwdVault][Error]" << QString::fromStdString(what);
    }

    switch (error.code) {
        case core::ErrorCode::None:
            return QCoreApplication::translate("ErrorMessages", "成功");
        case core::ErrorCode::InvalidArgument:
            return QCoreApplication::translate("ErrorMessages", "输入无效，请检查字段内容");
        case core::ErrorCode::NotFound:
            return QCoreApplication::translate("ErrorMessages", "条目不存在，可能已被删除");
        case core::ErrorCode::AlreadyExists:
            return QCoreApplication::translate("ErrorMessages", "条目名已存在，请使用其他名称");
        case core::ErrorCode::Unauthorized:
            return QCoreApplication::translate("ErrorMessages", "密码错误或未授权");
        case core::ErrorCode::CryptoError:
            return QCoreApplication::translate("ErrorMessages", "加解密失败，数据可能已损坏");
        case core::ErrorCode::StorageError:
            return QCoreApplication::translate("ErrorMessages", "本地存储读写失败，请重试");
        case core::ErrorCode::IpcError:
            return QCoreApplication::translate("ErrorMessages", "与 service 通信失败，请重试");
        case core::ErrorCode::InternalError:
            return QCoreApplication::translate("ErrorMessages", "内部错误，请重试");
    }
    return QCoreApplication::translate("ErrorMessages", "未知错误");
}

}  // namespace pwdvault::ui
