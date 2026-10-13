// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_menu.hpp"
#include "ui_text.hpp"
#include <QLineEdit>
#include <QTextEdit>
#include <QMenu>

namespace awl::chat {
void install_text_menu(QLineEdit *edit) {
    edit->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(edit, &QWidget::customContextMenuRequested, edit, [edit](QPoint position) {
        QMenu menu(edit);
        menu.addAction(ui_text("Undo"), edit, &QLineEdit::undo, QKeySequence::Undo)
            ->setEnabled(!edit->isReadOnly() && edit->isUndoAvailable());
        menu.addAction(ui_text("Redo"), edit, &QLineEdit::redo, QKeySequence::Redo)
            ->setEnabled(!edit->isReadOnly() && edit->isRedoAvailable());
        menu.addSeparator();
        menu.addAction(ui_text("Cut"), edit, &QLineEdit::cut, QKeySequence::Cut)
            ->setEnabled(!edit->isReadOnly() && edit->hasSelectedText());
        menu.addAction(ui_text("Copy"), edit, &QLineEdit::copy, QKeySequence::Copy)
            ->setEnabled(edit->hasSelectedText());
        menu.addAction(ui_text("Paste"), edit, &QLineEdit::paste, QKeySequence::Paste)
            ->setEnabled(!edit->isReadOnly());
        menu.addAction(ui_text("Delete"), edit, &QLineEdit::del)
            ->setEnabled(!edit->isReadOnly() && edit->hasSelectedText());
        menu.addSeparator();
        menu.addAction(ui_text("Select all"), edit, &QLineEdit::selectAll, QKeySequence::SelectAll)
            ->setEnabled(!edit->text().isEmpty());
        menu.exec(edit->mapToGlobal(position));
    });
}
void install_text_menu(QTextEdit *view) {
    view->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(view, &QWidget::customContextMenuRequested, view, [view](QPoint position) {
        QMenu menu(view);
        menu.addAction(ui_text("Copy"), view, &QTextEdit::copy, QKeySequence::Copy)
            ->setEnabled(view->textCursor().hasSelection());
        menu.addAction(ui_text("Select all"), view, &QTextEdit::selectAll, QKeySequence::SelectAll)
            ->setEnabled(!view->document()->isEmpty());
        menu.exec(view->viewport()->mapToGlobal(position));
    });
}
} // namespace awl::chat
