#include "XmlCls.h"

#include <cstring>

namespace {
thread_local std::string c_api_error;

void ClearCError()
{
    c_api_error.clear();
}

void SetCError(const ErrorPtr err, const char* fallback)
{
    if (err) {
        c_api_error = err->msg;
        if (!err->data.empty()) {
            if (!c_api_error.empty()) c_api_error += ": ";
            c_api_error += err->data;
        }
    }
    if (c_api_error.empty() && fallback)
        c_api_error = fallback;
}

XmlDoc* CDoc(void* owner)
{
    return static_cast<XmlDoc*>(owner);
}
}

extern "C" {

/**
 * @brief Attach a canonical C++ XmlDoc wrapper to an existing libxml2 document.
 * @param doc Borrowed libxml2 document pointer. The document must not already
 *            have an owner stored in xmlDoc::_private.
 * @return Opaque XmlDoc owner handle, or nullptr on failure.
 *
 * The returned wrapper does not assume ownership of @p doc. Release the wrapper
 * with XmlDoc_Detach(). If the document element declares a JRNL attribute, its
 * journal is opened and validated automatically. A journal error leaves the
 * source DOM attached and readable, but XmlCls_LastError() reports the problem
 * and journal validation can make the wrapper immutable.
 */
void* XmlDoc_Attach(xmlDocPtr doc)
{
    ClearCError();
    if (!doc) {
        SetCError(nullptr, "Cannot attach a NULL xmlDocPtr");
        return nullptr;
    }
    if (doc->_private) {
        SetCError(nullptr, "xmlDocPtr already has a canonical XmlDoc owner");
        return nullptr;
    }
    XmlDoc* wrapper = new XmlDoc(doc);
    if (wrapper->err)
        SetCError(wrapper->err, "Unable to open declared XML journal");
    return wrapper;
}

/**
 * @brief Release an opaque XmlDoc owner created by XmlDoc_Attach().
 * @param owner Opaque XmlDoc owner handle; nullptr is accepted.
 *
 * The underlying xmlDocPtr remains owned by the caller.
 */
void XmlDoc_Detach(void* owner)
{
    ClearCError();
    XmlDoc* wrapper = CDoc(owner);
    if (!wrapper) return;
    if (wrapper->doc && wrapper->doc->_private == wrapper)
        wrapper->doc->_private = nullptr;
    delete wrapper;
}

/**
 * @brief Save an attached XML document.
 * @param owner Opaque XmlDoc owner handle.
 * @param filename Destination XML filename encoded as UTF-8.
 * @return 1 on success, otherwise 0.
 */
int XmlDoc_Save(void* owner, const char* filename)
{
    ClearCError();
    XmlDoc* wrapper = CDoc(owner);
    if (!wrapper || !filename) {
        SetCError(nullptr, "XmlDoc_Save requires an owner and filename");
        return 0;
    }
    wrapper->err = nullptr;
    wrapper->Save(filename);
    if (wrapper->err) {
        SetCError(wrapper->err, "Unable to save XML document");
        return 0;
    }
    return 1;
}

/**
 * @brief Open and attach an existing mutation journal.
 * @param owner Opaque XmlDoc owner handle for the source DOM.
 * @param filename Journal filename encoded as UTF-8. A relative filename is
 *                 resolved relative to the source XML file.
 * @return 1 when the journal opens and validates, otherwise 0.
 */
int XmlDoc_OpenJournal(void* owner, const char* filename)
{
    ClearCError();
    XmlDoc* wrapper = CDoc(owner);
    if (!wrapper || !filename) {
        SetCError(nullptr, "XmlDoc_OpenJournal requires an owner and filename");
        return 0;
    }
    wrapper->err = nullptr;
    wrapper->OpenJournal(filename);
    if (wrapper->err) {
        SetCError(wrapper->err, "Unable to open XML journal");
        return 0;
    }
    return 1;
}

/**
 * @brief Create and attach a mutation journal.
 * @param owner Opaque XmlDoc owner handle for the source DOM.
 * @param filename Journal filename encoded as UTF-8. The value is recorded in
 *                 the source document element's JRNL attribute.
 * @param XML Optional complete journal seed document encoded as UTF-8; nullptr
 *            or an empty string selects the default journal structure.
 * @return 1 on success, otherwise 0.
 */
int XmlDoc_CreateJournal(void* owner, const char* filename, const char* XML)
{
    ClearCError();
    XmlDoc* wrapper = CDoc(owner);
    if (!wrapper || !filename) {
        SetCError(nullptr, "XmlDoc_CreateJournal requires an owner and filename");
        return 0;
    }
    wrapper->err = nullptr;
    wrapper->CreateJournal(filename, XML ? XML : "");
    if (wrapper->err) {
        SetCError(wrapper->err, "Unable to create XML journal");
        return 0;
    }
    return 1;
}

/**
 * @brief Undo the most recent unreversed action in the active release.
 * @param owner Opaque XmlDoc owner handle with an attached journal.
 * @return 1 on success, otherwise 0.
 */
int XmlDoc_Undo(void* owner)
{
    ClearCError();
    XmlDoc* wrapper = CDoc(owner);
    if (!wrapper || !wrapper->JRNL) {
        SetCError(nullptr, "XmlDoc has no open journal");
        return 0;
    }
    wrapper->JRNL->err = nullptr;
    wrapper->JRNL->Undo();
    if (wrapper->JRNL->err) {
        SetCError(wrapper->JRNL->err, "Unable to undo journal action");
        return 0;
    }
    return 1;
}

/**
 * @brief Redo the next redoable action in the active release.
 * @param owner Opaque XmlDoc owner handle with an attached journal.
 * @return 1 on success, otherwise 0.
 */
int XmlDoc_Redo(void* owner)
{
    ClearCError();
    XmlDoc* wrapper = CDoc(owner);
    if (!wrapper || !wrapper->JRNL) {
        SetCError(nullptr, "XmlDoc has no open journal");
        return 0;
    }
    wrapper->JRNL->err = nullptr;
    wrapper->JRNL->Redo();
    if (wrapper->JRNL->err) {
        SetCError(wrapper->JRNL->err, "Unable to redo journal action");
        return 0;
    }
    return 1;
}

/**
 * @brief Report whether a mutable document has an attached journal.
 * @param owner Opaque XmlDoc owner handle.
 * @return 1 when a valid journal is attached and the source is mutable;
 *         otherwise 0.
 */
int XmlDoc_HasJournal(void* owner)
{
    ClearCError();
    XmlDoc* wrapper = CDoc(owner);
    return wrapper && wrapper->JRNL && !wrapper->immutable ? 1 : 0;
}

/**
 * @brief Close the active release and open its next numbered release.
 * @param owner Opaque XmlDoc owner handle with an attached journal.
 * @param note Optional UTF-8 release comment; nullptr is treated as empty.
 * @return 1 on success, otherwise 0.
 */
int XmlDoc_MarkRelease(void* owner, const char* note)
{
    ClearCError();
    XmlDoc* wrapper = CDoc(owner);
    if (!wrapper || !wrapper->JRNL) {
        SetCError(nullptr, "XmlDoc has no open journal");
        return 0;
    }
    wrapper->JRNL->err = nullptr;
    wrapper->JRNL->MarkRelease(note ? note : "");
    if (wrapper->JRNL->err) {
        SetCError(wrapper->JRNL->err, "Unable to mark journal release");
        return 0;
    }
    return 1;
}

/**
 * @brief Add a named restore point to the active journal release.
 * @param owner Opaque XmlDoc owner handle with an attached journal.
 * @param note Optional UTF-8 comment; nullptr is treated as empty.
 * @param jid Caller-provided buffer receiving the 16-character hexadecimal JID.
 * @param capacity Size of @p jid in bytes; at least 17 bytes are required.
 * @return 1 on success, otherwise 0.
 */
int XmlDoc_MarkRestorePoint(void* owner, const char* note, char* jid, size_t capacity)
{
    ClearCError();
    XmlDoc* wrapper = CDoc(owner);
    if (!wrapper || !wrapper->JRNL || !jid || capacity < 17) {
        SetCError(nullptr, "MarkRestorePoint requires a journal and a 17-byte JID buffer");
        return 0;
    }
    wrapper->JRNL->err = nullptr;
    const std::string value = wrapper->JRNL->StampState("RestorePoint", note ? note : "");
    if (wrapper->JRNL->err || value.empty()) {
        SetCError(wrapper->JRNL->err, "Unable to mark restore point");
        return 0;
    }
    memcpy(jid, value.c_str(), value.size() + 1);
    wrapper->JRNL->Save();
    if (wrapper->JRNL->err) {
        SetCError(wrapper->JRNL->err, "Unable to save restore point");
        return 0;
    }
    return 1;
}

/**
 * @brief Serialize the available restore-point records as XML.
 * @param owner Opaque XmlDoc owner handle with an attached journal.
 * @param buffer Caller-provided output buffer, or nullptr to query the size.
 * @param capacity Size of @p buffer in bytes.
 * @return Required buffer size including the terminating null byte. Returns 0
 *         on error. When capacity is insufficient, the required size is
 *         returned without writing the XML.
 */
size_t XmlDoc_RestorePoints(void* owner, char* buffer, size_t capacity)
{
    ClearCError();
    XmlDoc* wrapper = CDoc(owner);
    if (!wrapper || !wrapper->JRNL) {
        SetCError(nullptr, "XmlDoc has no open journal");
        return 0;
    }

    std::string XML = "<RestorePoints>";
    auto states = wrapper->JRNL->XPath<std::vector<XmlNode>>("//State[@Type='RestorePoint']");
    if (wrapper->JRNL->err) {
        SetCError(wrapper->JRNL->err, "Unable to list restore points");
        return 0;
    }
    for (const auto& state : states) XML += state.XML();
    XML += "</RestorePoints>";

    const size_t required = XML.size() + 1;
    if (!buffer) return required;
    if (capacity < required) return required;
    memcpy(buffer, XML.c_str(), required);
    return required;
}

/**
 * @brief Restore the source DOM to a recorded restore point.
 * @param owner Opaque XmlDoc owner handle with an attached journal.
 * @param jid Null-terminated restore-point JID.
 * @return 1 on success, otherwise 0.
 */
int XmlDoc_Restore(void* owner, const char* jid)
{
    ClearCError();
    XmlDoc* wrapper = CDoc(owner);
    if (!wrapper || !wrapper->JRNL || !jid) {
        SetCError(nullptr, "Restore requires a journal and restore-point JID");
        return 0;
    }
    wrapper->JRNL->err = nullptr;
    wrapper->JRNL->Restore(jid);
    if (wrapper->JRNL->err) {
        SetCError(wrapper->JRNL->err, "Unable to restore journal state");
        return 0;
    }
    return 1;
}

/**
 * @brief Serialize one libxml2 node as XML.
 * @param node Node to serialize.
 * @param buffer Caller-provided output buffer, or nullptr to query the size.
 * @param capacity Size of @p buffer in bytes.
 * @return Required buffer size including the terminating null byte. Returns 0
 *         on error; unlike XmlDoc_RestorePoints(), an undersized non-null
 *         buffer is reported as an error.
 */
size_t XmlNode_XML(xmlNodePtr node, char* buffer, size_t capacity)
{
    ClearCError();
    if (!node) {
        SetCError(nullptr, "Cannot serialize a NULL xmlNodePtr");
        return 0;
    }
    XmlNode wrapper(node);
    const std::string XML = wrapper.XML();
    const size_t required = XML.size() + 1;
    if (!buffer) return required;
    if (capacity < required) {
        SetCError(nullptr, "XmlNode_XML buffer is too small");
        return 0;
    }
    memcpy(buffer, XML.c_str(), required);
    return required;
}

/**
 * @brief Replace a node with XML parsed in the context of its document.
 * @param node Node to replace.
 * @param XML Null-terminated UTF-8 XML fragment containing one element.
 * @return Pointer to the replacement node, or nullptr on failure.
 */
xmlNodePtr XmlNode_Parse(xmlNodePtr node, const char* XML)
{
    ClearCError();
    if (!node || !XML) {
        SetCError(nullptr, "XmlNode_Parse requires a node and XML");
        return nullptr;
    }
    XmlNode wrapper(node);
    wrapper.parse(XML);
    if (wrapper.err) {
        SetCError(wrapper.err, "Unable to replace XML node");
        return nullptr;
    }
    return wrapper.node;
}

/**
 * @brief Parse and append an element as the selected node's final child.
 * @param node Parent node.
 * @param XML Null-terminated UTF-8 XML fragment containing one element.
 * @return Pointer to the added node, or nullptr on failure.
 */
xmlNodePtr XmlNode_AddChild(xmlNodePtr node, const char* XML)
{
    ClearCError();
    if (!node || !XML) {
        SetCError(nullptr, "XmlNode_AddChild requires a node and XML");
        return nullptr;
    }
    XmlNode source(node);
    XmlNode added = source.AddChild(XML);
    if (source.err || added.err || !added.node) {
        SetCError(source.err ? source.err : added.err, "Unable to add child node");
        return nullptr;
    }
    return added.node;
}

/**
 * @brief Parse and insert an element immediately before the selected node.
 * @param node Reference node.
 * @param XML Null-terminated UTF-8 XML fragment containing one element.
 * @return Pointer to the added node, or nullptr on failure.
 */
xmlNodePtr XmlNode_AddBefore(xmlNodePtr node, const char* XML)
{
    ClearCError();
    if (!node || !XML) {
        SetCError(nullptr, "XmlNode_AddBefore requires a node and XML");
        return nullptr;
    }
    XmlNode source(node);
    XmlNode added = source.AddBefore(XML);
    if (source.err || added.err || !added.node) {
        SetCError(source.err ? source.err : added.err, "Unable to add node before selection");
        return nullptr;
    }
    return added.node;
}

/**
 * @brief Parse and insert an element immediately after the selected node.
 * @param node Reference node.
 * @param XML Null-terminated UTF-8 XML fragment containing one element.
 * @return Pointer to the added node, or nullptr on failure.
 */
xmlNodePtr XmlNode_AddAfter(xmlNodePtr node, const char* XML)
{
    ClearCError();
    if (!node || !XML) {
        SetCError(nullptr, "XmlNode_AddAfter requires a node and XML");
        return nullptr;
    }
    XmlNode source(node);
    XmlNode added = source.AddAfter(XML);
    if (source.err || added.err || !added.node) {
        SetCError(source.err ? source.err : added.err, "Unable to add node after selection");
        return nullptr;
    }
    return added.node;
}

/**
 * @brief Move a node to become the destination node's final child.
 * @param node Node to move.
 * @param parent Destination parent node in the same document.
 * @return 1 on success, otherwise 0.
 */
int XmlNode_MoveChild(xmlNodePtr node, xmlNodePtr parent)
{
    ClearCError();
    if (!node || !parent) {
        SetCError(nullptr, "XmlNode_MoveChild requires a node and parent");
        return 0;
    }
    XmlNode source(node);
    source.MoveChild(XmlNode(parent));
    if (source.err) {
        SetCError(source.err, "Unable to move XML node as child");
        return 0;
    }
    return 1;
}

/**
 * @brief Move a node immediately before a destination sibling.
 * @param node Node to move.
 * @param sibling Destination sibling node in the same document.
 * @return 1 on success, otherwise 0.
 */
int XmlNode_MoveBefore(xmlNodePtr node, xmlNodePtr sibling)
{
    ClearCError();
    if (!node || !sibling) {
        SetCError(nullptr, "XmlNode_MoveBefore requires a node and sibling");
        return 0;
    }
    XmlNode source(node);
    source.MoveBefore(XmlNode(sibling));
    if (source.err) {
        SetCError(source.err, "Unable to move XML node before selection");
        return 0;
    }
    return 1;
}

/**
 * @brief Move a node immediately after a destination sibling.
 * @param node Node to move.
 * @param sibling Destination sibling node in the same document.
 * @return 1 on success, otherwise 0.
 */
int XmlNode_MoveAfter(xmlNodePtr node, xmlNodePtr sibling)
{
    ClearCError();
    if (!node || !sibling) {
        SetCError(nullptr, "XmlNode_MoveAfter requires a node and sibling");
        return 0;
    }
    XmlNode source(node);
    source.MoveAfter(XmlNode(sibling));
    if (source.err) {
        SetCError(source.err, "Unable to move XML node after selection");
        return 0;
    }
    return 1;
}

/**
 * @brief Delete a node from its document.
 * @param node Node to delete.
 * @return 1 on success, otherwise 0.
 *
 * The supplied xmlNodePtr is invalid after a successful deletion.
 */
int XmlNode_Delete(xmlNodePtr node)
{
    ClearCError();
    if (!node) {
        SetCError(nullptr, "Cannot delete a NULL xmlNodePtr");
        return 0;
    }
    XmlNode wrapper(node);
    wrapper.Delete();
    if (wrapper.err) {
        SetCError(wrapper.err, "Unable to delete XML node");
        return 0;
    }
    return 1;
}

/**
 * @brief Return the calling thread's most recent C-interface error message.
 * @return Borrowed null-terminated string owned by XmlClsLib.
 *
 * The pointer remains valid until the next C-interface call on the same thread.
 */
const char* XmlCls_LastError()
{
    return c_api_error.c_str();
}

} // extern "C"
