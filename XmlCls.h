/*
* XmlCls.h
*
* Lightweight C++ wrapper for libxml2 providing explicit-error, XPath-centric
* access to XML documents and nodes.
*/

#pragma once

#include <stdio.h>
#include <map>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>
#include <libxml/xmlerror.h>


/**
* @struct Error
* @brief Encapsulates error state for XmlCls operations.
*
* This structure is updated by XmlDoc and XmlNode methods instead of throwing
* exceptions. Callers are expected to inspect and handle errors explicitly.
*/
#include "Error.h"

extern std::map<xmlDocPtr, xmlXPathContextPtr> ctxt_map;


/**
* @class XmlDoc
* @brief Owns an XML document and its associated XPath context.
*
* XmlDoc is responsible for parsing XML from files or memory buffers and for
* managing the lifetime of the underlying libxml2 xmlDocPtr and XPath context.
*
* Design notes:
* - No exceptions are thrown
* - All failures are reported through the @ref err member
* - XPath contexts are cached per document
*/
class XmlDoc
{
private:
    /* data */
public:
    xmlDocPtr doc = nullptr; /* the resulting document tree */
    ErrorPtr err = nullptr;
    xmlXPathContextPtr ctxt = nullptr;

    XmlDoc() {}
   /**
    * @brief Construct an XmlDoc from a file on disk.
    * @param filename Path to the XML file.
    *
    * On failure, @ref err is populated and the document handle is null.
    */
    XmlDoc(const char *filename);

   /**
    * @brief Construct an XmlDoc from an in-memory buffer.
    * @param content Pointer to XML text.
    * @param length Size of the buffer in bytes.
    */
    XmlDoc(const char *content, int length);

   /**
    * @brief Destructor.
    *
    * Releases the underlying xmlDocPtr and any associated XPath context.
    */
    ~XmlDoc();

   /**
    * @brief Generate XML string representation.
    *
    * @return XML string representation of the document.
    */
    std::string XML() {
        xmlChar *xmlbuff;
        int buffersize;
        xmlDocDumpFormatMemory(doc, &xmlbuff, &buffersize, 1);
        std::string result = std::string((char *)xmlbuff, buffersize);
        xmlFree(xmlbuff);
        return result;
    }

   /**
    * @brief Evaluate an XPath expression relative to this document.
    * @tparam T Desired return type.
    * @param expr XPath expression.
    * @return Converted XPath result.
    *
    * Errors are reported via @ref err.
    */
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

   /**
    * @brief Construct an XmlNode from an existing xmlNodePtr.
    * @param node Pointer to the existing xmlNodePtr.
    */
    XmlNode(xmlNodePtr node) {
        if (!node) return;
        this->node = node;
        doc = node ? node->doc : nullptr;
    }
    ~XmlNode(){}
    
   /**
    * @brief Generate XML string representation.
    *
    * @return XML string representation of the node only.
    */
    std::string XML() {
        xmlBufferPtr buffer = xmlBufferCreate();
        xmlNodeDump(buffer, doc, node, 0, 1);
        std::string result = std::string((char *)buffer->content, buffer->use);
        xmlBufferFree(buffer);
        return result;
    }

   /**
    * @brief Evaluate an XPath expression relative to this node.
    * @tparam T Desired return type.
    * @param expr XPath expression.
    * @return Converted XPath result.
    *
    * Errors are reported via @ref err.
    */
    template <typename T> T XPath(std::string query);
};

xmlXPathContextPtr GetXPathContext(xmlDocPtr doc, ErrorPtr &err);
