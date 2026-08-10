/*
* XmlCls.h
*
* Lightweight C++ wrapper for libxml2 providing explicit-error, XPath-centric
* access to XML documents and nodes.
*/

#include <stdio.h>
#include <map>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>
#include <libxml/xmlerror.h>
#include <mutex>

#include "string.h"

/**
* @struct Error
* @brief Encapsulates error state for XmlCls operations.
*
* This structure is updated by XmlDoc and XmlNode methods instead of throwing
* exceptions. Callers are expected to inspect and handle errors explicitly.
*/
#include "Error.h"
typedef Error* ErrorPtr;
#include "base64.h"

class XmlDoc;    // Forward declaration for doc_map
class XmlJrnl;
class XmlNode;
extern std::mutex doc_map_mtx;
extern std::map<xmlDocPtr, XmlDoc*> doc_map;
extern std::map<xmlDocPtr, XmlJrnl*> jrnl_map;

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

public:
    ErrorPtr err = nullptr;
    xmlXPathContextPtr ctxt = nullptr;
    XmlJrnl* JRNL = nullptr;
    mutable std::recursive_mutex mtx;

    XmlDoc() {}
    XmlDoc(const XmlDoc&) = delete;
    XmlDoc& operator=(const XmlDoc&) = delete;

    XmlDoc(XmlDoc&& other) noexcept {
        doc = other.doc;
        other.doc = nullptr;
        doc_map[doc] = this;
    }

    XmlDoc(xmlDocPtr doc) noexcept {
        this->doc = doc;
        doc_map[doc] = this;
    }

    XmlDoc& operator=(XmlDoc&& other) noexcept {
        if (this != &other) {
            clear();

            doc = other.doc;
            other.doc = nullptr;
        }
        return *this;
    }

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
    XmlDoc(const std::string content);

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
    std::string XML() const {
        xmlChar *xmlbuff;
        int buffersize;
        xmlDocDumpFormatMemory(doc, &xmlbuff, &buffersize, 1);
        std::string result = std::string((char *)xmlbuff, buffersize);
        xmlFree(xmlbuff);
        return result;
    }
    std::string to_string() const { return XML(); }
    explicit operator std::string() const { return XML(); }
    std::ostream& operator<<(std::ostream& os) { return os << XML(); }

    /**
     * @brief Save document to given filename.
     * @param filename Path to save.
     * @return True on success.
     */
    void Save(const char* filename);

    /**
     * @brief Save document to its last used path.
     * @return True on success.
     */
    void Save();
    
   /**
    * @brief Evaluate an XPath expression relative to this document.
    * @tparam T Desired return type.
    * @param expr XPath expression.
    * @return Converted XPath result.
    *
    * Errors are reported via @ref err.
    */
    template <typename T> T XPath(std::string query);

    void OpenJournal(const char* filename);

    void CreateJournal(const char* filename, std::string XML = "");

private:
    xmlDocPtr doc = nullptr;

    void clear() ;
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
    XmlJrnl* JRNL = nullptr;

    XmlNode() {}

   /**
    * @brief Construct an XmlNode from an existing xmlNodePtr.
    * @param node Pointer to the existing xmlNodePtr.
    */
    XmlNode(xmlNodePtr node) {
        if (!node) { err = new Error{lvl::ERR, "Invalid/NULL node pointer", ""}; return; }
        this->node = node;
        doc = node ? node->doc : nullptr;
        if (doc) {
            auto it = jrnl_map.find(doc);
            if (it != jrnl_map.end()) JRNL = it->second;
        }
        else { err = new Error{lvl::ERR, "Node has no associated document", ""}; }
    }
    ~XmlNode(){}
    
   /**
    * @brief Generate XML string representation.
    *
    * @return XML string representation of the node only.
    */
    std::string XML() const {
        xmlBufferPtr buffer = xmlBufferCreate();
        xmlNodeDump(buffer, doc, node, 0, 1);
        std::string result = std::string((char *)buffer->content, buffer->use);
        xmlBufferFree(buffer);
        return result;
    }
    std::string to_string() const { return XML(); }
    explicit operator std::string() const { return XML(); }
    std::ostream& operator<<(std::ostream& os) { return os << XML(); }

    /**
     * @brief Replaces this node's content with parsed XML.
     * @param XML New XML string.
     */
    void parse(std::string XML);

    /**
     * @brief Parse XML text and append it as a child of this node.
     * @param XmlStr XML text for the node to insert.
     * @return Wrapper for the inserted node, or an empty XmlNode on error.
     */
    XmlNode AddChild(std::string XmlStr);
    XmlNode AddChild(std::vector<std::string> XmlStrs) {
        XmlNode lastAdded;
        for (const auto& str : XmlStrs) {
            lastAdded = AddChild(str);
            if (lastAdded.err) return XmlNode();
        }
        return lastAdded;
    }

    /**
     * @brief Parse XML text and insert it before this node as a sibling.
     * @param XmlStr XML text for the node to insert.
     * @return Wrapper for the inserted node, or an empty XmlNode on error.
     */
    XmlNode AddBefore(std::string XmlStr);
    XmlNode AddBefore(std::vector<std::string> XmlStrs) {
        XmlNode lastAdded;
        for (const auto& str : XmlStrs) {
            lastAdded = AddBefore(str);
            if (lastAdded.err) return XmlNode();
        }
        return lastAdded;
    }

    /**
     * @brief Parse XML text and insert it after this node as a sibling.
     * @param XmlStr XML text for the node to insert.
     * @return Wrapper for the inserted node, or an empty XmlNode on error.
     */
    XmlNode AddAfter(std::string XmlStr);
    XmlNode AddAfter(std::vector<std::string> XmlStrs) {
        XmlNode lastAdded;
        for (const auto& str : XmlStrs) {
            lastAdded = AddAfter(str);
            if (lastAdded.err) return XmlNode();
        }
        return lastAdded;
    }

    /**
     * @brief Remove this node from the XML tree and invalidate the wrapper.
     */
    void Delete();

    std::string GetPath() const {
        if (!node) return {};

        xmlChar* path = xmlGetNodePath(node);
        if (!path) return {};

        std::string result(reinterpret_cast<const char*>(path));
        xmlFree(path);
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

class XmlJrnl : public XmlDoc
{
public:
    std::vector<int> rel_no;      // e.g. {0, 2, 1} => Release 0.2.1
    XmlNode active_release;       // deepest open Release node

    XmlJrnl(const char *filename);
    XmlJrnl(const std::string content);

    void OpenJournal(const char*) = delete;
    void CreateJournal(const char*, std::string) = delete;

    void LogAdd(XmlNode& added);
    void LogModify(XmlNode& node, const std::string& oldXML);
    void LogDelete(XmlNode& node);

    void Undo(XmlNode action_node);
    void RefreshActiveRelease();
    // std::string ReleaseString() const;

private:
    XmlNode FindActiveRelease(XmlNode start, std::vector<int>& path);
};
