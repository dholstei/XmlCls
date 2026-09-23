from ctypes import *
from typing import ClassVar
from dataclasses import dataclass
from enum import IntEnum
from pathlib import Path


class lvl(IntEnum):
    NOERR = 0
    INFO  = 1
    WARN  = 2
    ERR   = 3


class _CError(Structure):
    """Raw ctypes representation of the C ABI structure."""
    _fields_ = [
        ("level", c_int),
        ("msg", c_char_p),
        ("data", c_char_p),
    ]


_CErrorPtr = POINTER(_CError)


@dataclass(frozen=True)
class CError:
    """Python-owned copy of an XmlCls CError."""
    level: lvl
    msg: str
    data: str = ""


@dataclass(frozen=True)
class RestorePoint:
    jid: str
    note: str = ""


class XmlCls:
    """Thin Python wrapper around the XmlCls C ABI."""

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
        lib = cls._lib = CDLL(str(Path(__file__).resolve().with_name("XmlClsLib.so")))

        lib.FreeCError.argtypes = [_CErrorPtr]
        lib.FreeCError.restype = None

        lib.XmlDoc_Error.argtypes = []
        lib.XmlDoc_Error.restype = _CErrorPtr
        lib.XmlNode_Error.argtypes = []
        lib.XmlNode_Error.restype = _CErrorPtr

        lib.XmlDoc_Attach.argtypes = [c_void_p]
        lib.XmlDoc_Attach.restype = c_void_p
        lib.XmlDoc_FromXML.argtypes = [c_char_p]
        lib.XmlDoc_FromXML.restype = c_void_p
        lib.XmlDoc_FromFile.argtypes = [c_char_p]
        lib.XmlDoc_FromFile.restype = c_void_p
        lib.XmlDoc_Free.argtypes = [c_void_p]
        lib.XmlDoc_Free.restype = None
        lib.XmlDoc_Detach.argtypes = [c_void_p]
        lib.XmlDoc_Detach.restype = None

        lib.XmlDoc_Save.argtypes = [c_void_p, c_char_p]
        lib.XmlDoc_Save.restype = None
        lib.XmlDoc_OpenJournal.argtypes = [c_void_p, c_char_p]
        lib.XmlDoc_OpenJournal.restype = None
        lib.XmlDoc_CreateJournal.argtypes = [c_void_p, c_char_p, c_char_p]
        lib.XmlDoc_CreateJournal.restype = None
        lib.XmlDoc_HasJournal.argtypes = [c_void_p]
        lib.XmlDoc_HasJournal.restype = c_int
        lib.XmlDoc_Journal.argtypes = [c_void_p]
        lib.XmlDoc_Journal.restype = c_void_p

        lib.XmlDoc_XPathString.argtypes = [c_void_p, c_void_p, c_char_p]
        lib.XmlDoc_XPathString.restype = c_char_p
        lib.XmlDoc_XPathDouble.argtypes = [c_void_p, c_void_p, c_char_p]
        lib.XmlDoc_XPathDouble.restype = c_double
        lib.XmlDoc_XPathInt.argtypes = [c_void_p, c_void_p, c_char_p]
        lib.XmlDoc_XPathInt.restype = c_int
        lib.XmlDoc_XPathBool.argtypes = [c_void_p, c_void_p, c_char_p]
        lib.XmlDoc_XPathBool.restype = c_int
        lib.XmlDoc_XPathNode.argtypes = [c_void_p, c_void_p, c_char_p]
        lib.XmlDoc_XPathNode.restype = c_void_p
        lib.XmlDoc_XPathNodes.argtypes = [
            c_void_p, c_void_p, c_char_p, POINTER(c_void_p), c_size_t
        ]
        lib.XmlDoc_XPathNodes.restype = c_size_t

        lib.XmlNode_GetPath.argtypes = [c_void_p]
        lib.XmlNode_GetPath.restype = c_char_p

        lib.XmlNode_XML.argtypes = [c_void_p]
        lib.XmlNode_XML.restype = c_char_p
        lib.XmlNode_Parse.argtypes = [c_void_p, c_char_p]
        lib.XmlNode_Parse.restype = c_void_p
        lib.XmlNode_AddChild.argtypes = [c_void_p, c_char_p]
        lib.XmlNode_AddChild.restype = c_void_p
        lib.XmlNode_AddBefore.argtypes = [c_void_p, c_char_p]
        lib.XmlNode_AddBefore.restype = c_void_p
        lib.XmlNode_AddAfter.argtypes = [c_void_p, c_char_p]
        lib.XmlNode_AddAfter.restype = c_void_p
        lib.XmlNode_MoveChild.argtypes = [c_void_p, c_void_p]
        lib.XmlNode_MoveChild.restype = None
        lib.XmlNode_MoveBefore.argtypes = [c_void_p, c_void_p]
        lib.XmlNode_MoveBefore.restype = None
        lib.XmlNode_MoveAfter.argtypes = [c_void_p, c_void_p]
        lib.XmlNode_MoveAfter.restype = None
        lib.XmlNode_Delete.argtypes = [c_void_p]
        lib.XmlNode_Delete.restype = None

        lib.XmlJrnl_Undo.argtypes = [c_void_p]
        lib.XmlJrnl_Undo.restype = None
        lib.XmlJrnl_Redo.argtypes = [c_void_p]
        lib.XmlJrnl_Redo.restype = None
        lib.XmlJrnl_MarkRelease.argtypes = [c_void_p, c_char_p]
        lib.XmlJrnl_MarkRelease.restype = None
        lib.XmlJrnl_StampState.argtypes = [c_void_p, c_char_p, c_char_p]
        lib.XmlJrnl_StampState.restype = c_char_p
        lib.XmlJrnl_Restore.argtypes = [c_void_p, c_char_p]
        lib.XmlJrnl_Restore.restype = None

    @staticmethod
    def _decode(value):
        return value.decode("utf-8", errors="replace") if value else ""

    @classmethod
    def _consume_error(cls, fn):
        p = fn()
        if not p:
            return None
        try:
            e = p.contents
            try:
                level = lvl(e.level)
            except ValueError:
                level = lvl.ERR
            return CError(level, cls._decode(e.msg), cls._decode(e.data))
        finally:
            cls._lib.FreeCError(p)

    def _take_error(self):
        self.err = self._consume_error(self._lib.XmlDoc_Error)
        return self.err

    def _attach(self, doc):
        self.doc = c_void_p(self._lib.XmlDoc_Attach(doc))
        self._take_error()

    def _from_xml(self, XML):
        self.doc = c_void_p(self._lib.XmlDoc_FromXML(XML.encode("utf-8")))
        self._take_error()

    def _from_file(self, filename):
        self.doc = c_void_p(self._lib.XmlDoc_FromFile(filename))
        self._take_error()

    def close(self):
        if self.doc and self.doc.value:
            self._lib.XmlDoc_Free(self.doc)
            self.doc = c_void_p()

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass

    @property
    def JRNL(self):
        if not self.doc or not self.doc.value:
            return None
        p = self._lib.XmlDoc_Journal(self.doc)
        self._take_error()
        return XmlJrnl(self, c_void_p(p)) if p else None

    def Save(self, filename):
        self._lib.XmlDoc_Save(self.doc, filename.encode("utf-8"))
        self._take_error()

    def OpenJournal(self, filename):
        self._lib.XmlDoc_OpenJournal(self.doc, filename.encode("utf-8"))
        self._take_error()

    def CreateJournal(self, filename, XML=""):
        self._lib.XmlDoc_CreateJournal(
            self.doc, filename.encode("utf-8"), XML.encode("utf-8"))
        self._take_error()

    def HasJournal(self):
        ans = bool(self._lib.XmlDoc_HasJournal(self.doc))
        self._take_error()
        return ans

    def _XPath(self, doc, query, type=None, node=None):
        if type is None:
            type = XmlNode

        context = node.node if isinstance(node, XmlNode) else node
        context = context if context else c_void_p()
        q = query.encode("utf-8")

        if type is XmlNode:
            ans = self._lib.XmlDoc_XPathNode(doc, context, q)
            self._take_error()
            return XmlNode(self, c_void_p(ans)) if ans else XmlNode(self, c_void_p())

        if type == list[XmlNode]:
            count = self._lib.XmlDoc_XPathNodes(doc, context, q, None, 0)
            self._take_error()
            if self.err or not count:
                return []

            nodes = (c_void_p * count)()
            count = self._lib.XmlDoc_XPathNodes(doc, context, q, nodes, count)
            self._take_error()
            return [XmlNode(self, nodes[i]) for i in range(count)]

        if type is str:
            ans = self._lib.XmlDoc_XPathString(doc, context, q)
            self._take_error()
            return self._decode(ans)

        if type is float:
            ans = self._lib.XmlDoc_XPathDouble(doc, context, q)
            self._take_error()
            return ans

        if type is int:
            ans = self._lib.XmlDoc_XPathInt(doc, context, q)
            self._take_error()
            return ans

        if type is bool:
            ans = self._lib.XmlDoc_XPathBool(doc, context, q)
            self._take_error()
            return bool(ans)

        raise TypeError(f"Unsupported XPath return type: {type}")

    def XPath(self, query, type=None, node=None):
        return self._XPath(self.doc, query, type, node)

    def RestorePoints(self):
        jrnl = self.JRNL
        if not jrnl:
            return []

        states = jrnl.XPath("//State[@Type='RestorePoint']", list[XmlNode])
        if jrnl.err:
            self.err = jrnl.err
            return []

        points = []
        for state in states:
            jid = state.XPath("@JID", str)
            if state.err:
                self.err = state.err
                return []

            note = state.XPath("@Note", str)
            if state.err:
                self.err = state.err
                return []

            points.append(RestorePoint(jid, note))

        return points

    def Restore(self, jid):
        jrnl = self.JRNL
        if not jrnl:
            return

        jrnl.Restore(jid)
        self.err = jrnl.err



class XmlNode:
    def __init__(self, owner, node):
        self.owner = owner
        self.node = c_void_p(node.value if isinstance(node, c_void_p) else node)
        self.err = None

    def _take_error(self):
        self.err = self.owner._consume_error(self.owner._lib.XmlNode_Error)
        return self.err

    def XPath(self, query, type=None):
        ans = self.owner.XPath(query, XmlNode if type is None else type, node=self)

        # XPath is implemented by XmlDoc::XPath<T>(), so mirror the C++ node
        # wrapper's transfer of owner.err to this XmlNode.
        self.err = self.owner.err
        self.owner.err = None
        return ans

    def GetPath(self):
        ans = self.owner._lib.XmlNode_GetPath(self.node)
        self._take_error()
        return self.owner._decode(ans)

    def XML(self):
        ans = self.owner._lib.XmlNode_XML(self.node)
        self._take_error()
        return self.owner._decode(ans)

    def parse(self, XML):
        ans = self.owner._lib.XmlNode_Parse(self.node, XML.encode("utf-8"))
        self._take_error()
        if ans:
            self.node = c_void_p(ans)

    def _Add(self, fn, XML):
        ans = XmlNode(self.owner, c_void_p(fn(self.node, XML.encode("utf-8"))))
        ans.err = self.owner._consume_error(self.owner._lib.XmlNode_Error)
        return ans

    def AddChild(self, XML):
        return self._Add(self.owner._lib.XmlNode_AddChild, XML)

    def AddBefore(self, XML):
        return self._Add(self.owner._lib.XmlNode_AddBefore, XML)

    def AddAfter(self, XML):
        return self._Add(self.owner._lib.XmlNode_AddAfter, XML)

    def _Move(self, fn, destination):
        if not isinstance(destination, XmlNode):
            raise TypeError("destination must be an XmlNode")
        fn(self.node, destination.node)
        self._take_error()

    def MoveChild(self, parent):
        self._Move(self.owner._lib.XmlNode_MoveChild, parent)

    def MoveBefore(self, sibling):
        self._Move(self.owner._lib.XmlNode_MoveBefore, sibling)

    def MoveAfter(self, sibling):
        self._Move(self.owner._lib.XmlNode_MoveAfter, sibling)

    def Delete(self):
        self.owner._lib.XmlNode_Delete(self.node)
        self._take_error()
        if not self.err:
            self.node = c_void_p()


class XmlJrnl:
    def __init__(self, owner, journal):
        self.owner = owner
        self.journal = c_void_p(
            journal.value if isinstance(journal, c_void_p) else journal)
        self.err = None

    def _take_error(self):
        # XmlJrnl derives from XmlDoc and the C adapter deliberately uses the
        # document error channel for journal operations.
        self.err = self.owner._consume_error(self.owner._lib.XmlDoc_Error)
        return self.err

    def XPath(self, query, type=None):
        ans = self.owner._XPath(self.journal, query, type)
        self.err = self.owner.err
        self.owner.err = None
        return ans

    def Undo(self):
        self.owner._lib.XmlJrnl_Undo(self.journal)
        self._take_error()

    def Redo(self):
        self.owner._lib.XmlJrnl_Redo(self.journal)
        self._take_error()

    def MarkRelease(self, note=""):
        self.owner._lib.XmlJrnl_MarkRelease(self.journal, note.encode("utf-8"))
        self._take_error()

    def StampState(self, type, note=""):
        ans = self.owner._lib.XmlJrnl_StampState(
            self.journal, type.encode("utf-8"), note.encode("utf-8"))
        self._take_error()
        return self.owner._decode(ans)

    def Restore(self, jid):
        self.owner._lib.XmlJrnl_Restore(self.journal, jid.encode("ascii"))
        self._take_error()