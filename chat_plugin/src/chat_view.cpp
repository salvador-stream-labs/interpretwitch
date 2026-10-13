// SPDX-License-Identifier: GPL-2.0-or-later
#include "chat_view.hpp"
#include "chat_list.hpp"
#include "text_menu.hpp"
#include "ui_text.hpp"
#include <QAbstractTextDocumentLayout>
#include <QMouseEvent>
#include <QHideEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QTimer>
#include <cmath>

namespace awl::chat {
static constexpr int translated_property = QTextFormat::UserProperty + 1;
static constexpr int pending_property = QTextFormat::UserProperty + 2;
static QColor username_color(const Message &message) {
    static const char *fallback[]{"#a970ff", "#00d6a2", "#ff9bbd", "#6bbdff", "#f6b95e", "#c5a3ff"};
    quint32 hash = 2166136261u;
    for (const auto c : (message.user_id.isEmpty() ? message.name : message.user_id).toUtf8())
        hash = (hash ^ quint8(c)) * 16777619u;
    QColor color(message.color);
    if (!color.isValid())
        color = QColor(fallback[hash % 6]);
    const auto luminance = [](QColor c) {
        const auto linear = [](double x) {
            return x <= .04045 ? x / 12.92 : std::pow((x + .055) / 1.055, 2.4);
        };
        return .2126 * linear(c.redF()) + .7152 * linear(c.greenF()) + .0722 * linear(c.blueF());
    };
    const double base = luminance(QColor("#1d1d21"));
    // Keep Twitch's hue, lifting only colours too dark to read on chat's base.
    for (int i = 0; i < 20 && (luminance(color) + .05) / (base + .05) < 4.5; ++i)
        color = QColor::fromRgbF(color.redF() * .88 + .12, color.greenF() * .88 + .12,
                                 color.blueF() * .88 + .12);
    return color;
}
static QString inline_text(QString text) {
    // One document block per message, even when users send multiline text.
    return text.replace("\r\n", "\n")
        .replace('\r', '\n')
        .replace('\n', QChar::LineSeparator)
        .replace(QChar::ParagraphSeparator, QChar::LineSeparator);
}
ChatView::ChatView(ChatList *model, QWidget *parent)
    : QTextEdit(parent), model_(model), spinner_(this) {
    setObjectName("awlChatMessages");
    install_text_menu(this);
    setAccessibleName(ui_text("Private translated Twitch chat"));
    setReadOnly(true);
    setAcceptRichText(false);
    setUndoRedoEnabled(false);
    setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    QFont chat_font = font();
    chat_font.setPointSizeF(11);
    setFont(chat_font);
    document()->setDefaultFont(chat_font);
    document()->setDocumentMargin(12);
    spinner_.setObjectName("awlChatPendingTimer");
    spinner_.setInterval(250);
    connect(&spinner_, &QTimer::timeout, this, [this] {
        spinner_angle_ = (spinner_angle_ + 60) % 360;
        viewport()->update(waiting_region_);
    });
    connect(model_, &QAbstractItemModel::rowsInserted, this,
            [this](const QModelIndex &, int first, int last) {
                mutate([&] {
                    QTextCursor cursor(document());
                    cursor.movePosition(QTextCursor::End);
                    for (int i = first; i <= last; ++i) {
                        if (i)
                            cursor.insertBlock();
                        write_row(cursor, model_->rows[i]);
                    }
                });
            });
    connect(model_, &QAbstractItemModel::rowsAboutToBeRemoved, this,
            [this](const QModelIndex &, int first, int last) {
                mutate([&] {
                    for (int i = last; i >= first; --i) {
                        deferred_.remove(model_->rows[i].message.id);
                        const auto block = document()->findBlockByNumber(i);
                        QTextCursor cursor(block);
                        if (block.next().isValid())
                            cursor.setPosition(block.next().position(), QTextCursor::KeepAnchor);
                        else {
                            cursor.movePosition(QTextCursor::EndOfBlock);
                            cursor.setPosition(std::max(0, block.position() - 1),
                                               QTextCursor::KeepAnchor);
                        }
                        cursor.removeSelectedText();
                    }
                });
            });
    connect(model_, &QAbstractItemModel::dataChanged, this,
            [this](QModelIndex first, QModelIndex last) {
                update_rows(first.row(), last.row());
            });
    connect(model_, &QAbstractItemModel::modelReset, this, [this] {
        deferred_.clear();
        mutate([&] {
            clear();
        });
    });
    connect(this, &QTextEdit::selectionChanged, this, [this] {
        if (editing_)
            return;
        notify_reading();
        if (!textCursor().hasSelection())
            QTimer::singleShot(0, this, [this] {
                flush_deferred();
            });
    });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        if (!editing_)
            notify_reading();
    });
}
bool ChatView::following() const {
    return !mouse_down_ && !textCursor().hasSelection() &&
           verticalScrollBar()->value() >= verticalScrollBar()->maximum() - 12;
}
void ChatView::notify_reading() {
    if (reading_older)
        reading_older(!following());
}
void ChatView::mutate(const std::function<void()> &operation) {
    const bool follow = following();
    const auto selected = textCursor();
    QTextCursor anchor(document()), caret(document());
    anchor.setPosition(selected.anchor());
    caret.setPosition(selected.position());
    anchor.setKeepPositionOnInsert(true);
    caret.setKeepPositionOnInsert(true);
    const auto top = cursorForPosition(QPoint(2, 2));
    const auto top_y = cursorRect(top).y();
    editing_ = true;
    operation();
    // Track the two ends independently: keepPositionOnInsert alone keeps the
    // caret fixed but can move its anchor and accidentally select new text.
    QTextCursor restored(document());
    restored.setPosition(anchor.position());
    restored.setPosition(caret.position(), QTextCursor::KeepAnchor);
    setTextCursor(restored);
    document()->documentLayout()->documentSize();
    if (follow)
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    else
        verticalScrollBar()->setValue(verticalScrollBar()->value() + cursorRect(top).y() - top_y);
    // Decorations extend into the existing margins, beyond text's dirty rect.
    viewport()->update();
    editing_ = false;
    notify_reading();
}
void ChatView::write_row(QTextCursor &cursor, const Row &row) {
    QTextBlockFormat block;
    block.setTopMargin(0);
    block.setBottomMargin(8);
    block.setLineHeight(125, QTextBlockFormat::ProportionalHeight);
    cursor.setBlockFormat(block);
    QTextCharFormat name;
    name.setForeground(username_color(row.message));
    name.setFontWeight(QFont::Bold);
    name.setFontPointSize(font().pointSizeF());
    // Keep this with the displayed text, including deferred selected messages.
    // Character formats survive retention/moderation merging adjacent blocks.
    name.setProperty(translated_property, row.translation.translated);
    cursor.insertText(inline_text(row.message.name) + ":", name);
    QTextCharFormat body;
    body.setForeground(QColor(row.pending ? "#adadb8" : "#efeff1"));
    body.setFontWeight(QFont::Normal);
    body.setFontPointSize(font().pointSizeF());
    if (row.pending) {
        // Reserve a fixed space for a painted spinner. Animation never edits
        // the document, shifts its layout or changes selected/copied wording.
        body.setProperty(pending_property, row.message.id);
        cursor.insertText(" \u2003 " + ui_text("Translating…"), body);
    } else
        cursor.insertText(" " + inline_text(row.translation.translated ? row.translation.text
                                                                       : row.message.original),
                          body);
    QTextCharFormat secondary;
    secondary.setForeground(QColor("#adadb8"));
    secondary.setFontPointSize(10);
    if (model_->originals && (row.translation.translated || row.pending))
        cursor.insertText(QString(QChar::LineSeparator) + inline_text(row.message.original),
                          secondary);
    if (row.translation.uncertain)
        cursor.insertText(QString(QChar::LineSeparator) +
                              ui_text("Original shown · translation unavailable"),
                          secondary);
}
void ChatView::update_rows(int first, int last) {
    const auto selected = textCursor();
    const bool has_selection = selected.hasSelection();
    mutate([&] {
        for (int i = first; i <= last; ++i) {
            const auto block = document()->findBlockByNumber(i);
            if (!block.isValid())
                continue;
            if (mouse_down_ ||
                (has_selection && selected.selectionStart() < block.position() + block.length() &&
                 selected.selectionEnd() > block.position())) {
                deferred_.insert(model_->rows[i].message.id);
                continue;
            }
            QTextCursor cursor(block);
            cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
            write_row(cursor, model_->rows[i]);
            deferred_.remove(model_->rows[i].message.id);
        }
    });
}
void ChatView::flush_deferred() {
    if (editing_ || mouse_down_ || textCursor().hasSelection() || deferred_.isEmpty())
        return;
    for (int i = 0; i < model_->rows.size(); ++i)
        if (deferred_.contains(model_->rows[i].message.id))
            update_rows(i, i);
}
void ChatView::latest() {
    auto cursor = textCursor();
    cursor.clearSelection();
    setTextCursor(cursor);
    flush_deferred();
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    notify_reading();
}
void ChatView::paintEvent(QPaintEvent *event) {
    {
        QPainter painter(viewport());
        painter.setClipRegion(event->region());
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(QColor("#1d1d21"));
        painter.setPen(QPen(QColor("#28282e"), 1));
        auto block = cursorForPosition(QPoint(0, event->rect().top())).block();
        if (block.previous().isValid())
            block = block.previous();
        for (; block.isValid(); block = block.next()) {
            const QRectF bounds = document()
                                      ->documentLayout()
                                      ->blockBoundingRect(block)
                                      .translated(0, -verticalScrollBar()->value())
                                      .adjusted(-6, -2, 6, 2);
            if (bounds.top() > event->rect().bottom())
                break;
            const auto first = block.begin();
            if (!first.atEnd() && first.fragment().charFormat().boolProperty(translated_property) &&
                bounds.intersects(event->rect()))
                painter.drawRoundedRect(bounds, 4, 4);
        }
    }
    // Let Qt draw the text and native selection over the faint backing.
    QTextEdit::paintEvent(event);
    waiting_region_ = {};
    QPainter painter(viewport());
    painter.setClipRegion(event->region());
    painter.setRenderHint(QPainter::Antialiasing);
    auto block = cursorForPosition(QPoint(0, 0)).block();
    for (; block.isValid(); block = block.next()) {
        const auto bounds = document()->documentLayout()->blockBoundingRect(block).translated(
            0, -verticalScrollBar()->value());
        if (bounds.top() > viewport()->height())
            break;
        const int index = block.blockNumber();
        if (index >= model_->rows.size() || !model_->rows[index].pending)
            continue;
        for (auto fragment = block.begin(); !fragment.atEnd(); ++fragment) {
            const auto text = fragment.fragment();
            if (text.charFormat().property(pending_property).toString() !=
                model_->rows[index].message.id)
                continue;
            QTextCursor cursor(document());
            cursor.setPosition(text.position() + 1);
            const auto caret = cursorRect(cursor);
            const QRectF circle(caret.x() + 1, caret.center().y() - 5, 10, 10);
            if (!circle.intersects(viewport()->rect()))
                continue;
            waiting_region_ += circle.adjusted(-2, -2, 2, 2).toAlignedRect();
            painter.setPen(QPen(QColor("#adadb8"), 1.5, Qt::SolidLine, Qt::RoundCap));
            painter.drawArc(circle, spinner_angle_ * 16, 250 * 16);
        }
    }
    if (!waiting_region_.isEmpty() && isVisible()) {
        if (!spinner_.isActive())
            spinner_.start();
    } else
        spinner_.stop();
}
void ChatView::hideEvent(QHideEvent *event) {
    spinner_.stop();
    QTextEdit::hideEvent(event);
}
void ChatView::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton)
        mouse_down_ = true;
    QTextEdit::mousePressEvent(event);
    notify_reading();
}
void ChatView::mouseReleaseEvent(QMouseEvent *event) {
    QTextEdit::mouseReleaseEvent(event);
    mouse_down_ = false;
    flush_deferred();
    notify_reading();
}
void ChatView::resizeEvent(QResizeEvent *event) {
    const bool follow = following();
    QTextEdit::resizeEvent(event);
    if (follow)
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    notify_reading();
}
} // namespace awl::chat
