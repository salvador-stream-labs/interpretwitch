// SPDX-License-Identifier: GPL-2.0-or-later
#include "panel.hpp"
#include <QApplication>
#include <QTemporaryDir>
#include <QClipboard>
#include <QTextBlock>
#include <QScrollBar>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QEventLoop>
#include <QDir>
#include <QScrollArea>
#include <iostream>
#include <stdexcept>
using namespace awl::chat;
static void check(bool value, const char *reason) {
    if (!value)
        throw std::runtime_error(reason);
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    try {
        QTemporaryDir temporary;
        QSettings initial(temporary.filePath("settings.ini"), QSettings::IniFormat);
        initial.setValue("client_id", "testclient1234");
        initial.sync();
        Panel panel("missing-catalogue", temporary.path(), temporary.filePath("settings.ini"),
                    nullptr, true);
        panel.resize(390, 680);
        panel.show();
        app.processEvents();
        auto list = panel.findChild<ChatView *>();
        auto model = list->model();
        auto device = panel.findChild<QComboBox *>("awlChatRunOn");
        auto resource = panel.findChild<QComboBox *>("awlChatResourceUsage");
        check(resource && resource->count() == 2 &&
                  resource->currentData() == model_id(ModelProfile::Light),
              "Resource usage must default to Light");
        resource->setCurrentIndex(1);
        initial.sync();
        check(initial.value("model_id") == model_id(ModelProfile::Heavy),
              "Resource preference not saved");
        app.processEvents();
        check(resource->isEnabled() &&
                  !QDir(profile_directory(temporary.path(), ModelProfile::Heavy)).exists(),
              "Preview preference change started model setup");
        check(device && device->count() == 2 && device->currentData() == "cpu",
              "Run on must default to CPU");
        check(device->itemData(1) == "gpu", "GPU option missing");
#ifdef __APPLE__
        check(!(device->model()->flags(device->model()->index(1, 0)) & Qt::ItemIsEnabled),
              "Unsupported Mac GPU offered as usable");
#endif
        for (int i = 0; i < 300; ++i) {
            Event event;
            event.kind = EventKind::Message;
            event.message = {QString::number(i), i % 2 ? "bob" : "alice", "<img src=x>",
                             QString("hello %1 <script>bad()</script>").arg(i)};
            panel.receive(event);
        }
        check(model->rowCount() == 200 && list->document()->blockCount() == 200,
              "History grew beyond 200 rows");
        check(list->toPlainText().contains("<img src=x>: hello 100 <script>bad()</script>"),
              "Chat markup was interpreted or history trimmed incorrectly");
        check(list->isReadOnly() && !list->isUndoRedoEnabled(),
              "Chat allows edits or retains an undo history");
        model->translated("299", {"Привет!", true});
        check(model->data(model->index(199), Qt::DisplayRole).toString().contains("Привет"),
              "Russian translation missing");
        // Select actual Cyrillic text, copy it, then keep receiving messages and
        // translations. Selected wording must not move or change under the user.
        list->setTextCursor(list->document()->find("Привет"));
        list->copy();
        check(app.clipboard()->text() == "Привет", "Selected text could not be copied");
        app.clipboard()->clear();
        const auto copy_shortcut = QKeySequence(QKeySequence::Copy)[0];
        QKeyEvent copy_key(QEvent::KeyPress, copy_shortcut.key(),
                           copy_shortcut.keyboardModifiers());
        QApplication::sendEvent(list, &copy_key);
        check(app.clipboard()->text() == "Привет", "Native copy keyboard shortcut failed");
        Event extra;
        extra.kind = EventKind::Message;
        extra.message = {"300", "bob", "Bob", "new message"};
        panel.receive(extra);
        model->translated("299", {"Здравствуйте!", true});
        model->translated("300", {"Новое сообщение", true});
        app.processEvents();
        check(list->textCursor().selectedText() == "Привет",
              "Incoming messages changed the selection");
        check(list->toPlainText().contains("Привет!") &&
                  !list->toPlainText().contains("Здравствуйте!"),
              "Selected translation was replaced before copying");
        check(list->toPlainText().contains("Новое сообщение"),
              "Unselected translation stopped updating");
        list->latest();
        app.processEvents();
        check(!list->textCursor().hasSelection() && list->toPlainText().contains("Здравствуйте!"),
              "Deferred translation did not resume");
        // Selection spans message boundaries, with standard keyboard and mouse
        // interaction supplied by the native text widget.
        auto selection = QTextCursor(list->document()->findBlockByNumber(198));
        selection.setPosition(list->document()->characterCount() - 1, QTextCursor::KeepAnchor);
        list->setTextCursor(selection);
        list->copy();
        check(app.clipboard()->text().contains("Здравствуйте!") &&
                  app.clipboard()->text().contains("Bob: Новое сообщение"),
              "Copy across messages failed");
        list->latest();
        list->setFocus();
        auto cursor = list->document()->find("Здравствуйте");
        cursor.clearSelection();
        cursor.movePosition(QTextCursor::StartOfBlock);
        list->setTextCursor(cursor);
        QKeyEvent key(QEvent::KeyPress, Qt::Key_Right, Qt::ShiftModifier);
        QApplication::sendEvent(list, &key);
        check(list->textCursor().hasSelection(), "Keyboard selection failed");
        list->latest();
        cursor = list->document()->find("Здравствуйте");
        auto end = cursor;
        end.setPosition(cursor.selectionEnd());
        cursor.setPosition(cursor.selectionStart());
        const QPointF start_point = list->cursorRect(cursor).center(),
                      end_point = list->cursorRect(end).center();
        QMouseEvent press(QEvent::MouseButtonPress, start_point,
                          list->viewport()->mapToGlobal(start_point.toPoint()), Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QMouseEvent move(QEvent::MouseMove, end_point,
                         list->viewport()->mapToGlobal(end_point.toPoint()), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, end_point,
                            list->viewport()->mapToGlobal(end_point.toPoint()), Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(list->viewport(), &press);
        QApplication::sendEvent(list->viewport(), &move);
        QApplication::sendEvent(list->viewport(), &release);
        check(list->textCursor().selectedText() == "Здравствуйте", "Mouse drag selection failed");
        const auto before_key = list->toPlainText();
        QKeyEvent edit(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier, "x");
        QApplication::sendEvent(list, &edit);
        check(list->toPlainText() == before_key, "Typing modified read-only chat");
        list->latest();
        app.processEvents();
        check(list->following(), "Latest messages did not resume following");
        list->verticalScrollBar()->setValue(list->verticalScrollBar()->maximum() / 2);
        const auto reading = list->cursorForPosition(QPoint(2, 2)).block().text();
        extra.message.id = "301";
        panel.receive(extra);
        app.processEvents();
        check(list->cursorForPosition(QPoint(2, 2)).block().text() == reading,
              "History shifted while reading older messages");
        Event remove;
        remove.kind = EventKind::Delete;
        remove.id = "299";
        panel.receive(remove);
        model->translated("299", {"Late reply", true});
        check(model->rowCount() == 199, "Deleted result resurrected");
        remove.kind = EventKind::ClearUser;
        remove.user_id = "alice";
        panel.receive(remove);
        for (const auto &row : model->rows)
            check(row.message.user_id != "alice", "User purge failed");
        remove.kind = EventKind::Clear;
        panel.receive(remove);
        check(!model->rowCount() && list->toPlainText().isEmpty(), "Chat clear failed");
        extra.message = {"multiline", "bob", "Bob", "one\ntwo\rthree"};
        panel.receive(extra);
        check(list->document()->blockCount() == 1, "Multiline message broke the history bound");
        model->translated("multiline", {"Один\nдва", true});
        check(list->document()->blockCount() == 1, "English original broke message boundaries");
        model->show_originals(false);
        check(!list->toPlainText().contains("three"), "Show original toggle failed");
        remove.kind = EventKind::Delete;
        remove.id = "multiline";
        panel.receive(remove);
        check(list->toPlainText().isEmpty(), "Deleting the only message left stale text");
        // Pending animation is painted over fixed text: no document rewrites,
        // shifting selection or permanent timer when there is nothing to show.
        auto spinner = list->findChild<QTimer *>("awlChatPendingTimer");
        check(spinner && !spinner->isActive(), "Idle chat animates unnecessarily");
        model->show_originals(true);
        model->add({"waiting", "bob", "Bob", "Waiting original"}, true);
        app.processEvents();
        check(spinner->isActive() && list->toPlainText().contains("Translating…") &&
                  list->toPlainText().contains("Waiting original"),
              "Waiting indicator or optional original missing");
        const auto pending_text = list->toPlainText();
        const auto revision = list->document()->revision();
        list->setTextCursor(list->document()->find("Waiting original"));
        QEventLoop animation;
        QTimer::singleShot(550, &animation, &QEventLoop::quit);
        animation.exec();
        check(list->document()->revision() == revision && list->toPlainText() == pending_text &&
                  list->textCursor().selectedText() == "Waiting original",
              "Spinner changed document layout or selection");
        model->translated("waiting", {"Готово", true});
        app.processEvents();
        check(!spinner->isActive() && list->textCursor().selectedText() == "Waiting original",
              "Completed translation kept animating or disturbed copying");
        list->latest();
        app.processEvents();
        check(list->toPlainText().contains("Готово") &&
                  !list->toPlainText().contains("Translating…"),
              "Deferred pending text did not become translation");
        model->add({"cancelled", "bob", "Bob", "Cancelled original"}, true);
        model->show_originals(false);
        app.processEvents();
        check(!list->toPlainText().contains("Cancelled original"),
              "Pending original ignored visibility preference");
        list->hide();
        check(!spinner->isActive(), "Hidden chat keeps animating");
        list->show();
        app.processEvents();
        check(spinner->isActive(), "Showing pending chat did not resume animation");
        model->discarded("cancelled");
        app.processEvents();
        check(!spinner->isActive() && list->toPlainText().contains("Cancelled original") &&
                  list->toPlainText().contains("translation unavailable"),
              "Discarded translation left a spinner or hid original");
        model->add({"deleted", "bob", "Bob", "Moderated original"}, true);
        app.processEvents();
        model->remove([](const Row &row) {
            return row.message.id == "deleted";
        });
        model->translated("deleted", {"Late translation", true});
        app.processEvents();
        check(!spinner->isActive() && !list->toPlainText().contains("Moderated original") &&
                  !list->toPlainText().contains("Late translation"),
              "Moderation left a spinner or resurrected pending content");
        model->clear();
        model->add({"offscreen", "bob", "Bob", "Old waiting message"}, true);
        for (int i = 0; i < 40; ++i)
            model->add({QString::number(i), "bob", "Bob", "Later message"});
        list->latest();
        app.processEvents();
        check(!spinner->isActive(), "Offscreen pending message keeps animating");
        list->verticalScrollBar()->setValue(0);
        app.processEvents();
        check(spinner->isActive(), "Visible older pending message did not animate");
        model->clear();
        app.processEvents();
        check(!spinner->isActive(), "Cleared chat keeps animating");
        check(panel.findChildren<QLineEdit *>().size() == 2, "Unexpected credential input");
        // Restoring saved preferences must not load any model in preview mode.
        const auto restored_path = temporary.filePath("restored.ini");
        QSettings saved(restored_path, QSettings::IniFormat);
        saved.setValue("run_on", "gpu");
        saved.setValue("model_id", model_id(ModelProfile::Heavy));
        saved.sync();
        Panel restored("missing-catalogue", temporary.path(), restored_path, nullptr, true);
        auto restored_device = restored.findChild<QComboBox *>("awlChatRunOn");
        check(restored.findChild<QComboBox *>("awlChatResourceUsage")->currentData() ==
                  model_id(ModelProfile::Heavy),
              "Heavy preference not restored");
#ifdef __APPLE__
        check(restored_device->currentData() == "cpu",
              "Imported GPU preference was not normalized on Mac");
#else
        check(restored_device->currentData() == "gpu", "GPU preference not restored");
#endif
        restored_device->setCurrentIndex(1);
        restored_device->setCurrentIndex(0);
        saved.sync();
        check(saved.value("run_on") == "cpu", "Device preference not saved");
        saved.setValue("run_on", "invalid-device");
        saved.setValue("model_id", "unknown-model");
        saved.sync();
        Panel unknown("missing-catalogue", temporary.path(), restored_path, nullptr, true);
        check(unknown.findChild<QComboBox *>("awlChatRunOn")->currentData() == "cpu",
              "Invalid device preference did not default to CPU");
        check(unknown.findChild<QComboBox *>("awlChatResourceUsage")->currentData() ==
                  model_id(ModelProfile::Light),
              "Unknown model preference did not default to Light");
        panel.resize(300, 340);
        panel.findChild<QToolButton *>("awlChatConnectionToggle")->setChecked(true);
        app.processEvents();
        const auto settings_scroll = panel.findChild<QScrollArea *>("awlChatSettingsScroll");
        const auto download = panel.findChild<QPushButton *>("awlChatDownload");
        check(panel.height() == 340 && settings_scroll->verticalScrollBar()->maximum() > 0,
              "Expanded settings forced a short dock to grow");
        settings_scroll->ensureWidgetVisible(download);
        app.processEvents();
        check(download->mapTo(settings_scroll->viewport(), QPoint()).y() >= 0 &&
                  download->mapTo(settings_scroll->viewport(), QPoint(0, download->height())).y() <=
                      settings_scroll->viewport()->height(),
              "Download control is unreachable in a short dock");
        std::cout << "Native chat UI: bounded history, literal markup, mouse/keyboard selection, "
                     "copy, updates during selection, scroll retention, moderation, multiline text "
                     "and reset passed.\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
