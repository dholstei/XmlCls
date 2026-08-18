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
    : doc(xmlReadFile(filename, NULL, XML_PARSE_NOBLANKS))
{
    if (doc == NULL) { 
        xmlError e = *xmlGetLastError(); 
        err = new Error{lvl::ERR, e.message, filename}; 
        xmlResetLastError();
    }
    doc_map[doc] = this;
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
    JRNL = new XmlJrnl(*this, filename);
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
    JRNL = new XmlJrnl(*this, XML);
    jrnl_map[doc] = JRNL;
}

void XmlDoc::clear() {
    if (doc) {
        if (JRNL) { JRNL->Save(); delete JRNL; JRNL = nullptr; }
        auto it = doc_map.find(doc);
        if (it != doc_map.end()) doc_map.erase(it);
        // xmlFreeDoc(doc);
        // doc = nullptr;
    }
}

template <>
std::string XmlDoc::XPath<std::string>(std::string query)
{
    ctxt = GetXPathContext(doc, err);
    xmlXPathObjectPtr result = xmlXPathEvalExpression((const xmlChar *)query.c_str(), ctxt);
    if (result == nullptr) XML_ERROR(std::string, query);
    std::string ans;

    if (result->type == XPATH_STRING)
        ans = std::string((const char *)result->stringval);

    else if (result->type == XPATH_NODESET)
    {
        auto NL = result->nodesetval;
        if (NL->nodeNr != 1) {
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
    
    else if (result->type == XPATH_NODESET)
    {
        auto NL = result->nodesetval;
        if (NL->nodeNr != 1) {
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
    
    else if (result->type == XPATH_NODESET)
    {
        auto NL = result->nodesetval;
        if (NL->nodeNr != 1) {
            err = new Error{lvl::ERR, "No single node, not compatible for \"boolean\" type", query};
            xmlXPathFreeObject(result); return false; }
        result = xmlXPathNodeEval(NL->nodeTab[0], (const xmlChar*) "boolean(.)", ctxt);

        if (result->type != XPATH_BOOLEAN) {
            err = new Error{lvl::ERR, "Couldn't determine boolean for \"boolean\" type", query};
            xmlXPathFreeObject(result); return false; }
        else { xmlXPathFreeObject(result); return result->boolval; }
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
    std::string ans;

    if (result->type == XPATH_STRING)
        ans = std::string((const char *)result->stringval);

    else if (result->type == XPATH_NODESET)
    {
        auto NL = result->nodesetval;
        if (NL->nodeNr != 1) {
            err = new Error{lvl::ERR, "No single node, not compatible for \"std::string\" type", query};
            xmlXPathFreeObject(result); return ans; }
        result = xmlXPathNodeEval(NL->nodeTab[0], (const xmlChar*) "string(.)", ctxt);

        if (result->type != XPATH_STRING)
            err = new Error{lvl::ERR, "Couldn't determine intermediate string for \"std::string\" type", query};

        else ans = std::string((const char *)result->stringval);
    }
    
    else
        err = new Error{lvl::ERR, "Result type is not \"string\"", query};

    xmlXPathFreeObject(result);
    return ans;
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
    
    else if (result->type == XPATH_NODESET)
    {
        auto NL = result->nodesetval;
        if (NL->nodeNr != 1) {
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
    bool ans = false;

    if (result->type == XPATH_BOOLEAN)
        ans = result->boolval;
    
    else if (result->type == XPATH_NODESET)
    {
        auto NL = result->nodesetval;
        if (NL->nodeNr != 1) {
            err = new Error{lvl::ERR, "No single node, not compatible for \"boolean\" type", query};
            xmlXPathFreeObject(result); return false; }
        result = xmlXPathNodeEval(NL->nodeTab[0], (const xmlChar*) "boolean(.)", ctxt);

        if (result->type != XPATH_BOOLEAN) {
            err = new Error{lvl::ERR, "Couldn't determine boolean for \"boolean\" type", query};
            xmlXPathFreeObject(result); return false; }
        else
            ans = result->boolval;
    }

    else
        err = new Error{lvl::ERR, "Result type is not \"boolean!\"", query};
        
        xmlXPathFreeObject(result);
        return ans;
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

    if (JRNL) JRNL->LogModify(*this, oldNode ? this->XML() : std::string());

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

    XmlNode result(added);

    if (JRNL)
        JRNL->LogAdd(result);

    return result;
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

    XmlNode result(added);

    if (JRNL)
        JRNL->LogAdd(result);

    return result;
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

    XmlNode result(added);

    if (JRNL)
        JRNL->LogAdd(result);

    return result;
}

void XmlNode::Delete()
{
    if (!node) return;

    if (JRNL)
        JRNL->LogDelete(*this);

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
        if (!(N).node || (N).doc != source_doc.doc) {                  \
            err = new Error{ lvl::ERR, "XmlNode does not belong to this journal's source DOM", (N).node ? (N).GetPath() : std::string() };                                                          \
            return;                                                     \
        }                                                               \
    } while (0)

XmlJrnl::XmlJrnl(XmlDoc& source, const char* filename): XmlDoc(filename), source_doc(source) {
    RefreshActiveRelease();
    if (err) return;

    BuildJIDMap();
}

XmlJrnl::XmlJrnl(XmlDoc& source, const std::string content) : XmlDoc(content), source_doc(source){
    RefreshActiveRelease();
    if (err) return;

    BuildJIDMap();
}

void XmlJrnl::LogAdd(XmlNode& added) {
    JRNL_CHECK_NODE(added);
    if (!active_release.node || !added.node) return;

    std::string change =
        "\n<Change Type=\"Add\" TimeStamp=\"" + CurrentIsoTimestampUTC() + "\">"
        "<XPathLoc>" + added.GetPath() + "</XPathLoc>\n</Change>\n";

    active_release.AddChild(change);
}

void XmlJrnl::LogModify(XmlNode& node, const std::string& oldXML)
{
    JRNL_CHECK_NODE(node);

    if (!active_release.node || !node.node)
        return;

    bool had_jid = node.XPath<bool>("./@JID");

    std::string jid = JID(node.node);
    if (err || jid.empty())
        return;

    const std::string savedXML =
        had_jid ? oldXML : node.XML();

    std::string change =
        "\n<Change Type=\"Modify\" TimeStamp=\"" +
        CurrentIsoTimestampUTC() + "\""
        " JID=\"" + jid + "\">"
        "<Node Encoding=\"Base64\">" +
        base64_encode(savedXML) +
        "</Node>"
        "<Reversed TimeStamp=\"\" Value=\"false\"/>"
        "\n</Change>\n";

    active_release.AddChild(change);
}
void XmlJrnl::LogDelete(XmlNode& node) {
    JRNL_CHECK_NODE(node);
    if (!active_release.node || !node.node) return;

    std::string change =
        "\n<Change Type=\"Deletion\" TimeStamp=\"" +
        CurrentIsoTimestampUTC() + "\">"
        "<XPathLoc>" + node.GetPath() + "</XPathLoc>\n</Change>\n";

    active_release.AddChild(change);
}

void XmlJrnl::Undo() {
    if (!active_release.node) {
        err = new Error{ lvl::ERR, "Cannot undo: journal has no active release", "" };
        return;
    }

    auto actions = active_release.XPath<std::vector<XmlNode>>( "./Change[Reversed/@Value='false']" );

    if (active_release.err) {
        err = active_release.err;
        return;
    }

    Undo(actions);
}
void XmlJrnl::Undo(XmlNode action_node)
{
    if (!action_node.node) {
        err = new Error{ lvl::ERR, "Cannot undo: invalid journal action node", "" };
        return;
    }

    const std::string journal_path = action_node.GetPath();

    /*
     * Already reversed?
     */
    if (action_node.XPath<bool>( "boolean(./Reversed[@Value='true'])"))
        return;

    /*
     * Undo is stack-oriented.  A transaction cannot be reversed while
     * a newer transaction in the same Release remains active.
     */
    if (action_node.XPath<bool>( "boolean(following-sibling::Change" "[Reversed/@Value='false'])"))
    {
        err = new Error{ lvl::ERR, "Cannot undo transaction: incompatible later changes remain", journal_path };
        return;
    }

    /*
     * Locate the XmlDoc associated with this journal.
     *
     * jrnl_map maps source xmlDocPtr -> this XmlJrnl, while doc_map gives
     * us the XmlDoc wrapper needed for XPath evaluation.
     */
    XmlDoc* target_doc = nullptr;

    for (auto& [docptr, journal] : jrnl_map) {
        if (journal != this) continue;

        auto it = doc_map.find(docptr);
        if (it != doc_map.end()) {
            target_doc = it->second;
            break;
        }
    }

    if (!target_doc) {
        err = new Error{ lvl::ERR, "Cannot undo transaction: source document is not available", journal_path };
        return;
    }

    const std::string type = action_node.XPath<std::string>("@Type");

    const std::string xpath = action_node.XPath<std::string>("./XPathLoc");

    const std::string loc_type = action_node.XPath<std::string>("./XPathLoc/@Type");

    if (type.empty() || xpath.empty()) {
        err = new Error{ lvl::ERR, "Cannot undo transaction: journal entry is incomplete", journal_path };
        return;
    }

    /*
     * ------------------------------------------------------------
     * Undo Add
     *
     * Original:
     *      <XPathLoc Type="Self">/Root/B</XPathLoc>
     *
     * Undo:
     *      remove exactly that node.
     * ------------------------------------------------------------
     */
    if (type == "Add")
    {
        auto nodes = target_doc->XPath<std::vector<XmlNode>>(xpath);

        if (nodes.size() != 1) {
            err = new Error{ lvl::ERR, "Cannot undo Add: target node no longer uniquely exists", journal_path };
            return;
        }

        xmlNodePtr doomed = nodes[0].node;

        xmlUnlinkNode(doomed);
        xmlFreeNode(doomed);
    }

    /*
     * ------------------------------------------------------------
     * Undo Modify
     *
     * Node contains the XML representation before the modification.
     * Replace the current node with that saved node.
     * ------------------------------------------------------------
     */
    else if (type == "Modify")
    {
        auto nodes = target_doc->XPath<std::vector<XmlNode>>(xpath);

        if (nodes.size() != 1) {
            err = new Error{ lvl::ERR, "Cannot undo Modify: target node no longer uniquely exists", journal_path };
            return;
        }

        const std::string encoded = action_node.XPath<std::string>("string(./Node)");

        if (encoded.empty()) {
            err = new Error{ lvl::ERR, "Cannot undo Modify: journal contains no previous node state", journal_path };
            return;
        }

        const std::string oldXML = base64_decode(encoded);

        XmlNode& current = nodes[0];

        xmlNodePtr restored = XmlNodeFromString(oldXML, current.doc, err);

        if (!restored) {
            if (!err)
                err = new Error{ lvl::ERR, "Cannot undo Modify: saved XML cannot be restored", journal_path };
            else
                err->data = journal_path;

            return;
        }

        xmlNodePtr old = current.node;

        if (!xmlReplaceNode(old, restored)) {
            xmlFreeNode(restored);

            err = new Error{ lvl::ERR, "Cannot undo Modify: node replacement failed", journal_path };
            return;
        }

        xmlFreeNode(old);
    }

    /*
     * ------------------------------------------------------------
     * Undo Deletion
     *
     * XPathLoc refers to an surviving anchor:
     *
     *   SiblingAfter  -> insert after anchor
     *   SiblingBefore -> insert before anchor
     *   Child         -> append beneath parent
     * ------------------------------------------------------------
     */
    else if (type == "Deletion")
    {
        auto anchors = target_doc->XPath<std::vector<XmlNode>>(xpath);

        if (anchors.size() != 1) {
            err = new Error{ lvl::ERR, "Cannot undo Deletion: insertion anchor no longer uniquely exists", journal_path };
            return;
        }

        const std::string encoded =
            action_node.XPath<std::string>("string(./Node)");

        if (encoded.empty()) {
            err = new Error{ lvl::ERR, "Cannot undo Deletion: journal contains no deleted node", journal_path };
            return;
        }

        const std::string oldXML = base64_decode(encoded);

        XmlNode& anchor = anchors[0];

        xmlNodePtr restored = XmlNodeFromString(oldXML, anchor.doc, err);

        if (!restored) {
            if (!err)
                err = new Error{ lvl::ERR, "Cannot undo Deletion: saved XML cannot be restored", journal_path };
            else
                err->data = journal_path;

            return;
        }

        xmlNodePtr inserted = nullptr;

        if (loc_type == "SiblingAfter")
            inserted = xmlAddNextSibling(anchor.node, restored);

        else if (loc_type == "SiblingBefore")
            inserted = xmlAddPrevSibling(anchor.node, restored);

        else if (loc_type == "Child")
            inserted = xmlAddChild(anchor.node, restored);

        else {
            xmlFreeNode(restored);

            err = new Error{ lvl::ERR, "Cannot undo Deletion: unknown XPathLoc Type", journal_path };
            return;
        }

        if (!inserted) {
            xmlFreeNode(restored);

            err = new Error{ lvl::ERR, "Cannot undo Deletion: node could not be reinserted", journal_path };
            return;
        }
    }

    else {
        err = new Error{
            lvl::ERR,
            "Cannot undo transaction: unknown Change Type \"" + type + "\"",
            journal_path
        };
        return;
    }

    /*
     * Only stamp the journal after the DOM mutation succeeds.
     *
     * We modify the journal node directly so this bookkeeping operation
     * cannot itself become a journal transaction.
     */
    auto reversed =
        action_node.XPath<std::vector<XmlNode>>("./Reversed");

    if (reversed.size() != 1) {
        err = new Error{
            lvl::ERR,
            "Transaction was reversed but journal has invalid Reversed node",
            journal_path
        };
        return;
    }

    xmlSetProp( reversed[0].node, BAD_CAST "Value", BAD_CAST "true" );

    const std::string timestamp = CurrentIsoTimestampUTC();

    xmlSetProp( reversed[0].node, BAD_CAST "TimeStamp", BAD_CAST timestamp.c_str() );
}
void XmlJrnl::Undo(std::vector<XmlNode> action_nodes) {
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

    auto nl = source_doc.XPath<std::vector<XmlNode>>("//*/@JID");

    if (source_doc.err)
        { err = source_doc.err; return; }

    for (auto n : nl) {
        std::string jid = n.XPath<std::string>(".");

        auto [it, inserted] = jid_map.emplace(jid, n.node->parent);

        if (!inserted) { 
            err = new Error{ lvl::ERR, "Duplicate JID \"" + jid + "\"", n.GetPath() };
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

std::string XmlJrnl::JID(xmlNodePtr node)
{
    if (!node || node->doc != source_doc.doc) {
        err = new Error{ lvl::ERR, "Cannot assign JID: node does not belong to this journal's source DOM", node ? XmlNode(node).GetPath() : std::string() };
        return {};
    }

    xmlChar* value = xmlGetProp(node, BAD_CAST "JID");

    if (value) {
        std::string jid(reinterpret_cast<const char*>(value));
        xmlFree(value);

        auto it = jid_map.find(jid);

        if (it == jid_map.end())
            jid_map[jid] = node;

        else if (it->second != node) {
            err = new Error{ lvl::ERR, "Duplicate JID \"" + jid + "\"", XmlNode(node).GetPath() };
            return {};
        }

        return jid;
    }

    std::string jid = JID();

    if (!xmlSetProp(node, BAD_CAST "JID", BAD_CAST jid.c_str())) {
        err = new Error{ lvl::ERR, "Unable to assign JID", XmlNode(node).GetPath() };
        return {};
    }

    jid_map[jid] = node;

    return jid;
}
