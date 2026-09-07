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
    return new XmlDoc(doc);
}

void XmlDoc_Detach(void* owner)
{
    ClearCError();
    XmlDoc* wrapper = CDoc(owner);
    if (!wrapper) return;
    if (wrapper->doc && wrapper->doc->_private == wrapper)
        wrapper->doc->_private = nullptr;
    delete wrapper;
}

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

int XmlDoc_HasJournal(void* owner)
{
    ClearCError();
    XmlDoc* wrapper = CDoc(owner);
    return wrapper && wrapper->JRNL && !wrapper->immutable ? 1 : 0;
}

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

const char* XmlCls_LastError()
{
    return c_api_error.c_str();
}

} // extern "C"
