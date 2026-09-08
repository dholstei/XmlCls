#include "XmlCls.h"
#include "base64.h"

/**
 * @brief Convert the current libxml2 global/thread error into XmlCls error state.
 *
 * Intended for typed XPath specializations where the default return value must
 * match the requested C++ type.
 */
#define XML_ERROR(T, data) \
    do { \
        xmlError e = *xmlGetLastError(); \
        err = new Error{lvl::ERR, e.message, data}; \
        xmlResetLastError(); return T(); \
    } while(0)

#define MUTABLE_CHECK(d, ret)                                             \
    do {                                                                  \
        XmlDoc* owner = d ? static_cast<XmlDoc*>(d->_private) : nullptr;  \
        if (owner && owner->immutable) {                                  \
            err = new Error{lvl::WARN, "Source DOM is immutable", std::string()}; \
            ret;                                                          \
        }                                                                 \
    } while (0)

Error* SetXmlError(const std::string& context) {
    Error* err = new Error();
    const xmlError* xerr = xmlGetLastError();
    if (xerr && xerr->message)
        err->msg = xerr->message;
    else
        err->msg = "Unknown libxml error";

    err->level = ERR;
    err->data = context;
    return err;
}

namespace {

std::string JournalPath(const XmlDoc& source, const char* filename)
{
    std::string path = filename ? filename : "";
    if (path.empty() || path.front() == '/' || !source.doc || !source.doc->URL)
        return path;

    std::string source_path = (const char*)source.doc->URL;
    std::size_t slash = source_path.find_last_of('/');
    return slash == std::string::npos ? path : source_path.substr(0, slash + 1) + path;
}

}

XmlDoc::XmlDoc(const char *filename)
    : doc(xmlReadFile(filename, NULL, XML_PARSE_NOBLANKS))
{
    if (doc == NULL) { 
        xmlError e = *xmlGetLastError(); 
        err = new Error{lvl::ERR, e.message, filename}; 
        xmlResetLastError();
        return;
    }
    doc->_private = this;
}

XmlDoc::XmlDoc(const std::string content)
    : doc(xmlReadMemory(content.c_str(), content.length(), "noname.xml", NULL, XML_PARSE_NOBLANKS))
{
    if (doc == NULL)
    {
        xmlError e = *xmlGetLastError();
        err = new Error{lvl::ERR, e.message, std::string(e.str1)};
        xmlResetLastError();
        return;
    }
    doc->_private = this;
}

void XmlDoc::Save(const char* filename) {
    if (!doc || !filename) return;
    MUTABLE_CHECK(doc, return);

    if (JRNL)
        { JRNL->StampState("Save", ""); if (JRNL->err) { err = JRNL->err; return; } }

    bool rc = xmlSaveFormatFileEnc(filename, doc, "UTF-8", 1) >= 0;
    if (!rc) { err = SetXmlError(filename); return;}

    if (!doc->URL || strcmp((const char*)doc->URL, filename) != 0) {
        if (doc->URL) xmlFree((void*) doc->URL); 
        doc->URL = xmlStrdup(BAD_CAST filename);
    }

    if (JRNL)
        { JRNL->Save(); if (JRNL->err) err = JRNL->err; }
}

void XmlDoc::Save() {
    if (!doc) return;
    MUTABLE_CHECK(doc, return);
    const char* url = (const char*)doc->URL;
    if (!url || !*url) return;
    Save(url);
}

XmlDoc::~XmlDoc()
{
    clear();
}

void XmlDoc::OpenJournal(const char* filename)
{
    const std::string path = JournalPath(*this, filename);
    JRNL = new XmlJrnl(*this, path.c_str());

    if (JRNL->err) {
        immutable = true;
        err = JRNL->err;
        return;
    }

    JRNL->ValidateState();

    if (JRNL->err) {
        immutable = true;
        err = JRNL->err;
        return;
    }

    immutable = false;
}

void XmlDoc::CreateJournal(const char* filename, std::string XML)
{
    if (!filename || !*filename) {
        err = new Error{lvl::ERR, "Cannot create journal: filename is empty", ""};
        return;
    }

    xmlNodePtr root = xmlDocGetRootElement(doc);
    if (!root || !xmlSetProp(root, BAD_CAST "JRNL", BAD_CAST filename)) {
        err = new Error{lvl::ERR, "Cannot set source JRNL filename", ""};
        return;
    }

    char* seed =
        "<JRNL>"
        "  <Release Number=\"0\" Open=\"%s\" Close=\"\">"
        "    <Release Number=\"1\" Open=\"%s\" Close=\"\">"
        "    </Release>"
        "  </Release>"
        "</JRNL>";

    if (XML.empty()) {
        char buf[1024];
        snprintf(buf, sizeof(buf), seed, CurrentIsoTimestampUTC().c_str(), CurrentIsoTimestampUTC().c_str());
        XML = std::string(buf);
    }

    JRNL = new XmlJrnl(*this, XML);

    if (JRNL->err) {
        err = JRNL->err;
        return;
    }

    const std::string path = JournalPath(*this, filename);
    JRNL->Save(path.c_str());

    if (JRNL->err) {
        err = JRNL->err;
        return;
    }
}

void XmlDoc::clear() {
    if (ctxt) {
        xmlXPathFreeContext(ctxt);
        ctxt = nullptr;
    }
    if (doc) {
        if (JRNL) { JRNL->Save(); delete JRNL; JRNL = nullptr; }
        // xmlFreeDoc(doc);
        // doc = nullptr;
    }
}

template <>
std::string XmlDoc::XPath<std::string>(std::string query, xmlNodePtr node)
{
    if (!ctxt) ctxt = XPathContext();
    xmlXPathObjectPtr result = node
        ? xmlXPathNodeEval(node, BAD_CAST query.c_str(), ctxt)
        : xmlXPathEvalExpression(BAD_CAST query.c_str(), ctxt);
    
    if (result == nullptr) XML_ERROR(std::string, query);
    std::string ans;

    if (result->type == XPATH_STRING)
        ans = std::string((const char *)result->stringval);

    else if (result->type == XPATH_NODESET)
    {
        auto NL = result->nodesetval;
        if (!NL || NL->nodeNr != 1) {
            err = new Error{lvl::ERR, "No single node, not compatible for \"std::string\" type", query};
            xmlXPathFreeObject(result); return ans; }
        result = xmlXPathNodeEval(NL->nodeTab[0], (const xmlChar*) "string(.)", ctxt);

        if (result->type != XPATH_STRING)
            err = new Error{lvl::ERR, "Couldn't determine intermediate string for \"std::string\" type", query};

        else ans = std::string((const char *)result->stringval);
    }

    else
    {
        xmlXPathFreeObject(result);
        err = new Error{lvl::ERR, "Result type is not \"string\"", query};
    }
    xmlXPathFreeObject(result);
    return ans;
}

template <>
double XmlDoc::XPath<double>(std::string query, xmlNodePtr node)
{
    if (!ctxt) ctxt = XPathContext();
    xmlXPathObjectPtr result = node
        ? xmlXPathNodeEval(node, BAD_CAST query.c_str(), ctxt)
        : xmlXPathEvalExpression(BAD_CAST query.c_str(), ctxt);

    if (result == nullptr) XML_ERROR(double, query);

    double ans = 0.0;
    if (result->type == XPATH_NUMBER)
    {
        if (xmlXPathIsNaN(result->floatval)) err = new Error{lvl::ERR, "Result is NaN!", query};
        else if (xmlXPathIsInf(result->floatval)) err = new Error{lvl::ERR, "Result is infinite!", query};
        else ans = result->floatval;
    }
    
    else if (result->type == XPATH_NODESET)
    {
        auto NL = result->nodesetval;
        if (!NL || NL->nodeNr != 1) {
            err = new Error{lvl::ERR, "No single node, not compatible for \"double\" type", query};
            xmlXPathFreeObject(result); return ans; }
        result = xmlXPathNodeEval(NL->nodeTab[0], (const xmlChar*) "number(.)", ctxt);

        if (result->type != XPATH_NUMBER) {
            err = new Error{lvl::ERR, "Couldn't determine number for \"double\" type", query};
            xmlXPathFreeObject(result); return ans; }

        if (xmlXPathIsNaN(result->floatval)) err = new Error{lvl::ERR, "Result is NaN!", query};
        else if (xmlXPathIsInf(result->floatval)) err = new Error{lvl::ERR, "Result is infinite!", query};
        else ans = result->floatval;
    }

    else
        err = new Error{lvl::ERR, "Result type is not \"number\"!", query};
    
    xmlXPathFreeObject(result);
    return ans;
}

template <>
int XmlDoc::XPath<int>(std::string query, xmlNodePtr node)
{
    double ans = XmlDoc::XPath<double>(query, node);
    if (err) return 0;
    if (ans != static_cast<int>(ans)) {
        err = new Error{lvl::WARN, "Result is not an integer, truncating", query};
    }
    
    return int(ans);
}

template <>
bool XmlDoc::XPath<bool>(std::string query, xmlNodePtr node)
{
    if (!ctxt) ctxt = XPathContext();
    xmlXPathObjectPtr result = node
        ? xmlXPathNodeEval(node, BAD_CAST query.c_str(), ctxt)
        : xmlXPathEvalExpression(BAD_CAST query.c_str(), ctxt);

    if (result == nullptr) XML_ERROR(bool, query);
    bool ans = false;

    if (result->type == XPATH_BOOLEAN)
        ans = result->boolval;
    
    else if (result->type == XPATH_NODESET)
        ans = result->nodesetval->nodeNr > 0;

    else
        err = new Error{lvl::ERR, "Result type is not \"boolean!\"", query};
        
    xmlXPathFreeObject(result);
    return ans;
}

template <>
std::vector<XmlNode> XmlDoc::XPath<std::vector<XmlNode>>(std::string query, xmlNodePtr node)
{
    std::vector<XmlNode> NL;
    if (!ctxt) ctxt = XPathContext();
    xmlXPathObjectPtr result = node
        ? xmlXPathNodeEval(node, BAD_CAST query.c_str(), ctxt)
        : xmlXPathEvalExpression(BAD_CAST query.c_str(), ctxt);

    if (result == nullptr) XML_ERROR(std::vector<XmlNode>, query);

    if (result->type == XPATH_NODESET)
    {
        auto ans = result->nodesetval;
        if (!ans) {
            xmlXPathFreeObject(result);
            return std::vector<XmlNode>();
        }
        NL.reserve(ans->nodeNr);
        for (int i = 0; i < ans->nodeNr; i++) NL.emplace_back(XmlNode(ans->nodeTab[i]));
        xmlXPathFreeObject(result);
        return NL;
    }
    else
    {
        xmlXPathFreeObject(result);
        err = new Error{lvl::ERR, "Result type is not \"nodelist/resultset\"!", query};
    }
    return std::vector<XmlNode>();
}

template <>
XmlNode XmlDoc::XPath<XmlNode>(std::string query, xmlNodePtr node)
{
    if (!ctxt) ctxt = XPathContext();
    xmlXPathObjectPtr result = node
        ? xmlXPathNodeEval(node, BAD_CAST query.c_str(), ctxt)
        : xmlXPathEvalExpression(BAD_CAST query.c_str(), ctxt);
        
    if (result == nullptr) XML_ERROR(XmlNode, query);
    XmlNode ans;

    if (result->type == XPATH_NODESET)
    {
        if (!result->nodesetval) {
            err = new Error{lvl::ERR, "Result is NULL!", query};
        }

        else switch (result->nodesetval->nodeNr)
        {
            case 0:
                err = new Error{lvl::ERR, "Result is NULL!", query};
                break;
            case 1:
                ans = XmlNode(result->nodesetval->nodeTab[0]);
                break;
            default:
                ans = XmlNode(result->nodesetval->nodeTab[0]);
                err = new Error{lvl::WARN, "Result is ambiguous, not a single node!", query};
                break;
        }
    }
    else
    {
        err = new Error{lvl::ERR, "Result type is not \"nodelist/resultset\"!", query};
    }
    xmlXPathFreeObject(result);
    return ans;
}

xmlXPathContextPtr XmlDoc::XPathContext()
{
    if (ctxt) return ctxt;
    else {
        ctxt = xmlXPathNewContext(doc);
        if (ctxt == NULL)
        {
            err = new Error{lvl::ERR, "Fatal error on XPath context", doc->URL ? (char *)doc->URL : "unknown"};
            return nullptr;
        }
        return ctxt;
    }
}

bool Child::noop(XmlNode& node)
{
    if (node.node->parent != destination.node)
        return false;

    auto next = node.XPath<std::vector<XmlNode>>("following-sibling::*[1]");
    return !node.err && next.empty();
}

xmlNodePtr Child::Place(xmlNodePtr node)
{
    return xmlAddChild(destination.node, node);
}

bool Before::noop(XmlNode& node)
{
    auto prev = destination.XPath<std::vector<XmlNode>>("preceding-sibling::*[1]");
    return !destination.err && !prev.empty() && prev[0].node == node.node;
}

xmlNodePtr Before::Place(xmlNodePtr node)
{
    return xmlAddPrevSibling(destination.node, node);
}

bool After::noop(XmlNode& node)
{
    auto next = destination.XPath<std::vector<XmlNode>>("following-sibling::*[1]");
    return !destination.err && !next.empty() && next[0].node == node.node;
}

xmlNodePtr After::Place(xmlNodePtr node)
{
    return xmlAddNextSibling(destination.node, node);
}

void XmlNode::parse(std::string XML)
{
    if (!node || !node->doc) return;
    MUTABLE_CHECK(node->doc, return);

    xmlDocPtr ownerDoc = node->doc;

    xmlDocPtr tempDoc = xmlReadMemory(XML.c_str(), XML.size(), nullptr, nullptr, 0);
    if (!tempDoc) {
        err = SetXmlError(XML.substr(0, 200));
        return;
    }

    xmlNodePtr parsedRoot = xmlDocGetRootElement(tempDoc);
    if (!parsedRoot) {
        xmlFreeDoc(tempDoc);
        err = SetXmlError("Could not extract root node from new XML");
        return;
    }

    xmlNodePtr imported = xmlDocCopyNode(parsedRoot, ownerDoc, 1);
    xmlFreeDoc(tempDoc);

    if (!imported) {
        err = new Error{lvl::ERR, "Could not copy node into target XML document", XML.substr(0, 200)};
        return;
    }

    xmlNodePtr oldNode = node;
    std::string jid;

    if (JRNL) {
        jid = this->JID();  // Ensure the node has a JID before logging the modification
        JRNL->LogModify(*this, oldNode ? this->XML() : std::string());
    }

    xmlReplaceNode(oldNode, imported);
    xmlFreeNode(oldNode);

    node = imported;
    if (JRNL)
        this->JID(jid);
}

static xmlNodePtr XmlNodeFromString(const std::string& XmlStr, xmlDocPtr ownerDoc, ErrorPtr& err)
{
    if (!ownerDoc) {
        err = new Error{lvl::ERR, "Node is not attached to an XML document", XmlStr.substr(0, 200)};
        return nullptr;
    }

    xmlDocPtr tempDoc = xmlReadMemory(XmlStr.c_str(), (int)XmlStr.size(), nullptr, nullptr, 0);
    if (!tempDoc) {
        err = SetXmlError(XmlStr.substr(0, 200));
        return nullptr;
    }

    xmlNodePtr parsedRoot = xmlDocGetRootElement(tempDoc);
    if (!parsedRoot) {
        xmlFreeDoc(tempDoc);
        err = new Error{lvl::ERR, "Could not extract root node from XML", XmlStr.substr(0, 200)};
        return nullptr;
    }

    xmlNodePtr imported = xmlDocCopyNode(parsedRoot, ownerDoc, 1);
    xmlFreeDoc(tempDoc);

    if (!imported) {
        err = new Error{lvl::ERR, "Could not copy node into target XML document", XmlStr.substr(0, 200)};
        return nullptr;
    }

    return imported;
}

template<typename Pos>
XmlNode XmlNode::Add(Pos pos, std::string XmlStr)
{
    if (!pos.destination.node || !pos.destination.doc) {
        err = new Error{lvl::ERR, "Cannot Add: invalid destination", XmlStr.substr(0, 200)};
        return XmlNode();
    }

    MUTABLE_CHECK(pos.destination.doc, return XmlNode());

    xmlNodePtr imported = XmlNodeFromString(XmlStr, pos.destination.doc, err);
    if (!imported)
        return XmlNode();

    xmlNodePtr added = pos.Place(imported);

    if (!added) {
        xmlFreeNode(imported);
        err = new Error{lvl::ERR, "Cannot Add: XML placement failed", XmlStr.substr(0, 200)};
        return XmlNode();
    }

    XmlNode result(added);

    if (result.JRNL) {
        result.JRNL->LogAdd(result);

        if (result.JRNL->err) {
            result.err = result.JRNL->err;
            err = result.err;
        }
    }

    return result;
}

template XmlNode XmlNode::Add<Before>(Before, std::string XmlStr);
template XmlNode XmlNode::Add<After>(After, std::string XmlStr);
template XmlNode XmlNode::Add<Child>(Child, std::string XmlStr);

XmlNode XmlNode::AddChild(std::string XmlStr)
{
    return Add(Child{*this}, XmlStr);
}

XmlNode XmlNode::AddBefore(std::string XmlStr)
{
    return Add(Before{*this}, XmlStr);
}

XmlNode XmlNode::AddAfter(std::string XmlStr)
{
    return Add(After{*this}, XmlStr);
}

template<typename Pos>
void XmlNode::Move(Pos pos)
{
    if (!node || !node->doc) {
        err = new Error{lvl::ERR, "Cannot Move: invalid source XmlNode", ""};
        return;
    }

    MUTABLE_CHECK(node->doc, return);

    if (!pos.destination.node || !pos.destination.doc) {
        err = new Error{lvl::ERR, "Cannot Move: invalid destination", GetPath()};
        return;
    }

    if (node->doc != pos.destination.doc) {
        err = new Error{lvl::ERR, "Cannot Move: destination must belong to the same DOM", GetPath()};
        return;
    }

    if (node == pos.destination.node)
        return;

    if (pos.noop(*this)) {
        if (pos.destination.err) err = pos.destination.err;
        return;
    }

    if (pos.destination.err) {
        err = pos.destination.err;
        return;
    }

    if (!JRNL) {
        xmlUnlinkNode(node);
        if (!pos.Place(node))
            err = new Error{lvl::ERR, "Cannot Move: XML insertion failed", GetPath()};
        return;
    }

    ActionMove action(*JRNL, *this);
    if (action.err) { err = action.err; return; }

    xmlUnlinkNode(node);

    if (!pos.Place(node)) {
        err = new Error{lvl::ERR, "Cannot Move: XML insertion failed", GetPath()};
        return;
    }

    action.Record();
    if (action.err) err = action.err;
}

template void XmlNode::Move<Before>(Before);
template void XmlNode::Move<After>(After);
template void XmlNode::Move<Child>(Child);

void XmlNode::MoveChild(XmlNode parent)
{
    Move(Child{parent});
}

void XmlNode::MoveBefore(XmlNode sibling)
{
    Move(Before{sibling});
}

void XmlNode::MoveAfter(XmlNode sibling)
{
    Move(After{sibling});
}

std::string XmlNode::JID()
{
    if (!node) return {};

    xmlChar* value = xmlGetProp(node, BAD_CAST "JID");

    if (value) {
        std::string jid(reinterpret_cast<const char*>(value));
        xmlFree(value);
        return jid;
    }

    if (!JRNL) {
        err = new Error{ lvl::ERR, "Cannot create JID: node is not associated with a journal", GetPath() };
        return {};
    }

    std::string jid = JRNL->JID();

    if (!xmlSetProp(node, BAD_CAST "JID", BAD_CAST jid.c_str())) {
        err = new Error{ lvl::ERR, "Unable to assign JID", GetPath() };
        return {};
    }

    JRNL->jid_map[jid] = node;

    return jid;
}

void XmlNode::JID(std::string jid)
{
    if (!node || jid.empty())
        return;

    if (!JRNL) {
        err = new Error{ lvl::ERR, "Cannot set JID: XmlNode is not associated with a journal", GetPath() };
        return;
    }

    if (!xmlSetProp(node, BAD_CAST "JID", BAD_CAST jid.c_str())) {
        err = new Error{ lvl::ERR, "Unable to set JID \"" + jid + "\"", GetPath() };
        return;
    }

    JRNL->jid_map[jid] = node;
}

void XmlNode::Delete()
{
    if (!node) return;
    std::string jid;
    MUTABLE_CHECK(node->doc, return);

    if (JRNL) {
        auto parent = this->XPath<std::vector<XmlNode>>("..")[0];
        (void) parent.JID();
        if (parent.err) { err = parent.err; return; }

        auto children = parent.XPath<std::vector<XmlNode>>("./*");
        for (auto& child : children)
            {(void) child.JID();
                if (parent.err) { err = parent.err; return; }}


        jid = this->JID();
        if (err) return;

        JRNL->LogDelete(*this);
        if (JRNL->err) { err = JRNL->err; return; }
    }

    if (JRNL && !jid.empty())
        JRNL->jid_map[jid] = nullptr;

    xmlNodePtr doomed = node;

    node = nullptr;
    doc  = nullptr;
    ctxt = nullptr;
    JRNL = nullptr;

    xmlUnlinkNode(doomed);
    xmlFreeNode(doomed);
}

#define JRNL_CHECK_NODE(N)                                              \
    do {                                                                \
        if (!(N).node || !(N).doc) {                                    \
            err = new Error{lvl::ERR, "Invalid XmlNode", std::string()};\
            return;                                                     \
        }                                                               \
        XmlDoc* owner = static_cast<XmlDoc*>((N).doc->_private);        \
        if (!owner) {                                                   \
            err = new Error{lvl::ERR, "XmlNode has no canonical XmlDoc", (N).GetPath()}; \
            return;                                                     \
        }                                                               \
        if (owner->immutable) {                                         \
            err = new Error{lvl::WARN, "XmlNode is immutable due to DOM state", std::string()}; \
            return;                                                     \
        }                                                               \
        if ((N).doc != source_doc.doc) {                                \
            err = new Error{lvl::ERR, "XmlNode does not belong to this journal's source DOM", (N).GetPath()}; \
            return;                                                     \
        }                                                               \
    } while (0)

XmlJrnl::XmlJrnl(XmlDoc& source, const char* filename): XmlDoc(filename), source_doc(source) {
    if (err)
        { source.immutable = true; return; }

    RefreshActiveRelease();
    if (err)
        { source.immutable = true; return; }

    BuildJIDMap();
    if (err)
        { source.immutable = true; return; }
}

XmlJrnl::XmlJrnl(XmlDoc& source, const std::string content) : XmlDoc(content), source_doc(source){
    if (err)
        { source.immutable = true; return; }

    RefreshActiveRelease();
    if (err)
        { source.immutable = true; return; }

    BuildJIDMap();
    if (err)
        { source.immutable = true; return; }
}

void XmlJrnl::LogAdd(XmlNode& node)
{
    JRNL_CHECK_NODE(node);

    ActionAdd action(*this, node);
    action.Record();

    if (action.err)
        err = action.err;
}

void XmlJrnl::LogModify(XmlNode& node, const std::string& oldXML)
{
    JRNL_CHECK_NODE(node);

    ActionModify action(*this, node, oldXML);
    action.Record();

    if (action.err)
        err = action.err;
}

void XmlJrnl::LogDelete(XmlNode& node)
{
    JRNL_CHECK_NODE(node);

    ActionDelete action(*this, node);
    action.Record();

    if (action.err)
        err = action.err;
}

void XmlJrnl::Undo()
{
    MUTABLE_CHECK(source_doc.doc, return);
    if (!active_release.node) {
        err = new Error{ lvl::ERR, "Cannot undo: journal has no active release", "" };
        return;
    }

    auto actions = active_release.XPath<std::vector<XmlNode>>( "./Change[Reversed/@Value='false'][last()]" );

    if (active_release.err) {
        err = active_release.err; return;
    }

    if (actions.empty()) return;

    Undo(actions[0]);
}

void XmlJrnl::Undo(XmlNode action_node)
{
    MUTABLE_CHECK(source_doc.doc, return);
    if (!action_node.node) {
        err = new Error{lvl::ERR, "Cannot undo: invalid journal action node", ""};
        return;
    }

    if (action_node.XPath<bool>("./Reversed[@Value='true']"))
        return;

    const std::string type = action_node.XPath<std::string>("@Type");

    if (type == "Modify") {
        ActionModify action(*this, action_node);
        action.Undo();

        if (action.err)
            err = action.err;

        return;
    }
    else if (type == "Deletion") {
        ActionDelete action(*this, action_node, true);
        action.Undo();
        if (action.err) err = action.err;
        return;
    }
    else if (type == "Add") {
        ActionAdd action(*this, action_node, true);
        action.Undo();

        if (action.err)
            err = action.err;

        return;
    }
    else if (type == "Move") {
        ActionMove action(*this, action_node, true);
        action.Undo();
        if (action.err) err = action.err;
        return;
    }
    err = new Error{lvl::ERR, "Undo currently implemented only for Modify transactions", action_node.GetPath()};
}

void XmlJrnl::Undo(std::vector<XmlNode> action_nodes) {
    MUTABLE_CHECK(source_doc.doc, return);
    for (auto it = action_nodes.rbegin(); it != action_nodes.rend(); ++it) {
        Undo(*it);
        if (err) return;
    }
}
void XmlJrnl::RefreshActiveRelease()
{
    rel_no.clear();
    active_release = XmlNode();

    auto roots = XPath<std::vector<XmlNode>>("/JRNL/Release[@Close='']");
    if (roots.empty()) {
        err = new Error{lvl::ERR, "No open root Release in journal", ""};
        return;
    }

    active_release = FindActiveRelease(roots.back(), rel_no);
}

XmlNode XmlJrnl::FindActiveRelease(XmlNode current, std::vector<int>& path)
{
    int n = current.XPath<int>("number(@Number)");
    path.push_back(n);

    auto children = current.XPath<std::vector<XmlNode>>("./Release[@Close='']");

    if (children.empty())
        return current;

    return FindActiveRelease(children.back(), path);
}

void XmlJrnl::BuildJIDMap()
{
    jid_map.clear();

    /*
     * Live source-node identities.
     */
    auto live = source_doc.XPath<std::vector<XmlNode>>("//*/@JID");
    if (source_doc.err) { err = source_doc.err; return; }

    for (auto& n : live) {
        std::string jid = n.XPath<std::string>(".");

        auto [it, inserted] = jid_map.emplace(jid, n.node->parent);

        if (!inserted) {
            err = new Error{lvl::ERR, "Duplicate JID \"" + jid + "\"", n.GetPath()};
            return;
        }
    }

    /*
     * Reserved journal identities.
     *
     * Change and State JIDs share the same namespace as source-node JIDs.
     * nullptr means reserved but not currently associated with a live
     * source xmlNodePtr.
     */
    auto reserved = XPath<std::vector<XmlNode>>("//Change/@JID | //State/@JID");
    if (err) return;

    for (auto& n : reserved) {
        std::string jid = n.XPath<std::string>(".");

        auto it = jid_map.find(jid);

        if (it == jid_map.end()) {
            jid_map.emplace(jid, nullptr);
            continue;
        }

        /*
         * A Change normally refers to a source-node JID, so finding the
         * same JID already mapped to a live node is valid.
         *
         * A State JID, however, should never collide with another identity.
         */
        XmlNode owner(n.node->parent);
        std::string name = owner.XPath<std::string>("name(.)");

        if (name == "State") {
            err = new Error{lvl::ERR, "State JID \"" + jid + "\" duplicates an existing JID", n.GetPath()};
            return;
        }
    }
}

std::string XmlJrnl::JID()
{
    static thread_local std::mt19937_64 rng{std::random_device{}()};

    for (;;) {
        char buf[17];

        std::snprintf(
            buf,
            sizeof(buf),
            "%016llx",
            static_cast<unsigned long long>(rng())
        );

        std::string jid(buf);

        if (jid_map.find(jid) == jid_map.end())
            return jid;
    }
}

std::string XmlJrnl::StampState(std::string type, std::string note)
{
    if (!active_release.node) {
        err = new Error{lvl::ERR, "Cannot stamp state: journal has no active release", ""};
        return {};
    }

    if (type.empty()) {
        err = new Error{lvl::ERR, "Cannot stamp state: Type is empty", active_release.GetPath()};
        return {};
    }

    xmlNodePtr root = xmlDocGetRootElement(source_doc.doc);
    if (!root) {
        err = new Error{lvl::ERR, "Cannot stamp state: source document has no document element", ""};
        return {};
    }
    const std::string jid = JID();

    XmlNode state = active_release.AddChild("<State/>");
    if (state.err) {
        err = state.err;
        return {};
    }

    const std::string timestamp = CurrentIsoTimestampUTC();

    if (!xmlSetProp(state.node, BAD_CAST "Type", BAD_CAST type.c_str()) ||
        !xmlSetProp(state.node, BAD_CAST "JID", BAD_CAST jid.c_str()) ||
        !xmlSetProp(state.node, BAD_CAST "TimeStamp", BAD_CAST timestamp.c_str())) {
        err = new Error{lvl::ERR, "Cannot stamp state: unable to set State attributes", state.GetPath()};
        return {};
    }

    if (!note.empty() && !xmlSetProp(state.node, BAD_CAST "Note", BAD_CAST note.c_str())) {
        err = new Error{lvl::ERR, "Cannot stamp state: unable to set Note", state.GetPath()};
        return {};
    }

    /*
     * Reserve the State JID in the same namespace as node/change JIDs.
     * nullptr means the JID is reserved but does not identify a live source node.
     */
    jid_map[jid] = nullptr;

    if (!xmlSetProp(root, BAD_CAST "STATE_JID", BAD_CAST jid.c_str())) {
        err = new Error{lvl::ERR, "Cannot stamp state: unable to set source STATE_JID", ""};
        return {};
    }

    return jid;
}

void XmlJrnl::MarkRelease(std::string note)
{
    if (!active_release.node) {
        err = new Error{lvl::ERR, "Cannot mark release: journal has no active release", ""};
        return;
    }

    auto parents = active_release.XPath<std::vector<XmlNode>>("..");
    if (parents.size() != 1) {
        err = new Error{lvl::ERR, "Cannot mark release: active release has no parent", active_release.GetPath()};
        return;
    }

    int number = active_release.XPath<int>("number(@Number)") + 1;
    auto siblings = parents[0].XPath<std::vector<XmlNode>>("./Release/@Number");
    for (auto& sibling : siblings)
        number = std::max(number, sibling.XPath<int>("number(.)") + 1);

    const std::string timestamp = CurrentIsoTimestampUTC();
    if (!xmlSetProp(active_release.node, BAD_CAST "Close", BAD_CAST timestamp.c_str())) {
        err = new Error{lvl::ERR, "Cannot mark release: unable to close active release", active_release.GetPath()};
        return;
    }

    xmlNodePtr release = xmlNewNode(nullptr, BAD_CAST "Release");
    if (!release ||
        !xmlSetProp(release, BAD_CAST "Number", BAD_CAST std::to_string(number).c_str()) ||
        !xmlSetProp(release, BAD_CAST "Open", BAD_CAST timestamp.c_str()) ||
        !xmlSetProp(release, BAD_CAST "Close", BAD_CAST "") ||
        (!note.empty() && !xmlSetProp(release, BAD_CAST "Note", BAD_CAST note.c_str()))) {
        if (release) xmlFreeNode(release);
        err = new Error{lvl::ERR, "Cannot mark release: unable to create release entry", ""};
        return;
    }

    if (!xmlAddNextSibling(active_release.node, release)) {
        xmlFreeNode(release);
        err = new Error{lvl::ERR, "Cannot mark release: unable to place release entry", active_release.GetPath()};
        return;
    }

    RefreshActiveRelease();
    if (!err) Save();
}

void XmlJrnl::Restore(std::string jid)
{
    XmlNode restore_point;
    auto states = XPath<std::vector<XmlNode>>("//State[@Type='RestorePoint']");
    if (err) return;

    for (auto& state : states) {
        if (state.XPath<std::string>("@JID") == jid) {
            restore_point = state;
            break;
        }
    }

    if (!restore_point.node) {
        err = new Error{lvl::ERR, "Cannot restore: restore-point JID was not found", jid};
        return;
    }

    auto actions = restore_point.XPath<std::vector<XmlNode>>("following::Change[Reversed/@Value='false']");
    if (restore_point.err) { err = restore_point.err; return; }

    Undo(actions);
    if (err) return;

    if (StampState("Restore", jid).empty()) return;
    Save();
}

void XmlJrnl::ValidateState()
{
    const std::string source_state = source_doc.XPath<std::string>("string(/*/@STATE_JID)");

    if (source_doc.err) {
        err = source_doc.err;
        source_doc.immutable = true;
        return;
    }

    if (source_state.empty()) {
        err = new Error{lvl::WARN, "JRNL-controlled DOM has no STATE_JID", ""};
        source_doc.immutable = true;
        return;
    }

    auto states = XPath<std::vector<XmlNode>>("//State[@JID='" + source_state + "']");

    if (err) {
        source_doc.immutable = true;
        return;
    }

    if (states.empty()) {
        err = new Error{lvl::WARN, "Journal does not contain DOM STATE_JID \"" + source_state + "\"", source_doc.doc->URL ? (char*)source_doc.doc->URL : ""};
        source_doc.immutable = true;
        return;
    }

    auto latest = XPath<std::vector<XmlNode>>("(//State)[last()]");

    if (latest.empty()) {
        err = new Error{lvl::WARN, "Journal contains no State records", ""};
        source_doc.immutable = true;
        return;
    }

    const std::string latest_jid = latest[0].XPath<std::string>("@JID");

    if (latest_jid != source_state) {
        err = new Error{lvl::WARN, "DOM STATE_JID does not match latest journal State", latest[0].GetPath()};
        source_doc.immutable = true;
        return;
    }

    source_doc.immutable = false;
}

/* -------------------------------------------------------------------------
 * Journal Action implementations
 *
 * Action::Record()/ReverseStamp()/Conflict() contain transaction mechanics
 * common to all actions.  ActionModify, ActionDelete, and ActionAdd contain
 * only the payload and inverse operation specific to their mutation type.
 * ------------------------------------------------------------------------- */

ActionModify::ActionModify(XmlJrnl& j, XmlNode n, const std::string& old)
    : Action(j), node(n), oldXML(old)
{
    type = "Modify";
    jid = node.JID();

    if (node.err)
        err = node.err;
}

ActionModify::ActionModify(XmlJrnl& j, XmlNode action)
    : Action(j, action)
{
    type = "Modify";
    jid = action_node.XPath<std::string>("@JID");

    if (action_node.err)
        err = action_node.err;
}

void Action::ReverseStamp()
{
    auto reversed = action_node.XPath<std::vector<XmlNode>>("./Reversed");

    if (reversed.size() != 1) {
        err = new Error{lvl::ERR, "Journal action contains invalid Reversed state", action_node.GetPath()};
        return;
    }

    xmlSetProp(reversed[0].node, BAD_CAST "Value", BAD_CAST "true");

    const std::string timestamp = CurrentIsoTimestampUTC();
    xmlSetProp(reversed[0].node, BAD_CAST "TimeStamp", BAD_CAST timestamp.c_str());
}

void ActionModify::Record()
{
    if (err) return;

    XmlNode parent = node.XPath<std::vector<XmlNode>>("..")[0];
    const std::string parent_jid = parent.JID();
    if (parent.err || parent_jid.empty()) { err = parent.err; return; }

    Action::Record();
    if (err) return;

    action_node.AddChild("<Parent JID=\"" + parent_jid + "\"/>");
    if (action_node.err) { err = action_node.err; return; }

    action_node.AddChild("<Node Encoding=\"Base64\">" + base64_encode(oldXML) + "</Node>");
    if (action_node.err) err = action_node.err;
}

void ActionModify::Undo()
{
    if (!action_node.node) {
        err = new Error{lvl::ERR, "Cannot undo Modify: invalid journal action node", ""};
        return;
    }

    const std::string journal_path = action_node.GetPath();

    if (action_node.XPath<bool>("./Reversed[@Value='true']"))
        return;

    if (jid.empty()) {
        err = new Error{lvl::ERR, "Cannot undo Modify: journal transaction has no JID", journal_path};
        return;
    }

    /*
     * JID must identify the current live incarnation of this logical node.
     */
    const std::string parent_jid = action_node.XPath<std::string>("./Parent/@JID");

    if (parent_jid.empty()) {
        err = new Error{lvl::ERR, "Cannot undo Modify: journal transaction has no Parent JID", journal_path};
        return;
    }

    auto pit = jrnl.jid_map.find(parent_jid);

    if (pit == jrnl.jid_map.end() || !pit->second) {
        auto causes = jrnl.XPath<std::vector<XmlNode>>("//Change[@JID='" + parent_jid + "']");

        if (!causes.empty())
            Conflict("parent node is no longer available", causes.back());
        else
            err = new Error{lvl::ERR, "Cannot undo Modify: parent JID \"" + parent_jid + "\" is not present in the source DOM", journal_path};

        return;
    }

    auto it = jrnl.jid_map.find(jid);

    if (it == jrnl.jid_map.end() || !it->second) {
        auto causes = jrnl.XPath<std::vector<XmlNode>>("//Change[@JID='" + jid + "']");

        if (!causes.empty())
            Conflict("modified node is no longer available", causes.back());
        else
            err = new Error{lvl::ERR, "Cannot undo Modify: JID \"" + jid + "\" is not present in the source DOM", journal_path};

        return;
    }

    xmlNodePtr current = it->second;

    if (current->doc != jrnl.source_doc.doc) {
        err = new Error{lvl::ERR, "Cannot undo Modify: JID \"" + jid + "\" belongs to an incompatible DOM", journal_path};
        return;
    }

    /*
     * Recover the previous serialized state.
     */
    const std::string encoded = action_node.XPath<std::string>("./Node");

    if (encoded.empty()) {
        err = new Error{lvl::ERR, "Cannot undo Modify: journal contains no previous node state", journal_path};
        return;
    }

    const std::string oldXML = base64_decode(encoded);
    xmlNodePtr restored = XmlNodeFromString(oldXML, jrnl.source_doc.doc, err);

    if (!restored) {
        if (err)
            err->data = journal_path;
        else
            err = new Error{lvl::ERR, "Cannot undo Modify: saved XML cannot be restored", journal_path};

        return;
    }

    /*
     * Saved state must represent the same logical node.
     */
    XmlNode restored_node(restored);
    const std::string restored_jid = restored_node.XPath<std::string>("@JID");

    if (restored_jid != jid) {
        xmlFreeNode(restored);
        err = new Error{lvl::ERR, "Cannot undo Modify: saved node JID does not match transaction JID", journal_path};
        return;
    }

    /*
     * Replace the current physical node with its previous incarnation.
     */
    xmlNodePtr replaced = xmlReplaceNode(current, restored);

    if (replaced != current) {
        xmlFreeNode(restored);
        err = new Error{lvl::ERR, "Cannot undo Modify: xmlReplaceNode failed", journal_path};
        return;
    }

    xmlFreeNode(current);

    /*
     * Logical identity remains the same; only xmlNodePtr changed.
     */
    jrnl.jid_map[jid] = restored;

    ReverseStamp();
}

ActionDelete::ActionDelete(XmlJrnl& j, XmlNode n) : Action(j), node(n) {
    type = "Deletion";
    jid = node.JID();
    if (node.err) err = node.err;
}

ActionDelete::ActionDelete(XmlJrnl& j, XmlNode action, bool) : Action(j, action) {
    type = "Deletion";
    jid = action_node.XPath<std::string>("@JID");
    if (action_node.err) err = action_node.err;
}

void ActionDelete::Record()
{
    if (err) return;

    XmlNode parent = node.XPath<std::vector<XmlNode>>("..")[0];
    std::string parent_jid = parent.JID();
    if (parent.err || parent_jid.empty()) { err = parent.err; return; }

    XmlNode before;
    auto before_nodes = node.XPath<std::vector<XmlNode>>("preceding-sibling::*[1]");
    if (!before_nodes.empty()) before = before_nodes[0];

    XmlNode after;
    auto after_nodes = node.XPath<std::vector<XmlNode>>("following-sibling::*[1]");
    if (!after_nodes.empty()) after = after_nodes[0];

    Action::Record();
    if (err) return;

    action_node.AddChild("<Parent JID=\"" + parent_jid + "\"/>");
    if (action_node.err) { err = action_node.err; return; }

    if (before.node) {
        action_node.AddChild("<Before JID=\"" + before.JID() + "\"/>");
        if (action_node.err) { err = action_node.err; return; }
    }

    if (after.node) {
        action_node.AddChild("<After JID=\"" + after.JID() + "\"/>");
        if (action_node.err) { err = action_node.err; return; }
    }

    action_node.AddChild("<Node Encoding=\"Base64\">" + base64_encode(node.XML()) + "</Node>");
    if (action_node.err) err = action_node.err;
}

void ActionDelete::Undo()
{
    if (!action_node.node) {
        err = new Error{lvl::ERR, "Cannot undo Deletion: invalid journal action node", ""};
        return;
    }

    const std::string journal_path = action_node.GetPath();

    if (action_node.XPath<bool>("./Reversed[@Value='true']"))
        return;

    if (jid.empty()) {
        err = new Error{lvl::ERR, "Cannot undo Deletion: journal transaction has no JID", journal_path};
        return;
    }

    const std::string parent_jid = action_node.XPath<std::string>("./Parent/@JID");
    if (parent_jid.empty()) {
        err = new Error{lvl::ERR, "Cannot undo Deletion: journal transaction has no Parent JID", journal_path};
        return;
    }

    auto pit = jrnl.jid_map.find(parent_jid);
    if (pit == jrnl.jid_map.end() || !pit->second) {
        Conflict("parent node is no longer available", action_node);
        return;
    }

    XmlNode parent(pit->second);

    XmlNode before;
    if (action_node.XPath<bool>("./Before")) {
        const std::string before_jid = action_node.XPath<std::string>("./Before/@JID");
        auto it = jrnl.jid_map.find(before_jid);

        if (it == jrnl.jid_map.end() || !it->second) {
            Conflict("preceding sibling is no longer available", action_node);
            return;
        }

        before = XmlNode(it->second);

        if (before.node->parent != parent.node) {
            Conflict("preceding sibling is no longer under the recorded parent", action_node);
            return;
        }
    }

    XmlNode after;
    if (action_node.XPath<bool>("./After")) {
        const std::string after_jid = action_node.XPath<std::string>("./After/@JID");
        auto it = jrnl.jid_map.find(after_jid);

        if (it == jrnl.jid_map.end() || !it->second) {
            Conflict("following sibling is no longer available", action_node);
            return;
        }

        after = XmlNode(it->second);

        if (after.node->parent != parent.node) {
            Conflict("following sibling is no longer under the recorded parent", action_node);
            return;
        }
    }

    if (before.node && after.node) {
        auto next = before.XPath<std::vector<XmlNode>>("following-sibling::*[1]");

        if (next.size() != 1 || next[0].node != after.node) {
            Conflict("deletion slot has been changed", action_node);
            return;
        }
    }

    if (!before.node && !after.node && parent.XPath<bool>("./*")) {
        Conflict("parent now contains children that did not exist at deletion", action_node);
        return;
    }

    const std::string encoded = action_node.XPath<std::string>("./Node");

    if (encoded.empty()) {
        err = new Error{lvl::ERR, "Cannot undo Deletion: journal contains no deleted node", journal_path};
        return;
    }

    const std::string oldXML = base64_decode(encoded);
    xmlNodePtr restored = XmlNodeFromString(oldXML, jrnl.source_doc.doc, err);

    if (!restored) {
        if (err) err->data = journal_path;
        else err = new Error{lvl::ERR, "Cannot undo Deletion: saved XML cannot be restored", journal_path};
        return;
    }

    XmlNode restored_node(restored);

    if (restored_node.XPath<std::string>("@JID") != jid) {
        xmlFreeNode(restored);
        err = new Error{lvl::ERR, "Cannot undo Deletion: saved node JID does not match transaction JID", journal_path};
        return;
    }

    xmlNodePtr inserted = nullptr;

    if (before.node)
        inserted = xmlAddNextSibling(before.node, restored);
    else if (after.node)
        inserted = xmlAddPrevSibling(after.node, restored);
    else
        inserted = xmlAddChild(parent.node, restored);

    if (!inserted) {
        xmlFreeNode(restored);
        err = new Error{lvl::ERR, "Cannot undo Deletion: node could not be restored", journal_path};
        return;
    }

    XmlNode inserted_node(inserted);
    inserted_node.JID(jid);

    if (inserted_node.err) {
        err = inserted_node.err;
        return;
    }

    ReverseStamp();
}

ActionAdd::ActionAdd(XmlJrnl& j, XmlNode n) : Action(j), node(n) {
    type = "Add";
    jid = node.JID();
    if (node.err) err = node.err;
}

ActionAdd::ActionAdd(XmlJrnl& j, XmlNode action, bool) : Action(j, action) {
    type = "Add";
    jid = action_node.XPath<std::string>("@JID");
    if (action_node.err) err = action_node.err;
}

void ActionAdd::Record()
{
    if (err) return;

    XmlNode parent = node.XPath<std::vector<XmlNode>>("..")[0];
    const std::string parent_jid = parent.JID();

    if (parent.err || parent_jid.empty()) {
        err = parent.err;
        return;
    }

    Action::Record();
    if (err) return;

    action_node.AddChild("<Parent JID=\"" + parent_jid + "\"/>");
    if (action_node.err) err = action_node.err;
}

void ActionAdd::Undo()
{
    if (!action_node.node) {
        err = new Error{lvl::ERR, "Cannot undo Add: invalid journal action node", ""};
        return;
    }

    const std::string journal_path = action_node.GetPath();

    if (action_node.XPath<bool>("./Reversed[@Value='true']"))
        return;

    if (jid.empty()) {
        err = new Error{lvl::ERR, "Cannot undo Add: journal transaction has no JID", journal_path};
        return;
    }

    const std::string parent_jid = action_node.XPath<std::string>("./Parent/@JID");

    if (parent_jid.empty()) {
        err = new Error{lvl::ERR, "Cannot undo Add: journal transaction has no Parent JID", journal_path};
        return;
    }

    auto pit = jrnl.jid_map.find(parent_jid);

    if (pit == jrnl.jid_map.end() || !pit->second) {
        Conflict("parent node is no longer available", action_node);
        return;
    }

    auto it = jrnl.jid_map.find(jid);

    if (it == jrnl.jid_map.end() || !it->second) {
        Conflict("added node is no longer available", action_node);
        return;
    }

    xmlNodePtr current = it->second;

    if (current->parent != pit->second) {
        Conflict("added node is no longer under its recorded parent", action_node);
        return;
    }

    xmlUnlinkNode(current);
    xmlFreeNode(current);

    /*
     * Keep the identity reserved in the journal namespace.
     */
    jrnl.jid_map[jid] = nullptr;

    ReverseStamp();
}

ActionMove::ActionMove(XmlJrnl& j, XmlNode n) : Action(j), node(n)
{
    type = "Move";
    jid = node.JID();

    if (node.err || jid.empty()) {
        err = node.err;
        return;
    }

    auto parents = node.XPath<std::vector<XmlNode>>("..");
    if (parents.size() != 1) {
        err = new Error{lvl::ERR, "Cannot Move: node has no parent", node.GetPath()};
        return;
    }

    from_parent = parents[0].JID();
    if (parents[0].err) { err = parents[0].err; return; }

    auto before = node.XPath<std::vector<XmlNode>>("preceding-sibling::*[1]");
    if (node.err) { err = node.err; return; }

    if (!before.empty()) {
        from_before = before[0].JID();
        if (before[0].err) { err = before[0].err; return; }
    }

    auto after = node.XPath<std::vector<XmlNode>>("following-sibling::*[1]");
    if (node.err) { err = node.err; return; }

    if (!after.empty()) {
        from_after = after[0].JID();
        if (after[0].err) { err = after[0].err; return; }
    }
}

ActionMove::ActionMove(XmlJrnl& j, XmlNode action, bool) : Action(j, action)
{
    type = "Move";
    jid = action_node.XPath<std::string>("@JID");

    if (action_node.err)
        err = action_node.err;
}

void ActionMove::Record()
{
    if (err) return;

    auto parents = node.XPath<std::vector<XmlNode>>("..");
    if (parents.size() != 1) {
        err = new Error{lvl::ERR, "Cannot record Move: node has no destination parent", node.GetPath()};
        return;
    }

    const std::string to_parent = parents[0].JID();
    if (parents[0].err) { err = parents[0].err; return; }

    std::string to_before;
    auto before = node.XPath<std::vector<XmlNode>>("preceding-sibling::*[1]");
    if (node.err) { err = node.err; return; }

    if (!before.empty()) {
        to_before = before[0].JID();
        if (before[0].err) { err = before[0].err; return; }
    }

    std::string to_after;
    auto after = node.XPath<std::vector<XmlNode>>("following-sibling::*[1]");
    if (node.err) { err = node.err; return; }

    if (!after.empty()) {
        to_after = after[0].JID();
        if (after[0].err) { err = after[0].err; return; }
    }

    Action::Record();
    if (err) return;

    XmlNode from = action_node.AddChild("<From/>");
    if (from.err) { err = from.err; return; }

    from.AddChild("<Parent JID=\"" + from_parent + "\"/>");
    if (from.err) { err = from.err; return; }

    if (!from_before.empty()) {
        from.AddChild("<Before JID=\"" + from_before + "\"/>");
        if (from.err) { err = from.err; return; }
    }

    if (!from_after.empty()) {
        from.AddChild("<After JID=\"" + from_after + "\"/>");
        if (from.err) { err = from.err; return; }
    }

    XmlNode to = action_node.AddChild("<To/>");
    if (to.err) { err = to.err; return; }

    to.AddChild("<Parent JID=\"" + to_parent + "\"/>");
    if (to.err) { err = to.err; return; }

    if (!to_before.empty()) {
        to.AddChild("<Before JID=\"" + to_before + "\"/>");
        if (to.err) { err = to.err; return; }
    }

    if (!to_after.empty()) {
        to.AddChild("<After JID=\"" + to_after + "\"/>");
        if (to.err) { err = to.err; return; }
    }
}

void ActionMove::Undo()
{
    if (!action_node.node) {
        err = new Error{lvl::ERR, "Cannot undo Move: invalid journal action node", ""};
        return;
    }

    const std::string journal_path = action_node.GetPath();

    if (action_node.XPath<bool>("./Reversed[@Value='true']"))
        return;

    auto it = jrnl.jid_map.find(jid);
    if (it == jrnl.jid_map.end() || !it->second) {
        Conflict("moved node is no longer available", action_node);
        return;
    }

    XmlNode current(it->second);

    const std::string to_parent = action_node.XPath<std::string>("./To/Parent/@JID");
    const std::string to_before = action_node.XPath<std::string>("./To/Before/@JID");
    const std::string to_after = action_node.XPath<std::string>("./To/After/@JID");

    auto pit = jrnl.jid_map.find(to_parent);
    if (pit == jrnl.jid_map.end() || !pit->second) {
        Conflict("Move destination parent is no longer available", action_node);
        return;
    }

    if (current.node->parent != pit->second) {
        Conflict("moved node is no longer under its recorded destination parent", action_node);
        return;
    }

    auto before = current.XPath<std::vector<XmlNode>>("preceding-sibling::*[1]");
    auto after = current.XPath<std::vector<XmlNode>>("following-sibling::*[1]");

    if ((!to_before.empty() && (before.empty() || before[0].JID() != to_before)) ||
        (to_before.empty() && !before.empty())) {
        Conflict("Move destination Before relationship has changed", action_node);
        return;
    }

    if ((!to_after.empty() && (after.empty() || after[0].JID() != to_after)) ||
        (to_after.empty() && !after.empty())) {
        Conflict("Move destination After relationship has changed", action_node);
        return;
    }

    const std::string from_parent = action_node.XPath<std::string>("./From/Parent/@JID");
    const std::string from_before = action_node.XPath<std::string>("./From/Before/@JID");
    const std::string from_after = action_node.XPath<std::string>("./From/After/@JID");

    pit = jrnl.jid_map.find(from_parent);
    if (pit == jrnl.jid_map.end() || !pit->second) {
        Conflict("Move original parent is no longer available", action_node);
        return;
    }

    xmlNodePtr parent = pit->second;

    if (!from_before.empty()) {
        auto bit = jrnl.jid_map.find(from_before);

        if (bit == jrnl.jid_map.end() || !bit->second || bit->second->parent != parent) {
            Conflict("Move original Before sibling is no longer available", action_node);
            return;
        }

        xmlUnlinkNode(current.node);

        if (!xmlAddNextSibling(bit->second, current.node)) {
            err = new Error{lvl::ERR, "Cannot undo Move: xmlAddNextSibling failed", journal_path};
            return;
        }
    }
    else if (!from_after.empty()) {
        auto ait = jrnl.jid_map.find(from_after);

        if (ait == jrnl.jid_map.end() || !ait->second || ait->second->parent != parent) {
            Conflict("Move original After sibling is no longer available", action_node);
            return;
        }

        xmlUnlinkNode(current.node);

        if (!xmlAddPrevSibling(ait->second, current.node)) {
            err = new Error{lvl::ERR, "Cannot undo Move: xmlAddPrevSibling failed", journal_path};
            return;
        }
    }
    else {
        XmlNode p(parent);

        if (p.XPath<bool>("./*")) {
            Conflict("Move original parent no longer has an empty element slot", action_node);
            return;
        }

        xmlUnlinkNode(current.node);

        if (!xmlAddChild(parent, current.node)) {
            err = new Error{lvl::ERR, "Cannot undo Move: xmlAddChild failed", journal_path};
            return;
        }
    }

    ReverseStamp();
}
