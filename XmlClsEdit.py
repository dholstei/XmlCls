#!/usr/bin/env python3
"""Lightweight PyQt6 tree editor for the XmlCls/libxml2 DOM."""

import sys
from ctypes import c_char_p, c_void_p
from pathlib import Path

from PyQt6.QtCore import Qt
from PyQt6.QtGui import QAction, QKeySequence
from PyQt6.QtWidgets import (
    QApplication, QDialog, QFileDialog, QMainWindow, QMessageBox,
    QPlainTextEdit, QTreeWidget, QTreeWidgetItem, QVBoxLayout,
)

from XmlCls import XmlCls, XmlNode


XML_NODE_ROLE = int(Qt.ItemDataRole.UserRole)


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
        self.open_action = QAction("&Open…", self)
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

        self.paste_action = QAction("&Paste", self)
        self.paste_action.setShortcut(QKeySequence.StandardKey.Paste)
        self.paste_action.triggered.connect(lambda: self.paste_xml("AddChild"))

        self.paste_after_action = QAction("Paste &After", self)
        self.paste_after_action.triggered.connect(lambda: self.paste_xml("AddAfter"))

        self.paste_before_action = QAction("Paste &Before", self)
        self.paste_before_action.triggered.connect(lambda: self.paste_xml("AddBefore"))

        self.direct_action = QAction("&Direct…", self)
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

    def _create_menus(self):
        file_menu = self.menuBar().addMenu("&File")
        file_menu.addAction(self.open_action)
        file_menu.addAction(self.save_action)
        file_menu.addSeparator()
        file_menu.addAction(self.quit_action)

        edit_menu = self.menuBar().addMenu("&Edit")
        edit_menu.addAction(self.copy_action)
        edit_menu.addAction(self.paste_action)
        edit_menu.addAction(self.paste_after_action)
        edit_menu.addAction(self.paste_before_action)
        edit_menu.addAction(self.direct_action)
        edit_menu.addSeparator()
        edit_menu.addAction(self.delete_action)

        view_menu = self.menuBar().addMenu("&View")
        view_menu.addAction(self.collapse_all_action)
        view_menu.addAction(self.expand_all_action)

    def _update_title(self):
        name = Path(self.filename).name if self.filename else "Untitled"
        mark = "*" if self.dirty else ""
        self.setWindowTitle(f"{name}{mark} — XmlClsEdit")

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
        dom = XmlCls(c_char_p(str(filename).encode("utf-8")))
        if not dom.doc:
            self._error("Open failed", getattr(dom.err, "msg", "Unable to parse the XML file"))
            return
        self.dom = dom
        self.filename = str(filename)
        self._set_dirty(False)
        self.populate_tree()

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
        item = QTreeWidgetItem([name, content])
        item.setData(0, XML_NODE_ROLE, c_void_p(node.node.value))
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
            text = text[:97] + "…"
        return text

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

    def paste_xml(self, operation: str):
        node = self.current_node()
        xml = QApplication.clipboard().text().strip()
        if not node or not xml:
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
