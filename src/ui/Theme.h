// coding: utf-8
// =============================================================================
// Theme.h
//
// PwdVault 主题管理器。负责加载 dark.qss / light.qss，运行时切换并持久化
// 用户偏好到 QSettings（组织名 PwdVault，应用名 PwdVault）。
//
// 用法：
//   Theme::load_initial_theme(qApp);  // main.cpp 启动时调用
//   Theme::set_mode(Theme::Mode::Light);  // 切换主题，自动持久化
//   Theme::toggle();  // 在 dark/light 之间切换
// =============================================================================
#pragma once

#include <QObject>

class QApplication;

namespace pwdvault::ui {

class Theme : public QObject {
    Q_OBJECT
public:
    /// 主题模式。System 暂时按 Dark 处理（占位）。
    enum class Mode {
        Dark,
        Light,
        System,
    };
    Q_ENUM(Mode)

    /// 加载持久化的主题并应用到 \p app。应在 QApplication 构造后、显示主窗口前调用。
    static void load_initial_theme(QApplication* app);

    /// 返回单例实例（load_initial_theme 后才非空）。供需要监听 theme_changed 信号的对象使用。
    static Theme* instance();

    /// 当前模式。
    static Mode current_mode();

    /// 设置主题模式，自动持久化并即时刷新样式表。
    static void set_mode(Mode mode);

    /// 在 Dark / Light 之间切换（System 视为 Dark）。
    static void toggle();

    /// 当前是否为深色主题。
    static bool is_dark();

signals:
    /// 主题变化时触发（仅对实例化对象生效，主要供 MainWindow 监听）。
    void theme_changed(Mode new_mode);

private:
    explicit Theme(QObject* parent = nullptr);

    static Theme* instance_;
    static Mode current_mode_;

    void apply_mode(Mode mode);
    void persist(Mode mode);
    Mode load_persisted() const;
};

}  // namespace pwdvault::ui
