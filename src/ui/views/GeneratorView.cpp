// coding: utf-8
// =============================================================================
// GeneratorView.cpp
//
// PwdVault 密码生成器视图实现。构造 PasswordGeneratorOptions 调用 generate_password，
// 生成后调用 estimate_strength 更新强度条。
// =============================================================================
#include "GeneratorView.h"
#include "IpcClient.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include <string>

namespace pwdvault::ui {

namespace {

/// 强度条颜色：&lt; 40 红、40-80 黄、&gt; 80 绿。
QString strength_color(int bits) {
    if (bits < 40) return QStringLiteral("red");
    if (bits < 80) return QStringLiteral("orange");
    return QStringLiteral("green");
}

QString strength_text(int bits) {
    if (bits < 40) return QStringLiteral("弱");
    if (bits < 80) return QStringLiteral("中");
    return QStringLiteral("强");
}

}  // namespace

GeneratorView::GeneratorView(IpcClient* client, QWidget* parent)
    : QWidget(parent), client_(client)
{
    build_ui();
}

GeneratorView::~GeneratorView() = default;

void GeneratorView::build_ui() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    auto* title = new QLabel(QStringLiteral("密码生成器"), this);
    QFont tf = title->font();
    tf.setPointSize(13);
    tf.setBold(true);
    title->setFont(tf);
    layout->addWidget(title);

    // 长度
    auto* len_row = new QHBoxLayout();
    len_row->addWidget(new QLabel(QStringLiteral("密码长度："), this));
    length_spin_ = new QSpinBox(this);
    length_spin_->setRange(4, 128);
    length_spin_->setValue(16);
    len_row->addWidget(length_spin_);
    len_row->addStretch(1);
    layout->addLayout(len_row);

    // 字符集
    auto* charset_group = new QGroupBox(QStringLiteral("字符集"), this);
    auto* charset_layout = new QVBoxLayout(charset_group);
    upper_check_ = new QCheckBox(QStringLiteral("大写字母 A-Z"), this);
    upper_check_->setChecked(true);
    lower_check_ = new QCheckBox(QStringLiteral("小写字母 a-z"), this);
    lower_check_->setChecked(true);
    digits_check_ = new QCheckBox(QStringLiteral("数字 0-9"), this);
    digits_check_->setChecked(true);
    symbols_check_ = new QCheckBox(QStringLiteral("符号 !@#$%^&*..."), this);
    symbols_check_->setChecked(true);
    exclude_ambiguous_check_ = new QCheckBox(
        QStringLiteral("排除易混字符 (i l 1 L o 0 O)"), this);
    charset_layout->addWidget(upper_check_);
    charset_layout->addWidget(lower_check_);
    charset_layout->addWidget(digits_check_);
    charset_layout->addWidget(symbols_check_);
    charset_layout->addWidget(exclude_ambiguous_check_);
    layout->addWidget(charset_group);

    // 自定义字符
    auto* custom_row = new QHBoxLayout();
    custom_row->addWidget(new QLabel(QStringLiteral("自定义字符："), this));
    custom_chars_edit_ = new QLineEdit(this);
    custom_chars_edit_->setPlaceholderText(QStringLiteral("可选：追加自定义字符集"));
    custom_row->addWidget(custom_chars_edit_, 1);
    layout->addLayout(custom_row);

    // 生成按钮
    generate_button_ = new QPushButton(QStringLiteral("生成密码"), this);
    generate_button_->setDefault(true);
    generate_button_->setMinimumHeight(32);
    layout->addWidget(generate_button_);

    // 结果显示
    result_edit_ = new QLineEdit(this);
    result_edit_->setReadOnly(true);
    result_edit_->setPlaceholderText(QStringLiteral("生成的密码将显示在这里"));
    QFont rf = result_edit_->font();
    rf.setPointSize(12);
    result_edit_->setFont(rf);
    layout->addWidget(result_edit_);

    // 强度条
    strength_bar_ = new QProgressBar(this);
    strength_bar_->setRange(0, 100);
    strength_bar_->setValue(0);
    strength_bar_->setTextVisible(false);
    strength_bar_->setFixedHeight(8);
    layout->addWidget(strength_bar_);

    strength_label_ = new QLabel(QStringLiteral("强度：-"), this);
    layout->addWidget(strength_label_);

    layout->addStretch(1);

    // 复制按钮
    copy_button_ = new QPushButton(QStringLiteral("复制到剪贴板"), this);
    copy_button_->setEnabled(false);
    layout->addWidget(copy_button_);

    // 信号槽
    connect(generate_button_, &QPushButton::clicked,
            this, &GeneratorView::on_generate_clicked);
    connect(copy_button_, &QPushButton::clicked,
            this, &GeneratorView::on_copy_clicked);
}

void GeneratorView::on_generate_clicked() {
    if (!client_) return;

    core::PasswordGeneratorOptions options;
    options.length = static_cast<size_t>(length_spin_->value());
    options.use_uppercase = upper_check_->isChecked();
    options.use_lowercase = lower_check_->isChecked();
    options.use_digits = digits_check_->isChecked();
    options.use_symbols = symbols_check_->isChecked();
    options.exclude_ambiguous = exclude_ambiguous_check_->isChecked();
    options.custom_chars = custom_chars_edit_->text().toStdString();

    // 至少需要一种字符集
    if (!options.use_uppercase && !options.use_lowercase &&
        !options.use_digits && !options.use_symbols &&
        options.custom_chars.empty()) {
        result_edit_->setText(QString());
        strength_bar_->setValue(0);
        strength_label_->setStyleSheet(QStringLiteral("color: red;"));
        strength_label_->setText(QStringLiteral("请至少选择一种字符集。"));
        return;
    }

    auto result = client_->generate_password(options);
    if (result.ok()) {
        const QString password = QString::fromStdString(result.value().password);
        result_edit_->setText(password);
        copy_button_->setEnabled(true);
        update_strength(password);
        emit password_generated(password);
    } else {
        result_edit_->setText(QString());
        copy_button_->setEnabled(false);
        strength_bar_->setValue(0);
        const QString msg = QString::fromStdString(result.error().what());
        strength_label_->setStyleSheet(QStringLiteral("color: red;"));
        strength_label_->setText(msg.isEmpty()
                                     ? QStringLiteral("生成失败。")
                                     : QStringLiteral("生成失败：%1").arg(msg));
    }
}

void GeneratorView::on_copy_clicked() {
    const QString password = result_edit_->text();
    if (password.isEmpty()) return;
    QApplication::clipboard()->setText(password);
    strength_label_->setStyleSheet(QStringLiteral("color: green;"));
    strength_label_->setText(QStringLiteral("已复制到剪贴板。"));
}

void GeneratorView::update_strength(const QString& password) {
    if (!client_ || password.isEmpty()) {
        strength_bar_->setValue(0);
        strength_bar_->setStyleSheet(QString());
        strength_label_->setText(QStringLiteral("强度：-"));
        strength_label_->setStyleSheet(QString());
        return;
    }

    auto result = client_->estimate_strength(password.toStdString());
    int bits = 0;
    if (result.ok()) {
        bits = result.value().strength_bits;
    }

    const int pct = (bits >= 128) ? 100 : (bits * 100 / 128);
    strength_bar_->setValue(pct);

    const QString color = strength_color(bits);
    strength_bar_->setStyleSheet(
        QStringLiteral("QProgressBar::chunk { background-color: %1; }").arg(color));

    strength_label_->setStyleSheet(QString());
    strength_label_->setText(
        QStringLiteral("强度：%1（%2 bit）").arg(strength_text(bits)).arg(bits));
}

}  // namespace pwdvault::ui
