#include "XmlCls.h"

std::map<xmlDocPtr, xmlXPathContextPtr> ctxt_map;
#define XML_ERROR(T, data) \
    do { \
        xmlError e = *xmlGetLastError(); \
        err = new Error{lvl::ERR, e.message, data}; \
        xmlResetLastError(); return T(); \
    } while(0)

#define CHK_CONTEXT(T, ctxt) \
    do { \
        if (!ctxt) { \
            ctxt = GetXPathContext(doc, err); \
            if (!ctxt) return T(); \
            ctxt_map[doc] = ctxt; \
        } \
    } while(0)

XmlDoc::XmlDoc(const char *filename)
{
    doc = xmlReadFile(filename, NULL, 0);
    if (doc == NULL) { 
        xmlError e = *xmlGetLastError(); 
        err = new Error{lvl::ERR, e.message, filename}; 
        xmlResetLastError();
    }
}

XmlDoc::XmlDoc(const char *content, int length)
{
    doc = xmlReadMemory(content, length, "noname.xml", NULL, 0);
    if (doc == NULL)
    {
        xmlError e = *xmlGetLastError();
        err = new Error{lvl::ERR, e.message, std::string(e.str1)};
        xmlResetLastError();
    }
}

XmlDoc::~XmlDoc()
{
    // if (doc) xmlFreeDoc(doc);
    doc = nullptr;
}

template <>
std::string XmlDoc::XPath<std::string>(std::string query)
{
    CHK_CONTEXT(std::string, ctxt);
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
    CHK_CONTEXT(double, ctxt);
    xmlXPathObjectPtr result = xmlXPathEvalExpression((const xmlChar *)query.c_str(), ctxt);
    if (result == nullptr) XML_ERROR(double, query);

    if (result->type == XPATH_NUMBER)
    {
        double ans = result->floatval;
        xmlXPathFreeObject(result);
        return ans;
    }
    else
    {
        xmlXPathFreeObject(result);
        err = new Error{lvl::ERR, "Result type is not \"number\"!", query};
    }
    return 0.0;
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
    CHK_CONTEXT(bool, ctxt);
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
    std::vector<XmlNode>* NL = new std::vector<XmlNode>();
    CHK_CONTEXT(std::vector<XmlNode>, ctxt);
    xmlXPathObjectPtr result = xmlXPathEvalExpression((const xmlChar *)query.c_str(), ctxt);
    if (result == nullptr) XML_ERROR(std::vector<XmlNode>, query);

    if (result->type == XPATH_NODESET)
    {
        auto ans = result->nodesetval;
        NL->reserve(ans->nodeNr);
        for (int i = 0; i < ans->nodeNr; i++) NL->emplace_back(XmlNode(ans->nodeTab[i]));
        xmlXPathFreeObject(result);
        return *NL;
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

    if ((xpathCtx = ctxt_map[doc]) != nullptr)
        return xpathCtx;

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
    CHK_CONTEXT(std::string, ctxt);
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
    CHK_CONTEXT(double, ctxt);
    xmlXPathObjectPtr result = xmlXPathNodeEval(node, (const xmlChar *)query.c_str(), ctxt);
    if (result == nullptr) XML_ERROR(double, query);

    if (result->type == XPATH_NUMBER)
    {
        double ans = result->floatval;
        xmlXPathFreeObject(result);
        return ans;
    }
    else
    {
        xmlXPathFreeObject(result);
        err = new Error{lvl::ERR, "Result type is not \"number\"!", query};
    }
    return 0.0;
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
    CHK_CONTEXT(bool, ctxt);
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
    std::vector<XmlNode>* NL = new std::vector<XmlNode>();
    CHK_CONTEXT(std::vector<XmlNode>, ctxt);
    xmlXPathObjectPtr result = xmlXPathEvalExpression((const xmlChar *)query.c_str(), ctxt);
    if (result == nullptr) XML_ERROR(std::vector<XmlNode>, query);

    if (result->type == XPATH_NODESET)
    {
        auto ans = result->nodesetval;
        NL->reserve(ans->nodeNr);
        for (int i = 0; i < ans->nodeNr; i++) NL->emplace_back(XmlNode(ans->nodeTab[i]));
        xmlXPathFreeObject(result);
        return *NL;
    }
    else
    {
        xmlXPathFreeObject(result);
        err = new Error{lvl::ERR, "Result type is not \"nodelist/resultset\"!", query};
    }
    return std::vector<XmlNode>();
}
