
#include <stdio.h>
#include <map>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>
#include <libxml/xmlerror.h>

#include "Error.h"

extern std::map<xmlDocPtr, xmlXPathContextPtr> ctxt_map;

class XmlDoc
{
private:
    /* data */
public:
    xmlDocPtr doc = nullptr; /* the resulting document tree */
    ErrorPtr err = nullptr;
    xmlXPathContextPtr ctxt = nullptr;

    XmlDoc() {}
    XmlDoc(const char *filename);
    XmlDoc(const char *content, int length);
    ~XmlDoc();

    std::string XML() {
        xmlChar *xmlbuff;
        int buffersize;
        xmlDocDumpFormatMemory(doc, &xmlbuff, &buffersize, 1);
        std::string result = std::string((char *)xmlbuff, buffersize);
        xmlFree(xmlbuff);
        return result;
    }

    // XPath template definition
    template <typename T> T XPath(std::string query);
};


class XmlNode
{
private:
    /* data */
public:
    xmlDocPtr doc = nullptr; /* the parent document tree */
    xmlNodePtr node = nullptr;
    ErrorPtr err = nullptr;
    xmlXPathContextPtr ctxt = nullptr;

    XmlNode() {}
    XmlNode(xmlNodePtr node) {
        if (!node) return;
        this->node = node;
        doc = node ? node->doc : nullptr;
    }
    ~XmlNode(){}
    
    std::string XML() {
        xmlBufferPtr buffer = xmlBufferCreate();
        xmlNodeDump(buffer, doc, node, 0, 1);
        std::string result = std::string((char *)buffer->content, buffer->use);
        xmlBufferFree(buffer);
        return result;
    }

    // XPath template definition
    template <typename T> T XPath(std::string query);
};

xmlXPathContextPtr GetXPathContext(xmlDocPtr doc, ErrorPtr &err);
