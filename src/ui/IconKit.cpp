// coding: utf-8
// =============================================================================
// IconKit.cpp — SVG 图标着色工具实现
// =============================================================================
#include "IconKit.h"
#include "Theme.h"

#include <QApplication>
#include <QClipboard>
#include <QIconEngine>
#include <QPainter>
#include <QPushButton>
#include <QSvgRenderer>
#include <QTimer>

namespace pwdvault::ui {

QColor icon_color(IconRole role) {
    const bool dark = Theme::is_dark();
    switch (role) {
        case IconRole::Normal:
            // muted：dark #8b95a8 / light #5c6678
            return dark ? QColor(0x8b, 0x95, 0xa8) : QColor(0x5c, 0x66, 0x78);
        case IconRole::Active:
            // 主文字色：dark #e8ecf4 / light #0f1626
            return dark ? QColor(0xe8, 0xec, 0xf4) : QColor(0x0f, 0x16, 0x26);
        case IconRole::OnPrimary:
            // primary 按钮上的图标统一白色
            return QColor(0xff, 0xff, 0xff);
        case IconRole::Danger:
            // 危险红：dark #f56363 / light #dc2626
            return dark ? QColor(0xf5, 0x63, 0x63) : QColor(0xdc, 0x26, 0x26);
    }
    return dark ? QColor(0x8b, 0x95, 0xa8) : QColor(0x5c, 0x66, 0x78);
}

namespace {

/// 着色渲染核心：SVG → 透明 pixmap → SourceIn 着色。
/// \param size  目标尺寸（由调用方决定语义：逻辑尺寸或物理尺寸）
///              engine 的 pixmap()/paint() 收到的 size 由 Qt 框架传入，
///              高 DPI 下框架会传入物理尺寸，故此处不自行乘 dpr。
QPixmap render_tinted(const QString& svg_path, const QColor& color, const QSize& size) {
    if (svg_path.isEmpty() || !color.isValid() || size.isEmpty()) return QPixmap();

    QSvgRenderer renderer(svg_path);
    if (!renderer.isValid()) return QPixmap();

    // 1. 渲染 SVG 到透明 pixmap。QSvgRenderer 不支持 currentColor，会 fallback
    //    到黑色；但后续 SourceIn 着色会整体替换颜色，故渲染出的颜色无所谓，
    //    只需要 alpha 形状正确。明确指定 bounds 确保 SVG 缩放填满整个 pixmap。
    QPixmap src(size);
    src.fill(Qt::transparent);
    {
        QPainter p(&src);
        renderer.render(&p, QRectF(QPointF(0, 0), size));
    }

    // 2. 着色：先画原图到目标，再用 CompositionMode_SourceIn 覆盖颜色矩形。
    //    SourceIn 语义：result = source.rgb * destination.alpha
    //    即用颜色填入原图的 alpha 形状。
    QPixmap tinted(size);
    tinted.fill(Qt::transparent);
    {
        QPainter p(&tinted);
        p.drawPixmap(0, 0, src);
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(tinted.rect(), color);
    }
    return tinted;
}

/// 自定义 QIconEngine：按需着色渲染 SVG。
///
/// 实现遵循 Qt 官方 QSvgIconEngine 的模式：
///   - pixmap(size) 用传入 size 直接渲染，不自行乘 dpr / 不 setDevicePixelRatio。
///     Qt 框架在高 DPI 下会传入物理尺寸，清晰度由框架保证。
///   - paint(painter, rect) 调用 pixmap(rect.size()) 后用 drawPixmap(topLeft, pm)。
///
/// 避免在 engine 内部手动处理 dpr —— 那会与 Qt 框架自身的 dpr 处理叠加，
/// 导致 pixmap 物理尺寸翻倍，绘制时只显示左上角 1/4。
class TintedIconEngine : public QIconEngine {
public:
    TintedIconEngine(QString svg_path, QColor color)
        : svg_path_(std::move(svg_path)), color_(std::move(color)) {}

    QIconEngine* clone() const override {
        return new TintedIconEngine(*this);
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        Q_UNUSED(mode); Q_UNUSED(state);
        return render_tinted(svg_path_, color_, size);
    }

    void paint(QPainter* painter, const QRect& rect,
               QIcon::Mode mode, QIcon::State state) override {
        Q_UNUSED(mode); Q_UNUSED(state);
        const QPixmap pm = pixmap(rect.size(), mode, state);
        painter->drawPixmap(rect.topLeft(), pm);
    }

private:
    QString svg_path_;
    QColor color_;
};

}  // namespace

QPixmap tinted_pixmap(const QString& svg_path, const QColor& color, const QSize& size) {
    // QLabel::setPixmap 场景：size 是逻辑尺寸，按 qApp dpr 渲染高清并设置 dpr，
    // 让 QLabel 在高 DPI 屏幕上清晰显示。
    const qreal dpr = qMax(qreal(1.0), qApp ? qApp->devicePixelRatio() : qreal(1.0));
    const QSize phys(qRound(size.width() * dpr), qRound(size.height() * dpr));
    QPixmap pm = render_tinted(svg_path, color, phys);
    pm.setDevicePixelRatio(dpr);
    return pm;
}

QPixmap tinted_pixmap(const QString& svg_path, IconRole role, const QSize& size) {
    return tinted_pixmap(svg_path, icon_color(role), size);
}

QIcon tinted_icon(const QString& svg_path, const QColor& color) {
    return QIcon(new TintedIconEngine(svg_path, color));
}

QIcon tinted_icon(const QString& svg_path, IconRole role) {
    return tinted_icon(svg_path, icon_color(role));
}

void apply_primary_button_style(QPushButton* btn) {
    if (!btn) return;
    // 直接用内联样式，不依赖 QSS dynamic property 选择器。
    // 浅色模式：黑色背景；深色模式：蓝色背景。两种模式文字均为白色。
    const bool dark = Theme::is_dark();
    const QString bg = dark ? QStringLiteral("#3b6bff") : QStringLiteral("#0f1626");
    const QString hover = dark ? QStringLiteral("#5a82ff") : QStringLiteral("#2a3344");
    const QString pressed = dark ? QStringLiteral("#2f5fff") : QStringLiteral("#000000");
    const QString disabled_bg = dark ? QStringLiteral("#3a4866") : QStringLiteral("#d6e0ff");
    const QString disabled_fg = dark ? QStringLiteral("#5c6678") : QStringLiteral("#6f7d99");
    btn->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: %1; color: #ffffff; border: none; "
        "border-radius: 6px; padding: 0 18px; font-weight: 500; }"
        "QPushButton:hover { background-color: %2; }"
        "QPushButton:pressed { background-color: %3; }"
        "QPushButton:disabled { background-color: %4; color: %5; }"
    ).arg(bg, hover, pressed, disabled_bg, disabled_fg));
}

void copy_secure_to_clipboard(const QString& text) {
    QApplication::clipboard()->setText(text);
    // 30 秒后自动清空剪贴板，避免密码明文长期留存被其他程序读取。
    // 使用静态 QTimer 保证全生命周止单例，多次复制会重置计时器，
    // 不会提前清空后续复制的内容。
    static QTimer clear_timer;
    clear_timer.setSingleShot(true);
    clear_timer.disconnect();
    QObject::connect(&clear_timer, &QTimer::timeout, []() {
        QApplication::clipboard()->clear();
    });
    clear_timer.start(30000);
}

}  // namespace pwdvault::ui
