from ctypes import *
from collections.abc import Callable
from typing import ClassVar
from dataclasses import dataclass
from enum import IntEnum
from pathlib import Path
import sys
from xml.etree import ElementTree


class CError(Structure):
    _fields_ = [
        ("level", c_int),
        ("msg", c_char_p),
        ("data", c_char_p),
    ]


CErrorPtr = POINTER(CError)


def PrintError(error: "Error") -> None:
    """Default Error publisher for command-line applications."""

    print(f"level: {error.level.name}", file=sys.stderr)
    print(f"msg:   {error.msg}", file=sys.stderr)
    print(f"data:  {error.data}", file=sys.stderr)


class Error:
    class lvl(IntEnum):
        NOERR = 0
        INFO  = 1
        WARN  = 2
        ERR   = 3

    publisher: ClassVar[Callable[["Error"], None]] = PrintError

    def __init__(self, source, msg: str = "", data: str = ""):
        if isinstance(source, Error):
            self.level = source.level
            self.msg = source.msg
            self.data = source.data
            return

        if isinstance(source, (c_void_p, CErrorPtr)):
            pointer = cast(source, CErrorPtr)
            if not pointer:
                raise ValueError("Cannot construct Error from a NULL CError pointer")

            if XmlCls._lib is None:
                XmlCls._load_library()

            try:
                self.level = Error.lvl(pointer.contents.level)
                self.msg = pointer.contents.msg.decode("utf-8", errors="replace") if pointer.contents.msg else ""
                self.data = pointer.contents.data.decode("utf-8", errors="replace") if pointer.contents.data else ""
            finally:
                XmlCls._lib.FreeCError(pointer)
            return

        self.level = Error.lvl(source)
        self.msg = msg
        self.data = data

    def __bool__(self):
        return self.level >= Error.lvl.ERR

    def publish(self) -> None:
        """Publish through the application-selected error presentation."""

        type(self).publisher(self)


@dataclass(frozen=True)
class RestorePoint:
    jid: str
    note: str = ""


class XmlCls:
    """
    Python adapter for the XmlCls C ABI.

    All XML parsing, XPath evaluation, error generation, mutation, and
    journaling are performed by XmlClsLib.so.  Python owns only the C++
    XmlDoc handle returned by the C interface.
    """

    _lib: ClassVar[CDLL | None] = None

    def __init__(self, source):
        if XmlCls._lib is None:
            XmlCls._load_library()

        self.doc = c_void_p()
        self.err = None

        if isinstance(source, c_void_p):
            self._attach(source)
        elif isinstance(source, str):
            self._from_xml(source)
        elif isinstance(source, c_char_p):
            self._from_file(source)
        else:
            raise TypeError("XmlCls source must be c_void_p, str XML, or c_char_p filename")

    @classmethod
    def _load_library(cls):
        cls._lib = CDLL(str(Path(__file__).resolve().with_name("XmlClsLib.so")))

        # Error ownership.
        cls._lib.FreeCError.argtypes = [CErrorPtr]
        cls._lib.FreeCError.restype = None

        cls._lib.XmlDoc_Error.argtypes = []
        cls._lib.XmlDoc_Error.restype = CErrorPtr

        # XmlDoc lifetime/construction.
        cls._lib.XmlDoc_Attach.argtypes = [c_void_p]
        cls._lib.XmlDoc_Attach.restype = c_void_p
        cls._lib.XmlDoc_FromXML.argtypes = [c_char_p]
        cls._lib.XmlDoc_FromXML.restype = c_void_p
        cls._lib.XmlDoc_FromFile.argtypes = [c_char_p]
        cls._lib.XmlDoc_FromFile.restype = c_void_p
        cls._lib.XmlDoc_Detach.argtypes = [c_void_p]
        cls._lib.XmlDoc_Detach.restype = None

        # XmlDoc operations.
        cls._lib.XmlDoc_Save.argtypes = [c_void_p, c_char_p]
        cls._lib.XmlDoc_Save.restype = None
        cls._lib.XmlDoc_OpenJournal.argtypes = [c_void_p, c_char_p]
        cls._lib.XmlDoc_OpenJournal.restype = None
        cls._lib.XmlDoc_CreateJournal.argtypes = [c_void_p, c_char_p, c_char_p]
        cls._lib.XmlDoc_CreateJournal.restype = None
        cls._lib.XmlDoc_HasJournal.argtypes = [c_void_p]
        cls._lib.XmlDoc_HasJournal.restype = c_int
        cls._lib.XmlDoc_Journal.argtypes = [c_void_p]
        cls._lib.XmlDoc_Journal.restype = c_void_p

        # XmlJrnl operations.
        cls._lib.XmlJrnl_Undo.argtypes = [c_void_p]
        cls._lib.XmlJrnl_Undo.restype = None
        cls._lib.XmlJrnl_Redo.argtypes = [c_void_p]
        cls._lib.XmlJrnl_Redo.restype = None
        cls._lib.XmlJrnl_MarkRelease.argtypes = [c_void_p, c_char_p]
        cls._lib.XmlJrnl_MarkRelease.restype = None
        cls._lib.XmlJrnl_StampState.argtypes = [c_void_p, c_char_p, c_char_p]
        cls._lib.XmlJrnl_StampState.restype = c_char_p
        cls._lib.XmlJrnl_Restore.argtypes = [c_void_p, c_char_p]
        cls._lib.XmlJrnl_Restore.restype = None
        cls._lib.XmlJrnl_RestorePoints.argtypes = [c_void_p]
        cls._lib.XmlJrnl_RestorePoints.restype = c_char_p

        # XPath.  Scalar functions return the C++ value directly.
        # Node functions return the underlying xmlNodePtr; ownership remains
        # with the XmlDoc.
        cls._lib.XmlDoc_XPathString.argtypes = [c_void_p, c_void_p, c_char_p]
        cls._lib.XmlDoc_XPathString.restype = c_char_p
        cls._lib.XmlDoc_XPathDouble.argtypes = [c_void_p, c_void_p, c_char_p]
        cls._lib.XmlDoc_XPathDouble.restype = c_double
        cls._lib.XmlDoc_XPathInt.argtypes = [c_void_p, c_void_p, c_char_p]
        cls._lib.XmlDoc_XPathInt.restype = c_int
        cls._lib.XmlDoc_XPathBool.argtypes = [c_void_p, c_void_p, c_char_p]
        cls._lib.XmlDoc_XPathBool.restype = c_int
        cls._lib.XmlDoc_XPathNode.argtypes = [c_void_p, c_void_p, c_char_p]
        cls._lib.XmlDoc_XPathNode.restype = c_void_p
        cls._lib.XmlDoc_XPathNodes.argtypes = [c_void_p, c_void_p, c_char_p, POINTER(c_void_p), c_size_t]
        cls._lib.XmlDoc_XPathNodes.restype = c_size_t

        # XmlNode operations.
        cls._lib.XmlNode_Error.argtypes = []
        cls._lib.XmlNode_Error.restype = CErrorPtr
        cls._lib.XmlNode_GetPath.argtypes = [c_void_p]
        cls._lib.XmlNode_GetPath.restype = c_char_p
        cls._lib.XmlNode_XML.argtypes = [c_void_p]
        cls._lib.XmlNode_XML.restype = c_char_p
        cls._lib.XmlNode_Parse.argtypes = [c_void_p, c_char_p]
        cls._lib.XmlNode_Parse.restype = c_void_p
        cls._lib.XmlNode_AddChild.argtypes = [c_void_p, c_char_p]
        cls._lib.XmlNode_AddChild.restype = c_void_p
        cls._lib.XmlNode_AddBefore.argtypes = [c_void_p, c_char_p]
        cls._lib.XmlNode_AddBefore.restype = c_void_p
        cls._lib.XmlNode_AddAfter.argtypes = [c_void_p, c_char_p]
        cls._lib.XmlNode_AddAfter.restype = c_void_p
        cls._lib.XmlNode_MoveChild.argtypes = [c_void_p, c_void_p]
        cls._lib.XmlNode_MoveChild.restype = None
        cls._lib.XmlNode_MoveBefore.argtypes = [c_void_p, c_void_p]
        cls._lib.XmlNode_MoveBefore.restype = None
        cls._lib.XmlNode_MoveAfter.argtypes = [c_void_p, c_void_p]
        cls._lib.XmlNode_MoveAfter.restype = None
        cls._lib.XmlNode_Delete.argtypes = [c_void_p]
        cls._lib.XmlNode_Delete.restype = None

    @staticmethod
    def _decode(value) -> str:
        return value.decode("utf-8", errors="replace") if value else ""

    def _failed(self) -> bool:
        error = self._lib.XmlDoc_Error()
        self.err = Error(error) if error else None
        return bool(self.err)

    def _attach(self, doc: c_void_p):
        if not doc or not doc.value:
            raise ValueError("Invalid NULL xmlDocPtr")

        owner = self._lib.XmlDoc_Attach(doc)
        if not owner:
            raise RuntimeError("Unable to attach XmlDoc")

        self.doc = c_void_p(owner)
        error = self._lib.XmlDoc_Error()
        self.err = Error(error) if error else None

    def _from_xml(self, XML: str):
        owner = self._lib.XmlDoc_FromXML(XML.encode("utf-8"))
        if not owner:
            raise RuntimeError("Unable to construct XmlDoc from XML")

        self.doc = c_void_p(owner)
        error = self._lib.XmlDoc_Error()
        self.err = Error(error) if error else None

    def _from_file(self, filename: c_char_p):
        owner = self._lib.XmlDoc_FromFile(filename)
        if not owner:
            name = self._decode(filename.value)
            raise RuntimeError(f"Unable to open XML file: {name}")

        self.doc = c_void_p(owner)
        error = self._lib.XmlDoc_Error()
        self.err = Error(error) if error else None

    def close(self):
        if self.doc:
            self._lib.XmlDoc_Detach(self.doc)
            self.doc = c_void_p()

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass

    def Save(self, filename: str) -> bool:
        if self.doc:
            self._lib.XmlDoc_Save(self.doc, filename.encode("utf-8"))
        error = self._lib.XmlDoc_Error()
        self.err = Error(error) if error else None
        return not self.err

    def OpenJournal(self, filename: str) -> bool:
        if self.doc:
            self._lib.XmlDoc_OpenJournal(self.doc, filename.encode("utf-8"))
        error = self._lib.XmlDoc_Error()
        self.err = Error(error) if error else None
        return not self.err

    def CreateJournal(self, filename: str, XML: str = "") -> bool:
        if self.doc:
            self._lib.XmlDoc_CreateJournal(
                self.doc, filename.encode("utf-8"), XML.encode("utf-8"))
        error = self._lib.XmlDoc_Error()
        self.err = Error(error) if error else None
        return not self.err

    def Undo(self) -> bool:
        journal = self._lib.XmlDoc_Journal(self.doc) if self.doc else None
        if journal:
            self._lib.XmlJrnl_Undo(journal)
        error = self._lib.XmlDoc_Error()
        self.err = Error(error) if error else None
        return not self.err

    def Redo(self) -> bool:
        journal = self._lib.XmlDoc_Journal(self.doc) if self.doc else None
        if journal:
            self._lib.XmlJrnl_Redo(journal)
        error = self._lib.XmlDoc_Error()
        self.err = Error(error) if error else None
        return not self.err

    def HasJournal(self) -> bool:
        return bool(self.doc and self._lib.XmlDoc_HasJournal(self.doc))

    def MarkRelease(self, note: str = "") -> bool:
        journal = self._lib.XmlDoc_Journal(self.doc) if self.doc else None
        if journal:
            self._lib.XmlJrnl_MarkRelease(journal, note.encode("utf-8"))
        error = self._lib.XmlDoc_Error()
        self.err = Error(error) if error else None
        return not self.err

    def MarkRestorePoint(self, note: str = "") -> str:
        journal = self._lib.XmlDoc_Journal(self.doc) if self.doc else None
        jid = self._lib.XmlJrnl_StampState(
            journal, b"RestorePoint", note.encode("utf-8")
        ) if journal else None
        error = self._lib.XmlDoc_Error()
        self.err = Error(error) if error else None
        return self._decode(jid) if not self.err else ""

    def RestorePoints(self) -> list[RestorePoint]:
        journal = self._lib.XmlDoc_Journal(self.doc) if self.doc else None
        XML = self._lib.XmlJrnl_RestorePoints(journal) if journal else None
        error = self._lib.XmlDoc_Error()
        self.err = Error(error) if error else None
        if self.err or not XML:
            return []

        root = ElementTree.fromstring(self._decode(XML))
        return [RestorePoint(state.get("JID", ""), state.get("Note", "")) for state in root]

    def Restore(self, jid: str) -> bool:
        journal = self._lib.XmlDoc_Journal(self.doc) if self.doc else None
        if journal:
            self._lib.XmlJrnl_Restore(journal, jid.encode("ascii"))
        error = self._lib.XmlDoc_Error()
        self.err = Error(error) if error else None
        return not self.err

    @staticmethod
    def _XPathDefault(type):
        if type is str:
            return ""
        if type is float:
            return 0.0
        if type is int:
            return 0
        if type is bool:
            return False
        if type == list[XmlNode]:
            return []
        return None

    def XPath(self, query: str, type=None, node=None):
        if type is None:
            type = XmlNode

        context = node.node if isinstance(node, XmlNode) else node
        context = context or c_void_p()
        q = query.encode("utf-8")

        if type is XmlNode:
            value = self._lib.XmlDoc_XPathNode(self.doc, context, q)
            error = self._lib.XmlDoc_Error()
            self.err = Error(error) if error else None
            return XmlNode(self, c_void_p(value)) if value and not self.err else None

        if type == list[XmlNode]:
            count = self._lib.XmlDoc_XPathNodes(self.doc, context, q, None, 0)
            error = self._lib.XmlDoc_Error()
            self.err = Error(error) if error else None
            if self.err or not count:
                return []

            nodes = (c_void_p * count)()
            actual = self._lib.XmlDoc_XPathNodes(self.doc, context, q, nodes, count)
            error = self._lib.XmlDoc_Error()
            self.err = Error(error) if error else None
            if self.err:
                return []

            return [XmlNode(self, c_void_p(nodes[i])) for i in range(actual)]

        if type is str:
            value = self._lib.XmlDoc_XPathString(self.doc, context, q)
            error = self._lib.XmlDoc_Error()
            self.err = Error(error) if error else None
            return self._decode(value) if not self.err else ""

        if type is float:
            value = self._lib.XmlDoc_XPathDouble(self.doc, context, q)
            error = self._lib.XmlDoc_Error()
            self.err = Error(error) if error else None
            return value if not self.err else 0.0

        if type is int:
            value = self._lib.XmlDoc_XPathInt(self.doc, context, q)
            error = self._lib.XmlDoc_Error()
            self.err = Error(error) if error else None
            return value if not self.err else 0

        if type is bool:
            value = self._lib.XmlDoc_XPathBool(self.doc, context, q)
            error = self._lib.XmlDoc_Error()
            self.err = Error(error) if error else None
            return bool(value) if not self.err else False

        raise TypeError(f"Unsupported XPath return type: {type}")


class XmlNode:
    def __init__(self, owner: XmlCls, node: c_void_p):
        self.owner = owner
        self.node = c_void_p(node.value if isinstance(node, c_void_p) else node)

    @property
    def err(self):
        return self.owner.err

    def XPath(self, query: str, type=None):
        return self.owner.XPath(query, XmlNode if type is None else type, node=self)

    def GetPath(self) -> str:
        value = self.owner._lib.XmlNode_GetPath(self.node)
        error = self.owner._lib.XmlNode_Error()
        self.owner.err = Error(error) if error else None
        return self.owner._decode(value) if not self.owner.err else ""

    def XML(self) -> str:
        value = self.owner._lib.XmlNode_XML(self.node)
        error = self.owner._lib.XmlNode_Error()
        self.owner.err = Error(error) if error else None
        return self.owner._decode(value) if not self.owner.err else ""

    def parse(self, XML: str) -> bool:
        node = self.owner._lib.XmlNode_Parse(self.node, XML.encode("utf-8"))
        error = self.owner._lib.XmlNode_Error()
        self.owner.err = Error(error) if error else None
        if not node or self.owner.err:
            return False

        self.node = c_void_p(node)
        return True

    def _Add(self, function, XML: str):
        node = function(self.node, XML.encode("utf-8"))
        error = self.owner._lib.XmlNode_Error()
        self.owner.err = Error(error) if error else None
        return XmlNode(self.owner, c_void_p(node)) if node and not self.owner.err else None

    def AddChild(self, XML: str):
        return self._Add(self.owner._lib.XmlNode_AddChild, XML)

    def AddBefore(self, XML: str):
        return self._Add(self.owner._lib.XmlNode_AddBefore, XML)

    def AddAfter(self, XML: str):
        return self._Add(self.owner._lib.XmlNode_AddAfter, XML)

    def _Move(self, function, destination: "XmlNode") -> bool:
        if not isinstance(destination, XmlNode):
            raise TypeError("destination must be an XmlNode")

        function(self.node, destination.node)
        error = self.owner._lib.XmlNode_Error()
        self.owner.err = Error(error) if error else None
        return not self.owner.err

    def MoveChild(self, parent: "XmlNode") -> bool:
        return self._Move(self.owner._lib.XmlNode_MoveChild, parent)

    def MoveBefore(self, sibling: "XmlNode") -> bool:
        return self._Move(self.owner._lib.XmlNode_MoveBefore, sibling)

    def MoveAfter(self, sibling: "XmlNode") -> bool:
        return self._Move(self.owner._lib.XmlNode_MoveAfter, sibling)

    def Delete(self) -> bool:
        self.owner._lib.XmlNode_Delete(self.node)
        error = self.owner._lib.XmlNode_Error()
        self.owner.err = Error(error) if error else None
        if self.owner.err:
            return False

        self.node = c_void_p()
        return True


if __name__ == "__main__":
    dom = XmlCls("<root><child value='3.14'>text</child></root>")

    pi = dom.XPath("//child/@value", float)
    print(pi)
    txt = dom.XPath("//child", str)
    print(txt)
    chile = dom.XPath("//child")
    val_str = chile.XPath("@value", str)
    print(val_str)
    print(chile.XML())

    bad = XmlCls("<PonziCoin>")
    assert bad.err is not None
    assert bad.err.level == Error.lvl.ERR

    reported = Error(bad.err)
    bad.err = None
    assert bad.err is None
    reported.publish()

    manual = Error(Error.lvl.WARN, "manual warning", "constructor test")
    assert manual.level == Error.lvl.WARN
    assert manual.msg == "manual warning"
    assert manual.data == "constructor test"

    good = XmlCls("<PonziCoin/>")
    assert good.err is None