#include "XmlCls.h"
#include "base64.h"

std::mutex doc_map_mtx;
std::map<xmlDocPtr, XmlDoc*> doc_map;
std::map<xmlDocPtr, XmlJrnl*> jrnl_map;

#define XML_ERROR(T, data) \
    do { \
        xmlError e = *xmlGetLastError(); \
        err = new Error{lvl::ERR, e.message, data}; \
        xmlResetLastError(); return T(); \
    } while(0)

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

static std::string CurrentIsoTimestampUTC()
{
    std::time_t now = std::time(nullptr);
    std::tm tm{};

#ifdef _WIN32
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif

    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}

XmlDoc::XmlDoc(const char *filename)
{
    doc = xmlReadFile(filename, NULL, XML_PARSE_NOBLANKS);
    if (doc == NULL) { 
        xmlError e = *xmlGetLastError(); 
        err = new Error{lvl::ERR, e.message, filename}; 
        xmlResetLastError();
    }
    doc_map[doc] = this;
}

XmlDoc::XmlDoc(const char *content, int length)
{
    doc = xmlReadMemory(content, length, "noname.xml", NULL, XML_PARSE_NOBLANKS);
    if (doc == NULL)
    {
        xmlError e = *xmlGetLastError();
        err = new Error{lvl::ERR, e.message, std::string(e.str1)};
        xmlResetLastError();
    }
    doc_map[doc] = this;
}

void XmlDoc::Save(const char* filename) {
    if (!doc || !filename) return;
    bool rc = xmlSaveFormatFileEnc(filename, doc, "UTF-8", 1) >= 0;
    if (!rc) { err = SetXmlError(filename); return;}
    if (!doc->URL || strcmp((const char*)doc->URL, filename) != 0) {
        if (doc->URL) xmlFree((void*) doc->URL); 
        doc->URL = xmlStrdup(BAD_CAST filename);
    }
    return;
}

void XmlDoc::Save() {
    if (!doc) return;
    const char* url = (const char*)doc->URL;
    if (!url || !*url) return;
    Save(url);
}

XmlDoc::~XmlDoc()
{
    clear();
}

void XmlDoc::OpenJournal(const char* filename) {
    JRNL = new XmlJrnl(filename);
    if (!JRNL->doc) { delete JRNL; JRNL = nullptr; }
    jrnl_map[doc] = JRNL;
}

void XmlDoc::CreateJournal(const char* filename, std::string XML) {
    char* seed = "<JRNL>\
  <Release Number=\"0\" Open=\"%s\" Close=\"\">\
    <Release Number=\"1\" Open=\"%s\" Close=\"\">\
    </Release>\
  </Release>\
</JRNL>";
    if (XML.empty()) {
        char buf[1024];
        snprintf(buf, sizeof(buf), seed, CurrentIsoTimestampUTC().c_str(), CurrentIsoTimestampUTC().c_str());
        XML = std::string(buf);
    }
    JRNL = new XmlJrnl(XML.c_str(), XML.length());
    jrnl_map[doc] = JRNL;
}

void XmlDoc::clear() {
    if (doc) {
        if (JRNL) { JRNL->Save(); delete JRNL; JRNL = nullptr; }
        auto it = doc_map.find(doc);
        if (it != doc_map.end()) doc_map.erase(it);
        // xmlFreeDoc(doc);
        doc = nullptr;
    }
}

template <>
std::string XmlDoc::XPath<std::string>(std::string query)
{
    ctxt = GetXPathContext(doc, err);
    xmlXPathObjectPtr result = xmlXPathEvalExpression((const xmlChar *)query.c_str(), ctxt);
    if (result == nullptr) XML_ERROR(std::string, query);

    if (result->type == XPATH_STRING)
    {
        std::string ans = std::string((const char *)result->stringval);
        xmlXPathFreeObject(result);
        return ans;
    }
    else
    {
        xmlXPathFreeObject(result);
        err = new Error{lvl::ERR, "Result type is not \"string\"", query};
    }
    return std::string();
}

template <>
double XmlDoc::XPath<double>(std::string query)
{
    ctxt = GetXPathContext(doc, err);
    xmlXPathObjectPtr result = xmlXPathEvalExpression((const xmlChar *)query.c_str(), ctxt);
    if (result == nullptr) XML_ERROR(double, query);

    double ans = 0.0;
    if (result->type == XPATH_NUMBER)
    {
        if (xmlXPathIsNaN(result->floatval)) err = new Error{lvl::ERR, "Result is NaN!", query};
        else if (xmlXPathIsInf(result->floatval)) err = new Error{lvl::ERR, "Result is infinite!", query};
        else ans = result->floatval;
    }
    else err = new Error{lvl::ERR, "Result type is not \"number\"!", query};
    
    xmlXPathFreeObject(result);
    return ans;
}

template <>
int XmlDoc::XPath<int>(std::string query)
{
    double ans = XmlDoc::XPath<double>(query);
    if (err) return 0;
    if (ans != static_cast<int>(ans)) {
        err = new Error{lvl::WARN, "Result is not an integer, truncating", query};
    }
    
    return int(ans);
}

template <>
bool XmlDoc::XPath<bool>(std::string query)
{
    ctxt = GetXPathContext(doc, err);
    xmlXPathObjectPtr result = xmlXPathEvalExpression((const xmlChar *)query.c_str(), ctxt);
    if (result == nullptr) XML_ERROR(bool, query);

    if (result->type == XPATH_BOOLEAN)
    {
        bool ans = result->boolval;
        xmlXPathFreeObject(result);
        return ans;
    }
    else
    {
        xmlXPathFreeObject(result);
        err = new Error{lvl::ERR, "Result type is not \"boolean!\"", query};
    }
    return false;
}

template <>
std::vector<XmlNode> XmlDoc::XPath<std::vector<XmlNode>>(std::string query)
{
    std::vector<XmlNode> NL;
    ctxt = GetXPathContext(doc, err);
    xmlXPathObjectPtr result = xmlXPathEvalExpression((const xmlChar *)query.c_str(), ctxt);
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

xmlXPathContextPtr GetXPathContext(xmlDocPtr doc, ErrorPtr &err)
{
    xmlXPathContextPtr xpathCtx = nullptr;

    XmlDoc* DOM = doc_map[doc];
    if (DOM) {
        xpathCtx = DOM->ctxt;
        if (xpathCtx) return xpathCtx;
        else {
            xpathCtx = xmlXPathNewContext(doc);
            if (xpathCtx == NULL)
            {
                err = new Error{lvl::ERR, "Fatal error on XPath context", doc->URL ? (char *)doc->URL : "unknown"};
                return nullptr;
            }
            DOM->ctxt = xpathCtx;
            return xpathCtx;
        }
    }

    /* Create xpath evaluation context */
    xpathCtx = xmlXPathNewContext(doc);
    if (xpathCtx == NULL)
    {
        err = new Error{lvl::ERR, "Fatal error on XPath context", doc->URL ? (char *)doc->URL : "unknown"};
        xmlFreeDoc(doc);
        return (nullptr);
    }
    return xpathCtx;
}

template <>
std::string XmlNode::XPath<std::string>(std::string query)
{
    ctxt = GetXPathContext(doc, err);
    xmlXPathObjectPtr result = xmlXPathNodeEval(node, (const xmlChar *)query.c_str(), ctxt);
    if (result == nullptr) XML_ERROR(std::string, query);

    if (result->type == XPATH_STRING)
    {
        std::string ans = std::string((const char *)result->stringval);
        xmlXPathFreeObject(result);
        return ans;
    }
    else
    {
        xmlXPathFreeObject(result);
        err = new Error{lvl::ERR, "Result type is not \"string\"", query};
    }
    return std::string();
}

template <>
double XmlNode::XPath<double>(std::string query)
{
    ctxt = GetXPathContext(doc, err);
    xmlXPathObjectPtr result = xmlXPathNodeEval(node, (const xmlChar *)query.c_str(), ctxt);
    if (result == nullptr) XML_ERROR(double, query);

    double ans = 0.0;
    if (result->type == XPATH_NUMBER)
    {
        if (xmlXPathIsNaN(result->floatval)) err = new Error{lvl::ERR, "Result is NaN!", query};
        else if (xmlXPathIsInf(result->floatval)) err = new Error{lvl::ERR, "Result is infinite!", query};
        else ans = result->floatval;
    }
    else err = new Error{lvl::ERR, "Result type is not \"number\"!", query};
    
    xmlXPathFreeObject(result);
    return ans;
}

template <>
int XmlNode::XPath<int>(std::string query)
{
    double ans = XmlNode::XPath<double>(query);
    if (err) return 0;
    if (ans != static_cast<int>(ans)) {
        err = new Error{lvl::WARN, "Result is not an integer, truncating", query};
    }
    
    return int(ans);
}

template <>
bool XmlNode::XPath<bool>(std::string query)
{
    ctxt = GetXPathContext(doc, err);
    xmlXPathObjectPtr result = xmlXPathNodeEval(node, (const xmlChar *)query.c_str(), ctxt);
    if (result == nullptr) XML_ERROR(bool, query);

    if (result->type == XPATH_BOOLEAN)
    {
        bool ans = result->boolval;
        xmlXPathFreeObject(result);
        return ans;
    }
    else
    {
        xmlXPathFreeObject(result);
        err = new Error{lvl::ERR, "Result type is not \"boolean!\"", query};
    }
    return false;
}

template <>
std::vector<XmlNode> XmlNode::XPath<std::vector<XmlNode>>(std::string query)
{
    std::vector<XmlNode> NL;
    ctxt = GetXPathContext(doc, err);
    xmlXPathObjectPtr result = xmlXPathNodeEval(node, (const xmlChar *)query.c_str(), ctxt);
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

void XmlNode::parse(std::string XML)
{
    if (!node || !node->doc) return;

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

    JRNL::Modify(*this, oldNode ? this->XML() : std::string());

    xmlReplaceNode(oldNode, imported);
    xmlFreeNode(oldNode);

    node = imported;
    doc  = ownerDoc;
    ctxt = nullptr;
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

XmlNode XmlNode::AddChild(std::string XmlStr)
{
    if (!node || !node->doc) {
        err = new Error{lvl::ERR, "Cannot add child to null XmlNode", XmlStr.substr(0, 200)};
        return XmlNode();
    }

    xmlNodePtr imported = XmlNodeFromString(XmlStr, node->doc, err);
    if (!imported) return XmlNode();

    xmlNodePtr added = xmlAddChild(node, imported);
    if (!added) {
        xmlFreeNode(imported);
        err = new Error{lvl::ERR, "xmlAddChild failed", XmlStr.substr(0, 200)};
        return XmlNode();
    }

    return XmlNode(added);
}

XmlNode XmlNode::AddBefore(std::string XmlStr)
{
    if (!node || !node->doc) {
        err = new Error{lvl::ERR, "Cannot add sibling before null XmlNode", XmlStr.substr(0, 200)};
        return XmlNode();
    }
    if (!node->parent) {
        err = new Error{lvl::ERR, "Cannot add sibling before a node with no parent", XmlStr.substr(0, 200)};
        return XmlNode();
    }

    xmlNodePtr imported = XmlNodeFromString(XmlStr, node->doc, err);
    if (!imported) return XmlNode();

    xmlNodePtr added = xmlAddPrevSibling(node, imported);
    if (!added) {
        xmlFreeNode(imported);
        err = new Error{lvl::ERR, "xmlAddPrevSibling failed", XmlStr.substr(0, 200)};
        return XmlNode();
    }

    return XmlNode(added);
}

XmlNode XmlNode::AddAfter(std::string XmlStr)
{
    if (!node || !node->doc) {
        err = new Error{lvl::ERR, "Cannot add sibling after null XmlNode", XmlStr.substr(0, 200)};
        return XmlNode();
    }
    if (!node->parent) {
        err = new Error{lvl::ERR, "Cannot add sibling after a node with no parent", XmlStr.substr(0, 200)};
        return XmlNode();
    }

    xmlNodePtr imported = XmlNodeFromString(XmlStr, node->doc, err);
    if (!imported) return XmlNode();

    xmlNodePtr added = xmlAddNextSibling(node, imported);
    if (!added) {
        xmlFreeNode(imported);
        err = new Error{lvl::ERR, "xmlAddNextSibling failed", XmlStr.substr(0, 200)};
        return XmlNode();
    }

    return XmlNode(added);
}

namespace JRNL {

inline void Log(XmlDoc* journal, const std::string& xml)
{
    if (!journal) return;
    auto root = journal->XPath<std::vector<XmlNode>>("/*");
    if (root.empty()) return;
    root[0].AddChild(xml);
}

void Add(XmlNode& added)
{
    if (!added.doc || !doc_map[added.doc]->JRNL) return;

    std::string timestamp = CurrentIsoTimestampUTC();

    xmlChar* path = xmlGetNodePath(added.node);
    xmlChar* escPath = xmlEncodeSpecialChars(added.doc, path);

    std::string xml =
        std::string("<Change Type=\"Add\" TimeStamp=\"") + timestamp + "\">"
        "<XPathLoc Type=\"Self\">" +
        reinterpret_cast<const char*>(escPath) +
        "</XPathLoc>"
        "<Reversed TimeStamp=\"\" Value=\"false\"/>"
        "</Change>";

    if (escPath) xmlFree(escPath);
    if (path) xmlFree(path);

    Log(doc_map[added.doc]->JRNL, xml);
}

void Modify(XmlNode& node, const std::string& oldXML)
{
    if (!node.doc || !doc_map[node.doc]->JRNL) return;

    std::string timestamp = CurrentIsoTimestampUTC();

    xmlChar* path = xmlGetNodePath(node.node);
    xmlChar* escPath = xmlEncodeSpecialChars(node.doc, path);

    std::string xml =
        std::string("<Change Type=\"Modify\" TimeStamp=\"") + timestamp + "\">"
        "<XPathLoc Type=\"Self\">" +
        reinterpret_cast<const char*>(escPath) +
        "</XPathLoc>"
        "<Node Encoding=\"Base64\">" + base64_encode(oldXML) + "</Node>"
        "<Reversed TimeStamp=\"\" Value=\"false\"/>"
        "</Change>";

    if (escPath) xmlFree(escPath);
    if (path) xmlFree(path);

    Log(doc_map[node.doc]->JRNL, xml);
}

void Delete(XmlNode& node)
{
    if (!node.doc || !doc_map[node.doc]->JRNL) return;

    std::string oldXML = node.XML();

    std::string locType;
    std::string loc;

    if (node.node->prev) {
        locType = "SiblingAfter";
        loc = reinterpret_cast<const char*>(xmlGetNodePath(node.node->prev));
    }
    else if (node.node->next) {
        locType = "SiblingBefore";
        loc = reinterpret_cast<const char*>(xmlGetNodePath(node.node->next));
    }
    else {
        locType = "Child";
        loc = reinterpret_cast<const char*>(xmlGetNodePath(node.node->parent));
    }

    std::string xml =
        std::string("<Change Type=\"Deletion\"><TimeStamp/>") +
        "<XPathLoc Type=\"" + locType + "\">" + 
        reinterpret_cast<const char*>(xmlEncodeSpecialChars(node.doc, BAD_CAST loc.c_str())) + 
        "</XPathLoc>" +
        "<Node Encoding=\"Base64\">" + base64_encode(oldXML) + "</Node>" +
        "<Reversed TimeStamp=\"\" Value=\"false\"/></Change>";

    Log(doc_map[node.doc]->JRNL, xml);
}

}


void XmlJrnl::LogAdd(XmlNode& added) {}
void XmlJrnl::LogModify(XmlNode& node, const std::string& oldXML) {}
void XmlJrnl::LogDelete(XmlNode& node) {}

void XmlJrnl::Undo(XmlNode action_node) {}
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
