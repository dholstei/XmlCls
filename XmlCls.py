from ctypes import *
from typing import ClassVar
from dataclasses import dataclass
from enum import IntEnum
from pathlib import Path
from xml.etree import ElementTree


class lvl(IntEnum):
    NOERR = 0
    INFO  = 1
    WARN  = 2
    ERR   = 3


class CError(Structure):
    _fields_ = [
        ("level", c_int),
        ("msg", c_char_p),
        ("data", c_char_p),
    ]


CErrorPtr = POINTER(CError)


@dataclass
class Error:
    level: lvl = lvl.NOERR
    msg: str = ""
    data: str = ""

    def __bool__(self):
        return self.level >= lvl.ERR


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

    def _take_error(self) -> Error:
        p = self._lib.XmlDoc_Error()

        if not p:
            self.err = None
            return self.err

        try:
            e = p.contents
            try:
                level = lvl(e.level)
            except ValueError:
                level = lvl.ERR

            self.err = Error(level, self._decode(e.msg), self._decode(e.data))
            return self.err
        finally:
            self._lib.FreeCError(p)

    def _take_node_error(self) -> Error:
        p = self._lib.XmlNode_Error()

        if not p:
            self.err = None
            return self.err

        try:
            e = p.contents
            try:
                level = lvl(e.level)
            except ValueError:
                level = lvl.ERR

            self.err = Error(level, self._decode(e.msg), self._decode(e.data))
            return self.err
        finally:
            self._lib.FreeCError(p)

    def _failed(self) -> bool:
        self._take_error()
        return bool(self.err)

    def _attach(self, doc: c_void_p):
        if not doc or not doc.value:
            raise ValueError("Invalid NULL xmlDocPtr")

        owner = self._lib.XmlDoc_Attach(doc)
        if not owner:
            raise RuntimeError("Unable to attach XmlDoc")

        self.doc = c_void_p(owner)
        self._take_error()

    def _from_xml(self, XML: str):
        owner = self._lib.XmlDoc_FromXML(XML.encode("utf-8"))
        if not owner:
            raise RuntimeError("Unable to construct XmlDoc from XML")

        self.doc = c_void_p(owner)
        self._take_error()

    def _from_file(self, filename: c_char_p):
        owner = self._lib.XmlDoc_FromFile(filename)
        if not owner:
            name = self._decode(filename.value)
            raise RuntimeError(f"Unable to open XML file: {name}")

        self.doc = c_void_p(owner)
        self._take_error()

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
        self._take_error()
        return not self.err

    def OpenJournal(self, filename: str) -> bool:
        if self.doc:
            self._lib.XmlDoc_OpenJournal(self.doc, filename.encode("utf-8"))
        self._take_error()
        return not self.err

    def CreateJournal(self, filename: str, XML: str = "") -> bool:
        if self.doc:
            self._lib.XmlDoc_CreateJournal(
                self.doc, filename.encode("utf-8"), XML.encode("utf-8"))
        self._take_error()
        return not self.err

    def Undo(self) -> bool:
        journal = self._lib.XmlDoc_Journal(self.doc) if self.doc else None
        if journal:
            self._lib.XmlJrnl_Undo(journal)
        self._take_error()
        return not self.err

    def Redo(self) -> bool:
        journal = self._lib.XmlDoc_Journal(self.doc) if self.doc else None
        if journal:
            self._lib.XmlJrnl_Redo(journal)
        self._take_error()
        return not self.err

    def HasJournal(self) -> bool:
        return bool(self.doc and self._lib.XmlDoc_HasJournal(self.doc))

    def MarkRelease(self, note: str = "") -> bool:
        journal = self._lib.XmlDoc_Journal(self.doc) if self.doc else None
        if journal:
            self._lib.XmlJrnl_MarkRelease(journal, note.encode("utf-8"))
        self._take_error()
        return not self.err

    def MarkRestorePoint(self, note: str = "") -> str:
        journal = self._lib.XmlDoc_Journal(self.doc) if self.doc else None
        jid = self._lib.XmlJrnl_StampState(
            journal, b"RestorePoint", note.encode("utf-8")
        ) if journal else None
        self._take_error()
        return self._decode(jid) if not self.err else ""

    def RestorePoints(self) -> list[RestorePoint]:
        journal = self._lib.XmlDoc_Journal(self.doc) if self.doc else None
        XML = self._lib.XmlJrnl_RestorePoints(journal) if journal else None
        self._take_error()
        if self.err or not XML:
            return []

        root = ElementTree.fromstring(self._decode(XML))
        return [RestorePoint(state.get("JID", ""), state.get("Note", "")) for state in root]

    def Restore(self, jid: str) -> bool:
        journal = self._lib.XmlDoc_Journal(self.doc) if self.doc else None
        if journal:
            self._lib.XmlJrnl_Restore(journal, jid.encode("ascii"))
        self._take_error()
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
            self._take_error()
            return XmlNode(self, c_void_p(value)) if value and not self.err else None

        if type == list[XmlNode]:
            count = self._lib.XmlDoc_XPathNodes(self.doc, context, q, None, 0)
            self._take_error()
            if self.err or not count:
                return []

            nodes = (c_void_p * count)()
            actual = self._lib.XmlDoc_XPathNodes(self.doc, context, q, nodes, count)
            self._take_error()
            if self.err:
                return []

            return [XmlNode(self, c_void_p(nodes[i])) for i in range(actual)]

        if type is str:
            value = self._lib.XmlDoc_XPathString(self.doc, context, q)
            self._take_error()
            return self._decode(value) if not self.err else ""

        if type is float:
            value = self._lib.XmlDoc_XPathDouble(self.doc, context, q)
            self._take_error()
            return value if not self.err else 0.0

        if type is int:
            value = self._lib.XmlDoc_XPathInt(self.doc, context, q)
            self._take_error()
            return value if not self.err else 0

        if type is bool:
            value = self._lib.XmlDoc_XPathBool(self.doc, context, q)
            self._take_error()
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
        self.owner._take_node_error()
        return self.owner._decode(value) if not self.owner.err else ""

    def XML(self) -> str:
        value = self.owner._lib.XmlNode_XML(self.node)
        self.owner._take_node_error()
        return self.owner._decode(value) if not self.owner.err else ""

    def parse(self, XML: str) -> bool:
        node = self.owner._lib.XmlNode_Parse(self.node, XML.encode("utf-8"))
        self.owner._take_node_error()
        if not node or self.owner.err:
            return False

        self.node = c_void_p(node)
        return True

    def _Add(self, function, XML: str):
        node = function(self.node, XML.encode("utf-8"))
        self.owner._take_node_error()
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
        self.owner._take_node_error()
        return not self.owner.err

    def MoveChild(self, parent: "XmlNode") -> bool:
        return self._Move(self.owner._lib.XmlNode_MoveChild, parent)

    def MoveBefore(self, sibling: "XmlNode") -> bool:
        return self._Move(self.owner._lib.XmlNode_MoveBefore, sibling)

    def MoveAfter(self, sibling: "XmlNode") -> bool:
        return self._Move(self.owner._lib.XmlNode_MoveAfter, sibling)

    def Delete(self) -> bool:
        self.owner._lib.XmlNode_Delete(self.node)
        self.owner._take_node_error()
        if self.owner.err:
            return False

        self.node = c_void_p()
        return True