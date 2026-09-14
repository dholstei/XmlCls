#!/usr/bin/env python3
"""Lightweight PyQt6 tree editor for the XmlCls/libxml2 DOM."""

import sys
from ctypes import c_char_p, c_void_p
from pathlib import Path

from PyQt6.QtCore import QByteArray, QMimeData, Qt
from PyQt6.QtGui import QAction, QKeySequence
from PyQt6.QtWidgets import (
    QApplication, QDialog, QFileDialog, QMainWindow, QMessageBox,
    QInputDialog, QPlainTextEdit, QTreeWidget, QTreeWidgetItem, QVBoxLayout,
)

from XmlCls import XmlCls, XmlNode


XML_NODE_ROLE = int(Qt.ItemDataRole.UserRole)
CUT_MIME = "application/x-xmlcls-cut"


class DirectEditor(QDialog):
    """Edit one node as XML and return the replacement text on acceptance."""

    def __init__(self, xml: str, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Direct XML Editor")
        self.resize(800, 600)

        self.editor = QPlainTextEdit(self)
        self.editor.setPlainText(xml)

        save = QAction("Save", self)
        save.setShortcut(QKeySequence.StandardKey.Save)
        save.triggered.connect(self.accept)
        self.addAction(save)

        close = QAction("Close", self)
        close.setShortcut(QKeySequence.StandardKey.Cancel)
        close.triggered.connect(self.reject)
        self.addAction(close)

        layout = QVBoxLayout(self)
        layout.addWidget(self.editor)

    def xml(self) -> str:
        return self.editor.toPlainText()


class XmlClsEditor(QMainWindow):
    """Present one XmlCls DOM as one QTreeWidget."""

    def __init__(self, filename: str | None = None):
        super().__init__()
        self.dom: XmlCls | None = None
        self.filename: str | None = None
        self.dirty = False
        self.cut_node: c_void_p | None = None

        QApplication.clipboard().dataChanged.connect(self.clipboard_changed)

        self.tree = QTreeWidget(self)
        self.tree.setHeaderLabels(["Element", "Content"])
        self.tree.setAlternatingRowColors(True)
        self.setCentralWidget(self.tree)

        self._create_actions()
        self._create_menus()
        self.resize(1000, 700)
        self._update_title()

        if filename:
            self.open_file(filename)

    def _create_actions(self):
        self.open_action = QAction("&Open...", self)
        self.open_action.setShortcut(QKeySequence.StandardKey.Open)
        self.open_action.triggered.connect(self.open)

        self.save_action = QAction("&Save", self)
        self.save_action.setShortcut(QKeySequence.StandardKey.Save)
        self.save_action.triggered.connect(self.save)

        self.quit_action = QAction("&Quit", self)
        self.quit_action.setShortcut(QKeySequence.StandardKey.Quit)
        self.quit_action.triggered.connect(self.close)

        self.copy_action = QAction("&Copy", self)
        self.copy_action.setShortcut(QKeySequence.StandardKey.Copy)
        self.copy_action.triggered.connect(self.copy_xml)

        self.cut_action = QAction("Cu&t", self)
        self.cut_action.setShortcut(QKeySequence.StandardKey.Cut)
        self.cut_action.triggered.connect(self.cut_xml)

        self.paste_action = QAction("&Paste", self)
        self.paste_action.setShortcut(QKeySequence.StandardKey.Paste)
        self.paste_action.triggered.connect(lambda: self.paste_xml("AddChild"))

        self.paste_after_action = QAction("Paste &After", self)
        self.paste_after_action.triggered.connect(lambda: self.paste_xml("AddAfter"))

        self.paste_before_action = QAction("Paste &Before", self)
        self.paste_before_action.triggered.connect(lambda: self.paste_xml("AddBefore"))

        self.direct_action = QAction("&Direct...", self)
        self.direct_action.triggered.connect(self.direct_edit)

        # The editor's motivating operation; remove if Delete belongs elsewhere.
        self.delete_action = QAction("&Delete", self)
        self.delete_action.setShortcut(QKeySequence.StandardKey.Delete)
        self.delete_action.triggered.connect(self.delete_node)

        self.collapse_all_action = QAction("&Collapse All", self)
        self.collapse_all_action.setShortcut(QKeySequence("Ctrl+Up"))
        self.collapse_all_action.triggered.connect(self.tree.collapseAll)

        self.expand_all_action = QAction("&Expand All", self)
        self.expand_all_action.setShortcut(QKeySequence("Ctrl+Down"))
        self.expand_all_action.triggered.connect(self.tree.expandAll)

        self.create_journal_action = QAction("&Create", self)
        self.create_journal_action.triggered.connect(self.create_journal)

        self.undo_journal_action = QAction("&Undo", self)
        self.undo_journal_action.setShortcut(QKeySequence.StandardKey.Undo)
        self.undo_journal_action.triggered.connect(self.undo_journal)

        self.redo_journal_action = QAction("&Redo", self)
        self.redo_journal_action.setShortcut(QKeySequence.StandardKey.Redo)
        self.redo_journal_action.triggered.connect(self.redo_journal)

        self.mark_release_action = QAction("Mark &Release...", self)
        self.mark_release_action.triggered.connect(self.mark_release)

        self.mark_restore_point_action = QAction("Mark Restore &Point...", self)
        self.mark_restore_point_action.triggered.connect(self.mark_restore_point)

    def _create_menus(self):
        file_menu = self.menuBar().addMenu("&File")
        file_menu.addAction(self.open_action)
        file_menu.addAction(self.save_action)
        file_menu.addSeparator()
        file_menu.addAction(self.quit_action)

        edit_menu = self.menuBar().addMenu("&Edit")
        edit_menu.addAction(self.copy_action)
        edit_menu.addAction(self.cut_action)
        edit_menu.addAction(self.paste_action)
        edit_menu.addAction(self.paste_after_action)
        edit_menu.addAction(self.paste_before_action)
        edit_menu.addAction(self.direct_action)
        edit_menu.addSeparator()
        edit_menu.addAction(self.delete_action)

        view_menu = self.menuBar().addMenu("&View")
        view_menu.addAction(self.collapse_all_action)
        view_menu.addAction(self.expand_all_action)

        self.journal_menu = self.menuBar().addMenu("&Journal")
        self.journal_menu.addAction(self.create_journal_action)
        self.journal_menu.addAction(self.undo_journal_action)
        self.journal_menu.addAction(self.redo_journal_action)
        self.journal_menu.addSeparator()
        self.journal_menu.addAction(self.mark_release_action)
        self.journal_menu.addAction(self.mark_restore_point_action)
        self.restore_menu = self.journal_menu.addMenu("&Restore")
        self.restore_menu.setToolTipsVisible(True)
        self.journal_menu.aboutToShow.connect(self.refresh_journal_menu)

    def _update_title(self):
        name = Path(self.filename).name if self.filename else "Untitled"
        mark = "*" if self.dirty else ""
        self.setWindowTitle(f"{name}{mark} - XmlClsEdit")

    def _set_dirty(self, dirty=True):
        self.dirty = dirty
        self._update_title()

    def _confirm_discard(self) -> bool:
        if not self.dirty:
            return True
        answer = QMessageBox.question(
            self, "Unsaved Changes", "Discard the unsaved changes?",
            QMessageBox.StandardButton.Discard | QMessageBox.StandardButton.Cancel,
            QMessageBox.StandardButton.Cancel,
        )
        return answer == QMessageBox.StandardButton.Discard

    def open(self):
        if not self._confirm_discard():
            return
        filename, _ = QFileDialog.getOpenFileName(self, "Open XML", "", "XML files (*.xml);;All files (*)")
        if filename:
            self.open_file(filename)

    def open_file(self, filename: str):
        self.cancel_cut()
        dom = XmlCls(c_char_p(str(filename).encode("utf-8")))
        if not dom.doc:
            self._error("Open failed", getattr(dom.err, "msg", "Unable to parse the XML file"))
            return
        self.dom = dom
        self.filename = str(filename)
        if self.dom.err:
            self._error("Open journal failed", self._dom_error())
        self._set_dirty(False)
        self.populate_tree()
        self.refresh_journal_menu()

    def refresh_journal_menu(self):
        enabled = bool(self.dom and self.dom.HasJournal())
        declared = bool(self.dom and self.dom.XPath("boolean(/*/@JRNL)", bool))
        self.create_journal_action.setEnabled(bool(self.dom) and not enabled and not declared)
        self.undo_journal_action.setEnabled(enabled)
        self.redo_journal_action.setEnabled(enabled)
        self.mark_release_action.setEnabled(enabled)
        self.mark_restore_point_action.setEnabled(enabled)
        self.restore_menu.setEnabled(enabled)
        self.restore_menu.clear()

        if not enabled:
            return
        points = self.dom.RestorePoints()
        if not points:
            empty = self.restore_menu.addAction("(No restore points)")
            empty.setEnabled(False)
            return
        for point in reversed(points):
            action = self.restore_menu.addAction(point.jid)
            action.setToolTip(point.note)
            action.setStatusTip(point.note)
            action.triggered.connect(lambda checked=False, jid=point.jid: self.restore(jid))

    def create_journal(self):
        if not self.dom or not self.filename:
            return
        filename = f"{Path(self.filename).name}.jrnl"
        if self.dom.CreateJournal(filename):
            self.populate_tree()
            self._set_dirty(True)
            self.refresh_journal_menu()
        else:
            self._error("Create journal failed", self._dom_error())

    def _journal_comment(self, title: str) -> tuple[str, bool]:
        return QInputDialog.getMultiLineText(self, title, "Comment:")

    def mark_release(self):
        note, accepted = self._journal_comment("Mark Release")
        if not accepted:
            return
        if not self.dom.MarkRelease(note):
            self._error("Mark release failed", self._dom_error())

    def undo_journal(self):
        if not self.dom or not self.dom.Undo():
            self._error("Undo failed", self._dom_error())
            return
        self.populate_tree()
        self._set_dirty(True)
        self.refresh_journal_menu()

    def redo_journal(self):
        if not self.dom or not self.dom.Redo():
            self._error("Redo failed", self._dom_error())
            return
        self.populate_tree()
        self._set_dirty(True)
        self.refresh_journal_menu()

    def mark_restore_point(self):
        note, accepted = self._journal_comment("Mark Restore Point")
        if not accepted:
            return
        if not self.dom.MarkRestorePoint(note):
            self._error("Mark restore point failed", self._dom_error())
            return
        self._set_dirty(True)
        self.refresh_journal_menu()

    def restore(self, jid: str):
        answer = QMessageBox.question(
            self, "Restore Journal State", f"Restore the document to state {jid}?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.Cancel,
            QMessageBox.StandardButton.Cancel,
        )
        if answer != QMessageBox.StandardButton.Yes:
            return
        if not self.dom.Restore(jid):
            self._error("Restore failed", self._dom_error())
            return
        self.populate_tree()
        self._set_dirty(True)
        self.refresh_journal_menu()

    def save(self):
        if not self.dom:
            return
        if not self.filename:
            filename, _ = QFileDialog.getSaveFileName(self, "Save XML", "", "XML files (*.xml);;All files (*)")
            if not filename:
                return
            self.filename = filename

        if not self.dom.Save(self.filename):
            self._error("Save failed", self._dom_error())
            return

        self._set_dirty(False)

    def populate_tree(self):
        self.tree.clear()
        if not self.dom:
            return
        root = self.dom.XPath("/*", XmlNode)
        if root:
            self._add_node(None, root)
            self.tree.expandToDepth(1)

    def _add_node(self, parent: QTreeWidgetItem | None, node: XmlNode):
        name = node.XPath("name()", str)
        content = self._content_summary(node)
        tooltip = self._attribute_tooltip(node)
        item = QTreeWidgetItem([name, content])
        item.setData(0, XML_NODE_ROLE, c_void_p(node.node.value))
        item.setToolTip(0, tooltip)
        item.setToolTip(1, tooltip)
        if parent is None:
            self.tree.addTopLevelItem(item)
        else:
            parent.addChild(item)

        for child in node.XPath("./*", list[XmlNode]):
            self._add_node(item, child)

    @staticmethod
    def _content_summary(node: XmlNode) -> str:
        text = node.XPath("normalize-space(text())", str)
        if len(text) > 100:
            text = text[:97] + "..."
        return text

    @staticmethod
    def _attribute_tooltip(node: XmlNode) -> str:
        return "\n".join(
            f'{attribute.XPath("name(.)", str)}    {attribute.XPath("string(.)", str)}'
            for attribute in node.XPath("./@*", list[XmlNode])
        )

    def current_node(self) -> XmlNode | None:
        if not self.dom:
            return None
        item = self.tree.currentItem()
        ptr = item.data(0, XML_NODE_ROLE) if item else None
        if not ptr:
            return None
        return XmlNode(self.dom, ptr if isinstance(ptr, c_void_p) else c_void_p(ptr))

    def copy_xml(self):
        node = self.current_node()
        if node:
            QApplication.clipboard().setText(node.XML())

    def cut_xml(self):
        node = self.current_node()
        if not node:
            return

        mime = QMimeData()
        mime.setText(node.XML())
        mime.setData(CUT_MIME, QByteArray(b"cut"))
        self.cut_node = c_void_p(node.node.value)
        QApplication.clipboard().setMimeData(mime)

    def clipboard_changed(self):
        mime = QApplication.clipboard().mimeData()
        if not mime or not mime.hasFormat(CUT_MIME):
            self.cut_node = None

    def cancel_cut(self):
        self.cut_node = None
        clipboard = QApplication.clipboard()
        mime = clipboard.mimeData()
        if mime and mime.hasFormat(CUT_MIME):
            clipboard.setText(mime.text())

    def paste_xml(self, operation: str):
        node = self.current_node()
        clipboard = QApplication.clipboard()
        mime = clipboard.mimeData()
        xml = clipboard.text().strip()
        if not node or not xml:
            return

        if self.cut_node and mime and mime.hasFormat(CUT_MIME):
            source = XmlNode(self.dom, c_void_p(self.cut_node.value))
            move = {"AddChild": "MoveChild", "AddBefore": "MoveBefore", "AddAfter": "MoveAfter"}[operation]
            self._mutate(source, move, node)
            return
        self._mutate(node, operation, xml)

    def direct_edit(self):
        node = self.current_node()
        if not node:
            return
        editor = DirectEditor(node.XML(), self)
        if editor.exec() == QDialog.DialogCode.Accepted:
            self._mutate(node, "parse", editor.xml())

    def delete_node(self):
        node = self.current_node()
        if node:
            self._mutate(node, "Delete")

    def _mutate(self, node: XmlNode, operation: str, *args):
        try:
            result = getattr(node, operation)(*args)
        except Exception as exc:
            self._error(f"{operation} failed", str(exc))
            return
        if not result:
            self._error(f"{operation} failed", self._dom_error())
            return
        self.populate_tree()
        self._set_dirty(True)
        self.cancel_cut()

    def _dom_error(self) -> str:
        if self.dom and getattr(self.dom, "err", None):
            return self.dom.err.msg or self.dom.err.data or "Unknown XML error"
        return "Unknown XML error"

    def _error(self, title: str, message: str):
        QMessageBox.critical(self, title, message)

    def closeEvent(self, event):
        if self._confirm_discard():
            event.accept()
        else:
            event.ignore()


def main() -> int:
    app = QApplication(sys.argv)
    filename = sys.argv[1] if len(sys.argv) > 1 else None
    window = XmlClsEditor(filename)
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
