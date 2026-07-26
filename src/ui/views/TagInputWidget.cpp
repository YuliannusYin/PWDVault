// coding: utf-8
// =============================================================================
// TagInputWidget.cpp
//
// 标签输入控件实现。
// =============================================================================
#include "TagInputWidget.h"

#include <QCompleter>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStringListModel>
#include <QToolButton>

#include "FlowLayout.h"
#include "Types.h"

namespace pwdvault::ui {

namespace {

/// 透明融入 FlowLayout 的 QLineEdit，支持「空输入时退格删除最后一个芯片」。
class ChipLineEdit : public QLineEdit {
    Q_OBJECT
public:
    explicit ChipLineEdit(QWidget* parent = nullptr) : QLineEdit(parent) {
        setStyleSheet(QStringLiteral("border: none; background: transparent;"));
        setPlaceholderText(QStringLiteral("输入标签后回车"));
    }

signals:
    void backspaceAtEmpty();

protected:
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Backspace && text().isEmpty()) {
            emit backspaceAtEmpty();
            event->accept();
            return;
        }
        QLineEdit::keyPressEvent(event);
    }
};

}  // namespace

TagInputWidget::TagInputWidget(QWidget* parent) : QWidget(parent) {
    chips_layout_ = new FlowLayout(this, /*margin=*/0, /*hSpacing=*/6, /*vSpacing=*/6);

    input_ = new ChipLineEdit(this);
    input_->setMinimumWidth(120);
    chips_layout_->addWidget(input_);

    completer_ = new QCompleter(this);
    completer_->setModel(new QStringListModel(this));
    completer_->setCaseSensitivity(Qt::CaseSensitive);
    completer_->setFilterMode(Qt::MatchContains);
    input_->setCompleter(completer_);

    connect(input_, &QLineEdit::returnPressed, this, &TagInputWidget::on_return_pressed);
    connect(static_cast<ChipLineEdit*>(input_), &ChipLineEdit::backspaceAtEmpty,
            this, [this]() { remove_tag_at(static_cast<int>(selected_tags_.size()) - 1); });
}

TagInputWidget::~TagInputWidget() = default;

void TagInputWidget::set_existing_tags(const std::vector<core::Tag>& tags) {
    existing_tags_ = tags;
    QStringList names;
    names.reserve(static_cast<int>(tags.size()));
    for (const auto& t : tags) names << QString::fromStdString(t.name);
    if (auto* model = qobject_cast<QStringListModel*>(completer_->model())) {
        model->setStringList(names);
    }
}

void TagInputWidget::set_selected_tags(const std::vector<core::Tag>& tags) {
    selected_tags_ = tags;
    rebuild_chips();
}

std::vector<core::Tag> TagInputWidget::selected_tags() const {
    return selected_tags_;
}

void TagInputWidget::rebuild_chips() {
    // 先把 input_ 从布局中取出（保留 widget，不删除）
    // input_ 始终是最后一个 item
    QLayoutItem* input_item = nullptr;
    while (chips_layout_->count() > 0) {
        QLayoutItem* item = chips_layout_->takeAt(chips_layout_->count() - 1);
        if (!item) break;
        QWidget* w = item->widget();
        if (w == input_) {
            input_item = item;  // 保留，稍后重新加入
        } else {
            if (w) w->deleteLater();
            delete item;
        }
    }

    // 重新添加芯片
    for (const auto& tag : selected_tags_) {
        const QString tag_name = QString::fromStdString(tag.name);

        auto* chip = new QFrame(this);
        chip->setObjectName(QStringLiteral("tagChip"));
        chip->setProperty("cssClass", QStringLiteral("tagChip"));

        auto* hl = new QHBoxLayout(chip);
        hl->setContentsMargins(8, 2, 4, 2);
        hl->setSpacing(4);

        auto* name = new QLabel(tag_name, chip);
        name->setObjectName(QStringLiteral("tagChipName"));
        hl->addWidget(name);

        auto* btn = new QToolButton(chip);
        btn->setObjectName(QStringLiteral("tagChipRemove"));
        btn->setText(QStringLiteral("×"));
        btn->setFixedSize(16, 16);
        btn->setCursor(Qt::PointingHandCursor);
        connect(btn, &QToolButton::clicked, this, [this, tag_name]() {
            const std::string target = tag_name.toStdString();
            for (size_t i = 0; i < selected_tags_.size(); ++i) {
                if (selected_tags_[i].name == target) {
                    remove_tag_at(static_cast<int>(i));
                    return;
                }
            }
        });
        hl->addWidget(btn);

        chips_layout_->addWidget(chip);
    }

    // 重新添加 input_（最后）
    if (input_item) {
        chips_layout_->addItem(input_item);
    } else {
        chips_layout_->addWidget(input_);
    }

    updateGeometry();
}

void TagInputWidget::on_return_pressed() {
    const QString text = input_->text().trimmed();
    if (!text.isEmpty()) {
        add_tag_by_name(text);
        input_->clear();
    }
    focus_input();
}

void TagInputWidget::add_tag_by_name(const QString& name) {
    const std::string name_std = name.toStdString();
    if (name_std.empty()) return;

    // 去重：与已选标签同名（大小写敏感）则跳过
    for (const auto& t : selected_tags_) {
        if (t.name == name_std) {
            focus_input();
            return;
        }
    }

    // 查找已有标签
    core::Tag tag;
    bool found = false;
    for (const auto& t : existing_tags_) {
        if (t.name == name_std) {
            tag = t;
            found = true;
            break;
        }
    }
    if (!found) {
        // 新标签：id=0 表示尚未分配
        tag.id = 0;
        tag.name = name_std;
    }

    selected_tags_.push_back(tag);
    rebuild_chips();
    emit tags_changed();
    focus_input();
}

void TagInputWidget::remove_tag_at(int index) {
    if (index < 0 || index >= static_cast<int>(selected_tags_.size())) return;
    selected_tags_.erase(selected_tags_.begin() + index);
    rebuild_chips();
    emit tags_changed();
    focus_input();
}

void TagInputWidget::focus_input() {
    input_->setFocus();
}

}  // namespace pwdvault::ui

#include "TagInputWidget.moc"
