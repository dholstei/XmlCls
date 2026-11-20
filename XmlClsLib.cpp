#include "XmlCls.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

/*
 * The C ABI mirrors XmlCls error ownership:
 *
 *     C++ object.err -> Last*Err -> CError -> caller
 *
 * Each adapter call clears the previous unconsumed error for its object class,
 * invokes exactly one XmlCls operation, then transfers object.err into the
 * appropriate thread-local slot.  The C layer does not invent fallback errors.
 */
thread_local Error LastDocErr;
thread_local Error LastNodeErr;

thread_local std::string LastString;
thread_local std::map<std::string, std::string> LastAttributes;

void ClearLastDocError()
{
    LastDocErr = {};
}

void ClearLastNodeError()
{
    LastNodeErr = {};
}

void TakeDocError(XmlDoc* doc)
{
    if (doc && doc->err) {
        LastDocErr = std::move(doc->err);
        doc->err = {};
    }
}

void TakeNodeError(XmlNode& node)
{
    if (node.err) {
        LastNodeErr = std::move(node.err);
        node.err = {};
    }
}

XmlDoc* CDoc(void* owner)
{
    return static_cast<XmlDoc*>(owner);
}

xmlNodePtr CNode(void* node)
{
    return static_cast<xmlNodePtr>(node);
}

} // namespace


extern "C" {

/* One name/value pair in the C attribute-array interface. */
typedef struct attr {
    const char* name;
    const char* value;
} attr_t;

/*
 * Error conversion -----------------------------------------------------------
 *
 * ConvertToCError() and FreeCError() are supplied by XmlCls.h/XmlCls.cpp.
 * Retrieval consumes the corresponding Last*Err.
 */

CErrorPtr XmlDoc_Error()
{
    if (!LastDocErr) return nullptr;

    CErrorPtr ans = LastDocErr.ToCError();
    LastDocErr = {};
    return ans;
}

CErrorPtr XmlNode_Error()
{
    if (!LastNodeErr) return nullptr;

    CErrorPtr ans = LastNodeErr.ToCError();
    LastNodeErr = {};
    return ans;
}


/*
 * XmlDoc construction/lifetime ----------------------------------------------
 */

void* XmlDoc_FromFile(const char* filename)
{
    ClearLastDocError();

    XmlDoc* doc = new XmlDoc(filename);
    TakeDocError(doc);
    return doc;
}


void* XmlDoc_FromXML(const char* XML)
{
    ClearLastDocError();

    XmlDoc* doc = new XmlDoc(std::string(XML ? XML : ""));
    TakeDocError(doc);
    return doc;
}


/*
 * Attach an XmlDoc wrapper to an existing xmlDocPtr.
 *
 * The caller owns the libxml2 document.  XmlDoc_Detach() therefore clears the
 * canonical _private pointer and prevents XmlDoc::~XmlDoc() from freeing it.
 */
void* XmlDoc_Attach(xmlDocPtr raw)
{
    ClearLastDocError();

    if (!raw)
        return nullptr;

    XmlDoc* doc = new XmlDoc(raw);
    TakeDocError(doc);
    return doc;
}


void XmlDoc_Free(void* owner)
{
    ClearLastDocError();

    XmlDoc* doc = CDoc(owner);
    if (!doc)
        return;

    delete doc;
}


void XmlDoc_Detach(void* owner)
{
    ClearLastDocError();

    XmlDoc* doc = CDoc(owner);
    if (!doc)
        return;

    /*
     * XmlDoc::doc is const in the current class.  Detach is only for wrappers
     * around externally-owned xmlDocPtr objects: clear the canonical owner and
     * deliberately release only the C++ wrapper.
     *
     * If XmlDoc's destructor always xmlFreeDoc(doc), keep the existing
     * non-owning constructor/destructor policy instead of using this function.
     */
    if (doc->doc && doc->doc->_private == doc)
        doc->doc->_private = nullptr;

    /*
     * Do not delete here unless XmlDoc explicitly tracks non-ownership.
     * This function is retained only for the externally-owned-doc constructor.
     */
}


/*
 * XmlDoc methods -------------------------------------------------------------
 */

void XmlDoc_Save(void* owner, const char* filename)
{
    ClearLastDocError();

    XmlDoc* doc = CDoc(owner);
    if (!doc)
        return;

    doc->Save(filename);
    TakeDocError(doc);
}


void XmlDoc_OpenJournal(void* owner, const char* filename)
{
    ClearLastDocError();

    XmlDoc* doc = CDoc(owner);
    if (!doc)
        return;

    doc->OpenJournal(filename);
    TakeDocError(doc);
}


void XmlDoc_CreateJournal(void* owner, const char* filename, const char* XML)
{
    ClearLastDocError();

    XmlDoc* doc = CDoc(owner);
    if (!doc)
        return;

    doc->CreateJournal(filename, XML ? XML : "");
    TakeDocError(doc);
}


int XmlDoc_HasJournal(void* owner)
{
    ClearLastDocError();

    XmlDoc* doc = CDoc(owner);
    return doc && doc->JRNL && !doc->immutable;
}


/*
 * XPath ----------------------------------------------------------------------
 *
 * node == nullptr evaluates relative to the document, otherwise relative to
 * that xmlNodePtr.  This maps directly to the canonical
 *
 *     XmlDoc::XPath<T>(query, node)
 *
 * implementation.
 */

const char* XmlDoc_XPathString(void* owner, xmlNodePtr node, const char* query)
{
    ClearLastDocError();

    XmlDoc* doc = CDoc(owner);
    LastString.clear();

    if (!doc)
        return LastString.c_str();

    LastString = doc->XPath<std::string>(query ? query : "", node);
    TakeDocError(doc);
    return LastString.c_str();
}


double XmlDoc_XPathDouble(void* owner, xmlNodePtr node, const char* query)
{
    ClearLastDocError();

    XmlDoc* doc = CDoc(owner);
    if (!doc)
        return 0.0;

    double ans = doc->XPath<double>(query ? query : "", node);
    TakeDocError(doc);
    return ans;
}


int XmlDoc_XPathInt(void* owner, xmlNodePtr node, const char* query)
{
    ClearLastDocError();

    XmlDoc* doc = CDoc(owner);
    if (!doc)
        return 0;

    int ans = doc->XPath<int>(query ? query : "", node);
    TakeDocError(doc);
    return ans;
}


int XmlDoc_XPathBool(void* owner, xmlNodePtr node, const char* query)
{
    ClearLastDocError();

    XmlDoc* doc = CDoc(owner);
    if (!doc)
        return 0;

    bool ans = doc->XPath<bool>(query ? query : "", node);
    TakeDocError(doc);
    return ans ? 1 : 0;
}


xmlNodePtr XmlDoc_XPathNode(void* owner, xmlNodePtr node, const char* query)
{
    ClearLastDocError();

    XmlDoc* doc = CDoc(owner);
    if (!doc)
        return nullptr;

    XmlNode ans = doc->XPath<XmlNode>(query ? query : "", node);
    TakeDocError(doc);
    return ans.node;
}


/*
 * The vector return is the one place where the C ABI needs a container
 * accommodation.  Call with nodes == nullptr to obtain the element count,
 * then again with a caller-owned xmlNodePtr array.
 */
size_t XmlDoc_XPathNodes(void* owner, xmlNodePtr node, const char* query,
                         xmlNodePtr* nodes, size_t capacity)
{
    ClearLastDocError();

    XmlDoc* doc = CDoc(owner);
    if (!doc)
        return 0;

    auto ans = doc->XPath<std::vector<XmlNode>>(query ? query : "", node);
    TakeDocError(doc);

    if (LastDocErr)
        return 0;

    if (!nodes)
        return ans.size();

    const size_t count = std::min(capacity, ans.size());
    for (size_t i = 0; i < count; ++i)
        nodes[i] = ans[i].node;

    return count;
}


/*
 * XmlNode methods ------------------------------------------------------------
 *
 * XmlNode is deliberately not allocated by the C layer.  The temporary C++
 * wrapper exists only long enough to invoke the canonical XmlCls method.
 */

/**
 * @brief Return the structural XPath for a node.
 * @param node Node whose path is requested.
 * @return Borrowed null-terminated string valid until the next string-returning
 *         C-interface call on this thread.
 */
const char* XmlNode_GetPath(xmlNodePtr node)
{
    ClearLastNodeError();
    LastString.clear();
    if (!node) return LastString.c_str();

    XmlNode n(node);
    LastString = n.GetPath();
    TakeNodeError(n);
    return LastString.c_str();
}

const char* XmlDoc_XML(void* owner, int canonical)
{
    ClearLastDocError();
    LastString.clear();
    XmlDoc* doc = CDoc(owner);
    if (!doc) return LastString.c_str();
    LastString = doc->XML(canonical != 0);
    TakeDocError(doc);
    return LastString.c_str();
}

const char* XmlNode_XML(xmlNodePtr node, int canonical)
{
    ClearLastNodeError();
    LastString.clear();
    if (!node) return LastString.c_str();
    XmlNode n(node);
    LastString = n.XML(canonical != 0);
    TakeNodeError(n);
    return LastString.c_str();
}

xmlNodePtr XmlNode_Parse(xmlNodePtr node, const char* XML)
{
    ClearLastNodeError();

    if (!node)
        return nullptr;

    XmlNode n(node);
    n.parse(XML ? XML : "");
    TakeNodeError(n);
    return n.node;
}


/* Position values are part of the C ABI: 0=Child, 1=Before, 2=After. */
enum XmlInsertPosition { XML_CHILD = 0, XML_BEFORE = 1, XML_AFTER = 2 };

/* Explicit value type: 0 = XML string, 1 = XmlElement. */
enum XmlAddType { XML_STRING = 0, XML_ELEMENT = 1 };
xmlNodePtr XmlNode_Add(xmlNodePtr node, int pos, int type, const char* XML,
                       const char* name, const char* content,
                       const char* const* keys, const char* const* values,
                       size_t count)
{
    ClearLastNodeError();
    if (!node) return nullptr;

    XmlNode n(node);
    XmlNode ans;
    if (type == XML_ELEMENT) {
        XmlElement element;
        element.name = name;
        element.content = content ? content : "";
        if (count && (!keys || !values)) return nullptr;
        for (size_t i = 0; i < count; ++i)
            element.attributes[keys[i] ? keys[i] : ""] = values[i] ? values[i] : "";

        switch (pos) {
            case XML_CHILD:  ans = n.Add<Child>(element); break;
            case XML_BEFORE: ans = n.Add<Before>(element); break;
            case XML_AFTER:  ans = n.Add<After>(element); break;
            default: return nullptr;
        }
    } else if (type == XML_STRING) {
        const std::string xml = XML ? XML : "";
        switch (pos) {
            case XML_CHILD:  ans = n.Add<Child>(xml); break;
            case XML_BEFORE: ans = n.Add<Before>(xml); break;
            case XML_AFTER:  ans = n.Add<After>(xml); break;
            default: return nullptr;
        }
    } else return nullptr;

    TakeNodeError(n);
    if (!LastNodeErr) TakeNodeError(ans);
    return ans.node;
}

/* One C entry point for all three C++ Move<Pos> specializations. */
void XmlNode_Move(xmlNodePtr node, int pos, xmlNodePtr destination)
{
    ClearLastNodeError();
    if (!node) return;

    XmlNode n(node);
    XmlNode target(destination);
    switch (pos) {
        case XML_CHILD:  n.Move<Child>(target); break;
        case XML_BEFORE: n.Move<Before>(target); break;
        case XML_AFTER:  n.Move<After>(target); break;
        default: return;
    }
    TakeNodeError(n);
}


/* Return attribute count; when capacity is sufficient, fill attribute pairs.
 * The borrowed strings remain valid until the next GetAttributes call on this
 * thread. Callers must copy them before then.
 */
size_t XmlNode_GetAttributes(xmlNodePtr node, attr_t* attributes, size_t capacity)
{
    ClearLastNodeError();
    LastAttributes.clear();
    if (!node) return 0;

    XmlNode n(node);
    LastAttributes = n.Attributes();
    TakeNodeError(n);
    if (LastNodeErr) return 0;

    const size_t count = LastAttributes.size();
    if (attributes && capacity >= count) {
        size_t i = 0;
        for (const auto& [name, value] : LastAttributes) {
            attributes[i].name = name.c_str();
            attributes[i].value = value.c_str();
            ++i;
        }
    }
    return count;
}

void XmlNode_SetAttributes(xmlNodePtr node, const attr_t* attributes, size_t count)
{
    ClearLastNodeError();
    if (!node || (count && !attributes)) return;

    std::map<std::string, std::string> values;
    for (size_t i = 0; i < count; ++i)
        values[attributes[i].name ? attributes[i].name : ""] =
            attributes[i].value ? attributes[i].value : "";

    XmlNode n(node);
    n.Attributes(std::move(values));
    TakeNodeError(n);
}

void XmlNode_Delete(xmlNodePtr node)
{
    ClearLastNodeError();

    if (!node)
        return;

    XmlNode n(node);
    n.Delete();
    TakeNodeError(n);
}


/*
 * XmlJrnl methods ------------------------------------------------------------
 *
 * The journal remains the single C++ implementation.  These functions only
 * expose its existing methods across the C ABI.
 */

void* XmlDoc_Journal(void* owner)
{
    ClearLastDocError();

    XmlDoc* doc = CDoc(owner);
    return doc ? doc->JRNL : nullptr;
}


void XmlJrnl_Undo(void* journal)
{
    ClearLastDocError();

    XmlJrnl* jrnl = static_cast<XmlJrnl*>(journal);
    if (!jrnl) return;

    jrnl->Undo();
    TakeDocError(jrnl);
}


void XmlJrnl_Redo(void* journal)
{
    ClearLastDocError();

    XmlJrnl* jrnl = static_cast<XmlJrnl*>(journal);
    if (!jrnl) return;

    jrnl->Redo();
    TakeDocError(jrnl);
}


void XmlJrnl_MarkRelease(void* journal, const char* note)
{
    ClearLastDocError();

    XmlJrnl* jrnl = static_cast<XmlJrnl*>(journal);
    if (!jrnl) return;

    jrnl->MarkRelease(note ? note : "");
    TakeDocError(jrnl);
}


const char* XmlJrnl_StampState(void* journal, const char* type, const char* note)
{
    ClearLastDocError();
    LastString.clear();

    XmlJrnl* jrnl = static_cast<XmlJrnl*>(journal);
    if (!jrnl)
        return LastString.c_str();

    LastString = jrnl->StampState(type ? type : "", note ? note : "");
    TakeDocError(jrnl);
    return LastString.c_str();
}


const char* XmlJrnl_RestorePoints(void* journal)
{
    ClearLastDocError();
    LastString.clear();

    XmlJrnl* jrnl = static_cast<XmlJrnl*>(journal);
    if (!jrnl)
        return LastString.c_str();

    LastString = "<RestorePoints>";
    auto states = jrnl->XPath<std::vector<XmlNode>>("//State[@Type='RestorePoint']");
    TakeDocError(jrnl);

    if (LastDocErr) {
        LastString.clear();
        return LastString.c_str();
    }

    for (const auto& state : states)
        LastString += state.XML();

    LastString += "</RestorePoints>";
    return LastString.c_str();
}


void XmlJrnl_Restore(void* journal, const char* jid)
{
    ClearLastDocError();

    XmlJrnl* jrnl = static_cast<XmlJrnl*>(journal);
    if (!jrnl) return;

    jrnl->Restore(jid ? jid : "");
    TakeDocError(jrnl);
}


void XmlJrnl_Archive(void* journal)
{
    ClearLastDocError();

    XmlJrnl* jrnl = static_cast<XmlJrnl*>(journal);
    if (!jrnl) return;

    jrnl->Archive();
    TakeDocError(jrnl);
}

void XmlJrnl_Prune(void* journal)
{
    ClearLastDocError();

    XmlJrnl* jrnl = static_cast<XmlJrnl*>(journal);
    if (!jrnl) return;

    jrnl->Prune();
    TakeDocError(jrnl);
}


} // extern "C"