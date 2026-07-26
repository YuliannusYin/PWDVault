// coding: utf-8
// =============================================================================
// MarkdownUtil.cpp
//
// 轻量级 Markdown → HTML 转换实现。
//
// 处理顺序：
//   1. 转义 HTML 特殊字符（& < >），防止 XSS 与误解析
//   2. 按行分块处理：代码块 / 标题 / 列表 / 段落
//   3. 块内处理行内元素：粗体、斜体、行内代码、链接
//
// 限制：不完整支持 CommonMark，仅覆盖备注常见场景。
// =============================================================================
#include "MarkdownUtil.h"

#include <QString>
#include <QStringList>
#include <QRegularExpression>
#include <string>

namespace pwdvault::ui {

namespace {

/// 转义 HTML 特殊字符。
QString escape_html(const QString& s) {
    QString out = s;
    out.replace('&', QStringLiteral("&amp;"));
    out.replace('<', QStringLiteral("&lt;"));
    out.replace('>', QStringLiteral("&gt;"));
    return out;
}

/// 处理行内 markdown 元素：粗体、斜体、行内代码、链接。
/// 输入应已做 HTML 转义。
QString render_inline(QString s) {
    // 行内代码 `code` —— 先处理，避免内部被其它规则二次解析
    static const QRegularExpression code_re(QStringLiteral("`([^`]+)`"));
    s.replace(code_re, QStringLiteral("<code>\\1</code>"));

    // 链接 [text](url) —— url 不转义（已在 escape_html 处理过）
    static const QRegularExpression link_re(
        QStringLiteral("\\[([^\\]]+)\\]\\(([^)]+)\\)"));
    s.replace(link_re, QStringLiteral("<a href=\"\\2\">\\1</a>"));

    // 粗体 **text**
    static const QRegularExpression bold_re(QStringLiteral("\\*\\*([^*]+)\\*\\*"));
    s.replace(bold_re, QStringLiteral("<b>\\1</b>"));

    // 斜体 *text*（在粗体之后，避免误匹配 **）
    static const QRegularExpression italic_re(QStringLiteral("\\*([^*]+)\\*"));
    s.replace(italic_re, QStringLiteral("<i>\\1</i>"));

    return s;
}

}  // namespace

QString markdown_to_html(const std::string& md) {
    return markdown_to_html(QString::fromStdString(md));
}

QString markdown_to_html(const QString& md) {
    if (md.isEmpty()) {
        return QStringLiteral("<p style=\"color:#8b95a8;\">（无）</p>");
    }

    const QStringList lines = md.split('\n');
    QStringList html;
    bool in_code_block = false;
    bool in_ul = false;
    bool in_ol = false;

    // 代码块、列表的开关辅助
    auto close_lists = [&]() {
        if (in_ul) { html << QStringLiteral("</ul>"); in_ul = false; }
        if (in_ol) { html << QStringLiteral("</ol>"); in_ol = false; }
    };

    for (const QString& raw : lines) {
        const QString line = raw;

        // 代码块围栏
        if (line.trimmed().startsWith(QStringLiteral("```"))) {
            if (!in_code_block) {
                close_lists();
                html << QStringLiteral("<pre><code>");
                in_code_block = true;
            } else {
                html << QStringLiteral("</code></pre>");
                in_code_block = false;
            }
            continue;
        }
        if (in_code_block) {
            // 代码块内：原样输出（保留缩进），仅转义已在 escape 时处理
            html << escape_html(line);
            continue;
        }

        const QString trimmed = line.trimmed();

        // 空行：关闭列表，输出段落分隔
        if (trimmed.isEmpty()) {
            close_lists();
            continue;
        }

        // 标题：### / ## / #
        if (trimmed.startsWith(QStringLiteral("### "))) {
            close_lists();
            html << QStringLiteral("<h3>") << render_inline(escape_html(trimmed.mid(4))) << QStringLiteral("</h3>");
            continue;
        }
        if (trimmed.startsWith(QStringLiteral("## "))) {
            close_lists();
            html << QStringLiteral("<h2>") << render_inline(escape_html(trimmed.mid(3))) << QStringLiteral("</h2>");
            continue;
        }
        if (trimmed.startsWith(QStringLiteral("# "))) {
            close_lists();
            html << QStringLiteral("<h1>") << render_inline(escape_html(trimmed.mid(2))) << QStringLiteral("</h1>");
            continue;
        }

        // 无序列表项：- / * 开头
        if (trimmed.startsWith(QStringLiteral("- ")) || trimmed.startsWith(QStringLiteral("* "))) {
            if (!in_ul) { close_lists(); html << QStringLiteral("<ul>"); in_ul = true; }
            html << QStringLiteral("<li>") << render_inline(escape_html(trimmed.mid(2))) << QStringLiteral("</li>");
            continue;
        }

        // 有序列表项：1. / 2. 开头
        static const QRegularExpression ol_re(QStringLiteral("^\\d+\\.\\s"));
        if (ol_re.match(trimmed).hasMatch()) {
            if (!in_ol) { close_lists(); html << QStringLiteral("<ol>"); in_ol = true; }
            const int dot_pos = trimmed.indexOf('.');
            html << QStringLiteral("<li>") << render_inline(escape_html(trimmed.mid(dot_pos + 1).trimmed())) << QStringLiteral("</li>");
            continue;
        }

        // 普通段落
        close_lists();
        html << QStringLiteral("<p>") << render_inline(escape_html(line)) << QStringLiteral("</p>");
    }

    // 收尾：未闭合的块
    if (in_code_block) html << QStringLiteral("</code></pre>");
    close_lists();

    return html.join('\n');
}

}  // namespace pwdvault::ui
