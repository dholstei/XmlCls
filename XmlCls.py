from ctypes import *
from typing import ClassVar
from dataclasses import dataclass
from enum import IntEnum
from pathlib import Path
from xml.etree import ElementTree


class lvl(IntEnum):
    NOERR   = 0
    DEBUG   = 1
    INFO    = 2
    WARNING = 3
    ERR     = 4

class XPathType(IntEnum):
    NODESET = 1
    BOOLEAN = 2
    NUMBER  = 3
    STRING  = 4

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


class _xmlError(Structure):
    _fields_ = [
        ("domain", c_int),
        ("code", c_int),
        ("message", c_char_p),
        ("level", c_int),
        ("file", c_char_p),
        ("line", c_int),
        ("str1", c_char_p),
        ("str2", c_char_p),
        ("str3", c_char_p),
        ("int1", c_int),
        ("int2", c_int),
        ("ctxt", c_void_p),
        ("node", c_void_p),
    ]


class _xmlNodeSet(Structure):
    _fields_ = [
        ("nodeNr", c_int),
        ("nodeMax", c_int),
        ("nodeTab", POINTER(c_void_p)),
    ]


class _xmlXPathObject(Structure):
    _fields_ = [
        ("type", c_int),
        ("nodesetval", POINTER(_xmlNodeSet)),
        ("boolval", c_int),
        ("floatval", c_double),
        ("stringval", c_char_p),
    ]


class XmlCls:
    _lib: ClassVar[CDLL | None] = None
    _xlib: ClassVar[CDLL | None] = None

    def __init__(self, source):
        if XmlCls._lib is None:
            XmlCls._load_libxml2()

        self.doc = c_void_p()
        self.ctxt = c_void_p()
        self.err = Error()
        self._owns_doc = False
        self._cpp_doc = c_void_p()

        if isinstance(source, c_void_p):
            self._from_doc(source)

        elif isinstance(source, str):
            self._from_xml(source)

        elif isinstance(source, c_char_p):
            self._from_file(source)

        else:
            raise TypeError(
                "XmlCls source must be c_void_p, str XML, or c_char_p filename"
            )

        if self.doc:
            self._attach_doc()
        
    @classmethod
    def _load_libxml2(cls):
        cls._lib = CDLL("libxml2.so")

        # Basic function prototypes for libxml2 functions used in this class
        cls._lib.xmlReadFile.argtypes = [c_char_p, c_char_p, c_int]
        cls._lib.xmlReadFile.restype = c_void_p

        cls._lib.xmlReadMemory.argtypes = [
            c_char_p, c_int, c_char_p, c_char_p, c_int
        ]
        cls._lib.xmlReadMemory.restype = c_void_p

        cls._lib.xmlFreeDoc.argtypes = [c_void_p]
        cls._lib.xmlFreeDoc.restype = None

        xmlFreeFunc = CFUNCTYPE(None, c_void_p)
        cls._xmlFree = xmlFreeFunc.in_dll(cls._lib, "xmlFree")

        # Basic error handling prototypes for libxml2 functions
        cls._lib.xmlGetLastError.argtypes = []
        cls._lib.xmlGetLastError.restype = POINTER(_xmlError)

        cls._lib.xmlResetLastError.argtypes = []
        cls._lib.xmlResetLastError.restype = None

        # Basic XPath handling prototypes for libxml2 functions
        cls._lib.xmlXPathNewContext.argtypes = [c_void_p]
        cls._lib.xmlXPathNewContext.restype = c_void_p

        cls._lib.xmlXPathFreeContext.argtypes = [c_void_p]
        cls._lib.xmlXPathFreeContext.restype = None

        cls._lib.xmlXPathEvalExpression.argtypes = [c_char_p, c_void_p]
        cls._lib.xmlXPathEvalExpression.restype = POINTER(_xmlXPathObject)

        cls._lib.xmlXPathFreeObject.argtypes = [POINTER(_xmlXPathObject)]
        cls._lib.xmlXPathFreeObject.restype = None

        cls._lib.xmlXPathNodeEval.argtypes = [c_void_p, c_char_p, c_void_p]
        cls._lib.xmlXPathNodeEval.restype = POINTER(_xmlXPathObject)

        cls._lib.xmlNodeGetContent.argtypes = [c_void_p]
        cls._lib.xmlNodeGetContent.restype = c_void_p

        cls._xlib = CDLL(str(Path(__file__).resolve().with_name("XmlClsLib.so")))

        cls._xlib.XmlNode_XML.argtypes = [c_void_p, c_char_p, c_size_t]
        cls._xlib.XmlNode_XML.restype = c_size_t

        cls._xlib.XmlDoc_Attach.argtypes = [c_void_p]
        cls._xlib.XmlDoc_Attach.restype = c_void_p
        cls._xlib.XmlDoc_Detach.argtypes = [c_void_p]
        cls._xlib.XmlDoc_Detach.restype = None
        cls._xlib.XmlDoc_Save.argtypes = [c_void_p, c_char_p]
        cls._xlib.XmlDoc_Save.restype = c_int
        cls._xlib.XmlDoc_OpenJournal.argtypes = [c_void_p, c_char_p]
        cls._xlib.XmlDoc_OpenJournal.restype = c_int
        cls._xlib.XmlDoc_CreateJournal.argtypes = [c_void_p, c_char_p, c_char_p]
        cls._xlib.XmlDoc_CreateJournal.restype = c_int
        cls._xlib.XmlDoc_Undo.argtypes = [c_void_p]
        cls._xlib.XmlDoc_Undo.restype = c_int
        cls._xlib.XmlDoc_Redo.argtypes = [c_void_p]
        cls._xlib.XmlDoc_Redo.restype = c_int
        cls._xlib.XmlDoc_HasJournal.argtypes = [c_void_p]
        cls._xlib.XmlDoc_HasJournal.restype = c_int
        cls._xlib.XmlDoc_MarkRelease.argtypes = [c_void_p, c_char_p]
        cls._xlib.XmlDoc_MarkRelease.restype = c_int
        cls._xlib.XmlDoc_MarkRestorePoint.argtypes = [c_void_p, c_char_p, c_char_p, c_size_t]
        cls._xlib.XmlDoc_MarkRestorePoint.restype = c_int
        cls._xlib.XmlDoc_RestorePoints.argtypes = [c_void_p, c_char_p, c_size_t]
        cls._xlib.XmlDoc_RestorePoints.restype = c_size_t
        cls._xlib.XmlDoc_Restore.argtypes = [c_void_p, c_char_p]
        cls._xlib.XmlDoc_Restore.restype = c_int

        cls._xlib.XmlNode_Parse.argtypes = [c_void_p, c_char_p]
        cls._xlib.XmlNode_Parse.restype = c_void_p
        cls._xlib.XmlNode_AddChild.argtypes = [c_void_p, c_char_p]
        cls._xlib.XmlNode_AddChild.restype = c_void_p
        cls._xlib.XmlNode_AddBefore.argtypes = [c_void_p, c_char_p]
        cls._xlib.XmlNode_AddBefore.restype = c_void_p
        cls._xlib.XmlNode_AddAfter.argtypes = [c_void_p, c_char_p]
        cls._xlib.XmlNode_AddAfter.restype = c_void_p
        cls._xlib.XmlNode_MoveChild.argtypes = [c_void_p, c_void_p]
        cls._xlib.XmlNode_MoveChild.restype = c_int
        cls._xlib.XmlNode_MoveBefore.argtypes = [c_void_p, c_void_p]
        cls._xlib.XmlNode_MoveBefore.restype = c_int
        cls._xlib.XmlNode_MoveAfter.argtypes = [c_void_p, c_void_p]
        cls._xlib.XmlNode_MoveAfter.restype = c_int
        cls._xlib.XmlNode_Delete.argtypes = [c_void_p]
        cls._xlib.XmlNode_Delete.restype = c_int

        cls._xlib.XmlCls_LastError.argtypes = []
        cls._xlib.XmlCls_LastError.restype = c_char_p

    def _attach_doc(self):
        owner = self._xlib.XmlDoc_Attach(self.doc)
        if not owner:
            self.CAPI_err("Unable to attach the C++ XmlDoc wrapper")
            return
        self._cpp_doc = c_void_p(owner)
        if self._xlib.XmlCls_LastError():
            self.CAPI_err("Unable to open declared XML journal")

    def CAPI_err(self, data: str = "") -> Error:
        value = self._xlib.XmlCls_LastError()
        msg = value.decode("utf-8", errors="replace") if value else "Unknown XmlCls error"
        self.err = Error(lvl.ERR, msg, data)
        return self.err

    def close(self):
        if self.ctxt:
            self._lib.xmlXPathFreeContext(self.ctxt)
            self.ctxt = c_void_p()
        if self._cpp_doc:
            self._xlib.XmlDoc_Detach(self._cpp_doc)
            self._cpp_doc = c_void_p()
        if self._owns_doc and self.doc:
            self._lib.xmlFreeDoc(self.doc)
        self.doc = c_void_p()
        self._owns_doc = False

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass

    def Save(self, filename: str) -> bool:
        if not self._cpp_doc or not self._xlib.XmlDoc_Save(self._cpp_doc, filename.encode("utf-8")):
            self.CAPI_err(filename)
            return False
        self.err = Error()
        return True

    def OpenJournal(self, filename: str) -> bool:
        if not self._cpp_doc or not self._xlib.XmlDoc_OpenJournal(self._cpp_doc, filename.encode("utf-8")):
            self.CAPI_err(filename)
            return False
        self.err = Error()
        return True

    def CreateJournal(self, filename: str, XML: str = "") -> bool:
        if not self._cpp_doc or not self._xlib.XmlDoc_CreateJournal(
            self._cpp_doc, filename.encode("utf-8"), XML.encode("utf-8")
        ):
            self.CAPI_err(filename)
            return False
        self.err = Error()
        return True

    def Undo(self) -> bool:
        if not self._cpp_doc or not self._xlib.XmlDoc_Undo(self._cpp_doc):
            self.CAPI_err("Undo")
            return False
        self.err = Error()
        return True

    def Redo(self) -> bool:
        if not self._cpp_doc or not self._xlib.XmlDoc_Redo(self._cpp_doc):
            self.CAPI_err("Redo")
            return False
        self.err = Error()
        return True

    def HasJournal(self) -> bool:
        return bool(self._cpp_doc and self._xlib.XmlDoc_HasJournal(self._cpp_doc))

    def MarkRelease(self, note: str = "") -> bool:
        if not self._cpp_doc or not self._xlib.XmlDoc_MarkRelease(self._cpp_doc, note.encode("utf-8")):
            self.CAPI_err(note)
            return False
        self.err = Error()
        return True

    def MarkRestorePoint(self, note: str = "") -> str:
        jid = create_string_buffer(17)
        if not self._cpp_doc or not self._xlib.XmlDoc_MarkRestorePoint(
            self._cpp_doc, note.encode("utf-8"), jid, len(jid)
        ):
            self.CAPI_err(note)
            return ""
        self.err = Error()
        return jid.value.decode("ascii")

    def RestorePoints(self) -> list[RestorePoint]:
        size = self._xlib.XmlDoc_RestorePoints(self._cpp_doc, None, 0) if self._cpp_doc else 0
        if not size:
            self.CAPI_err("RestorePoints")
            return []
        buf = create_string_buffer(size)
        if self._xlib.XmlDoc_RestorePoints(self._cpp_doc, buf, size) != size:
            self.CAPI_err("RestorePoints")
            return []
        root = ElementTree.fromstring(buf.value.decode("utf-8"))
        self.err = Error()
        return [RestorePoint(state.get("JID", ""), state.get("Note", "")) for state in root]

    def Restore(self, jid: str) -> bool:
        if not self._cpp_doc or not self._xlib.XmlDoc_Restore(self._cpp_doc, jid.encode("ascii")):
            self.CAPI_err(jid)
            return False
        self.err = Error()
        return True

    def API_err(self, data: str = "") -> Error:
        p = self._lib.xmlGetLastError()

        if not p:
            return Error(lvl.ERR, "Unknown libxml2 error", data)

        e = p.contents
        msg = e.message.decode("utf-8", errors="replace").strip() if e.message else "Unknown libxml2 error"
        self.err = Error(lvl.ERR, msg, data)
        return self.err

    def _from_doc(self, doc: c_void_p):
        if not doc or not doc.value:
            raise ValueError("Invalid NULL xmlDocPtr")

        self.doc = doc
        self._owns_doc = False

    def _from_xml(self, XML: str):
        data = XML.encode("utf-8")

        doc = self._lib.xmlReadMemory( data, len(data), b"memory.xml", None, 0 )

        if not doc:
            self.API_err("Unable to parse XML string")
            return

        self.doc = c_void_p(doc)
        self._owns_doc = True

    def _from_file(self, filename: c_char_p):
        doc = self._lib.xmlReadFile( filename, None, 0 )

        if not doc:
            name = filename.value.decode() if filename.value else ""
            self.err = self.API_err(name)
            return

        self.doc = c_void_p(doc)
        self._owns_doc = True

    def _XPathContext(self) -> c_void_p:
        if not self.ctxt:
            self.ctxt = c_void_p(self._lib.xmlXPathNewContext(self.doc))

            if not self.ctxt:
                self.API_err("Unable to create XPath context")

        return self.ctxt

    def XPath(self, query: str, type=None, node=None):
        ctxt = self._XPathContext()

        if type is None:
            type = XmlNode

        if node is None:
            result = self._lib.xmlXPathEvalExpression(query.encode("utf-8"), ctxt)
        else:
            node_ptr = node.node if isinstance(node, XmlNode) else node
            result = self._lib.xmlXPathNodeEval(node_ptr, query.encode("utf-8"), ctxt)

        if not result:
            self.API_err(f'XPath evaluation failed: "{query}"')
            return self._XPathDefault(type)

        try:
            obj = result.contents

            if type is XmlNode:
                if obj.type != XPathType.NODESET:
                    self.err = Error(lvl.ERR, "XPath result is not a node set", query)
                    return None

                nodes = obj.nodesetval

                if not nodes or nodes.contents.nodeNr == 0:
                    return None

                if nodes.contents.nodeNr > 1:
                    self.err = Error(lvl.ERR, "XPath result is ambiguous, expected one node", query)
                    return None

                return XmlNode(self, c_void_p(nodes.contents.nodeTab[0]))

            if type == list[XmlNode]:
                if obj.type != XPathType.NODESET:
                    self.err = Error(lvl.ERR, "XPath result is not a node set", query)
                    return []

                nodes = obj.nodesetval

                if not nodes or nodes.contents.nodeNr == 0:
                    return []

                return [
                    XmlNode(self, c_void_p(nodes.contents.nodeTab[i]))
                    for i in range(nodes.contents.nodeNr)
                ]

            if type is str:
                if obj.type == XPathType.STRING:
                    return obj.stringval.decode("utf-8") if obj.stringval else ""

                if obj.type == XPathType.NODESET:
                    nodes = obj.nodesetval

                    if not nodes or nodes.contents.nodeNr != 1:
                        self.err = Error(lvl.ERR, "XPath result is not a single node", query)
                        return ""

                    return self._NodeString(nodes.contents.nodeTab[0])

                self.err = Error(lvl.ERR, "XPath result cannot be converted to str", query)
                return ""

            if type is float:
                if obj.type == XPathType.NUMBER:
                    return float(obj.floatval)

                if obj.type == XPathType.NODESET:
                    nodes = obj.nodesetval

                    if not nodes or nodes.contents.nodeNr != 1:
                        self.err = Error(lvl.ERR, "XPath result is not a single node", query)
                        return 0.0

                    try:
                        return float(self._NodeString(nodes.contents.nodeTab[0]))
                    except ValueError:
                        self.err = Error(lvl.ERR, "XPath node value cannot be converted to float", query)
                        return 0.0

                self.err = Error(lvl.ERR, "XPath result cannot be converted to float", query)
                return 0.0

            if type is int:
                if obj.type == XPathType.NUMBER:
                    return int(obj.floatval)

                if obj.type == XPathType.NODESET:
                    nodes = obj.nodesetval

                    if not nodes or nodes.contents.nodeNr != 1:
                        self.err = Error(lvl.ERR, "XPath result is not a single node", query)
                        return 0

                    try:
                        return int(float(self._NodeString(nodes.contents.nodeTab[0])))
                    except ValueError:
                        self.err = Error(lvl.ERR, "XPath node value cannot be converted to int", query)
                        return 0

                self.err = Error(lvl.ERR, "XPath result cannot be converted to int", query)
                return 0

            if type is bool:
                if obj.type == XPathType.BOOLEAN:
                    return bool(obj.boolval)

                if obj.type == XPathType.NODESET:
                    nodes = obj.nodesetval
                    return bool(nodes and nodes.contents.nodeNr > 0)

                self.err = Error(lvl.ERR, "XPath result cannot be converted to bool", query)
                return False

            raise TypeError(f"Unsupported XPath return type: {type}")

        finally:
            self._lib.xmlXPathFreeObject(result)

    def _NodeString(self, node: c_void_p) -> str:
        if not node:
            return ""

        value = self._lib.xmlNodeGetContent(node)
        if not value:
            return ""

        try:
            return cast(value, c_char_p).value.decode("utf-8")
        finally:
            self._xmlFree(value)


class XmlNode:
    def __init__(self, owner: "XmlCls", node: c_void_p):
        self.owner = owner
        self.node: c_void_p = c_void_p(node.value if isinstance(node, c_void_p) else node)

    @property
    def _lib(self):
        return self.owner._lib

    @property
    def err(self):
        return self.owner.err


    def XPath(self, query: str, type=None):
        if type is None:
            type = XmlNode

        return self.owner.XPath(query, type, node=self)
    
    def XML(self) -> str:
        size = self.owner._xlib.XmlNode_XML(self.node, None, 0)
        if not size:
            return ""

        buf = create_string_buffer(size)

        if self.owner._xlib.XmlNode_XML(self.node, buf, size) != size:
            return ""

        return buf.value.decode("utf-8")

    def parse(self, XML: str) -> bool:
        node = self.owner._xlib.XmlNode_Parse(self.node, XML.encode("utf-8"))
        if not node:
            self.owner.CAPI_err(XML[:200])
            return False
        self.node = c_void_p(node)
        self.owner.err = Error()
        return True

    def _Add(self, function, XML: str):
        node = function(self.node, XML.encode("utf-8"))
        if not node:
            self.owner.CAPI_err(XML[:200])
            return None
        self.owner.err = Error()
        return XmlNode(self.owner, c_void_p(node))

    def AddChild(self, XML: str):
        return self._Add(self.owner._xlib.XmlNode_AddChild, XML)

    def AddBefore(self, XML: str):
        return self._Add(self.owner._xlib.XmlNode_AddBefore, XML)

    def AddAfter(self, XML: str):
        return self._Add(self.owner._xlib.XmlNode_AddAfter, XML)

    def _Move(self, function, destination: "XmlNode") -> bool:
        if not isinstance(destination, XmlNode):
            raise TypeError("destination must be an XmlNode")
        if not function(self.node, destination.node):
            self.owner.CAPI_err("Move")
            return False
        self.owner.err = Error()
        return True

    def MoveChild(self, parent: "XmlNode") -> bool:
        return self._Move(self.owner._xlib.XmlNode_MoveChild, parent)

    def MoveBefore(self, sibling: "XmlNode") -> bool:
        return self._Move(self.owner._xlib.XmlNode_MoveBefore, sibling)

    def MoveAfter(self, sibling: "XmlNode") -> bool:
        return self._Move(self.owner._xlib.XmlNode_MoveAfter, sibling)

    def Delete(self) -> bool:
        if not self.owner._xlib.XmlNode_Delete(self.node):
            self.owner.CAPI_err("Delete")
            return False
        self.node = c_void_p()
        self.owner.err = Error()
        return True
