/**
 * @file XmlCls.h
 * @brief XPath-centric C++ wrapper for libxml2 with optional mutation journaling.
 *
 * XmlCls provides lightweight XmlDoc and XmlNode wrappers around libxml2.
 * Structural selection is expressed primarily with XPath while mutations are
 * performed through the wrapper methods.  Errors are reported explicitly
 * through Error pointers rather than exceptions.
 *
 * When journaling is enabled, XmlJrnl assigns persistent JIDs to logical XML
 * nodes and records Add, Modify, and Deletion actions that can subsequently be
 * reversed.  JIDs remain stable even when a mutation replaces the underlying
 * xmlNodePtr.
 */

#include <stdio.h>
#include <map>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>
#include <libxml/xmlerror.h>
#include <ctime>
#include <random>
#include <cstdint>

#include "string.h"

/**
 * @struct Error
 * @brief Explicit error/status state used by XmlCls operations.
 *
 * XmlCls does not throw exceptions for normal library failures.  Methods set an
 * Error pointer which callers are expected to inspect.  Journal conflicts that
 * are valid consequences of prior transactions are reported at lvl::INFO rather
 * than as implementation errors.
 */
#include "Error.h"
typedef Error* ErrorPtr;
#include "base64.h"

class XmlDoc;
class XmlJrnl;
class XmlNode;

static std::string CurrentIsoTimestampUTC()
{
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&now, &tm);

    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}

/**
 * @class XmlDoc
 * @brief Canonical wrapper for one libxml2 document.
 *
 * Each XmlDoc is permanently associated with one xmlDocPtr.  Copy and move
 * operations are disabled so that document identity cannot change during the
 * wrapper lifetime.  The canonical XmlDoc pointer is stored in xmlDoc::_private,
 * allowing transient XmlNode wrappers to recover their owning document and
 * journal without global lookup tables.
 *
 * XmlDoc caches an XPath context for the document and optionally owns an
 * attached XmlJrnl.  Failures are reported through @ref err.
 *
 * @note The current implementation frees the cached XPath context in clear().
 *       The underlying xmlDocPtr is intentionally not freed there at present.
 */
class XmlDoc
{

public:
    ErrorPtr err = nullptr;              ///< Last error/status reported by this wrapper.
    xmlXPathContextPtr ctxt = nullptr;   ///< Cached XPath context for this DOM.
    XmlJrnl* JRNL = nullptr;             ///< Optional mutation journal attached to this DOM.

    xmlDocPtr const doc;                  ///< Immutable identity of the wrapped libxml2 DOM.

    XmlDoc() : doc(nullptr) {}
    XmlDoc(const XmlDoc&) = delete;
    XmlDoc& operator=(const XmlDoc&) = delete;

    XmlDoc(xmlDocPtr doc) noexcept
        : doc(doc) {
        doc->_private = this;
    }

    XmlDoc(XmlDoc&&) = delete;
    XmlDoc& operator=(XmlDoc&&) = delete;

   /**
    * @brief Construct an XmlDoc from a file on disk.
    * @param filename Path to the XML file.
    *
    * On failure, @ref err is populated and the document handle is null.
    */
    XmlDoc(const char *filename);

   /**
    * @brief Construct an XmlDoc from an XML string.
    * @param content Complete XML document text.
    *
    * On success, xmlDoc::_private is set to this canonical XmlDoc wrapper.
    */
    XmlDoc(const std::string content);

   /**
    * @brief Destroy the wrapper and release wrapper-owned resources.
    *
    * The cached XPath context and attached journal are released.  The current
    * implementation intentionally does not call xmlFreeDoc() from clear().
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
     * @brief Save the document to a filename.
     * @param filename Destination path.
     *
     * On success the libxml2 document URL is updated so that a later Save()
     * without arguments writes to the same location.
     */
    void Save(const char* filename);

    /**
     * @brief Save the document using its current libxml2 document URL.
     *
     * If the document has no URL, the method returns without writing.
     */
    void Save();
    
   /**
    * @brief Evaluate an XPath expression relative to the document.
    * @tparam T Desired C++ result type.
    * @param query XPath expression.
    * @return Result converted to T.
    *
    * Explicit specializations provide std::string, double, int, bool, and
    * std::vector<XmlNode> results.  Node-set results requested as scalar types
    * are converted from the selected node value when exactly one node exists.
    * Errors are reported through @ref err.
    */
    template <typename T> T XPath(std::string query);

    /**
     * @brief Attach an existing journal file to this document.
     * @param filename Journal XML file.
     */
    void OpenJournal(const char* filename);

    /**
     * @brief Create and attach a journal to this document.
     * @param filename Destination journal filename.
     * @param XML Optional journal seed XML.  If empty, a default open release
     *            hierarchy is created.
     */
    void CreateJournal(const char* filename, std::string XML = "");

    /**
     * @brief Return the cached XPath context, creating it on first use.
     * @return XPath context associated with this document.
     *
     * The current implementation caches one context per XmlDoc; callers must
     * not assume concurrent evaluations against that same context are safe.
     */
    xmlXPathContextPtr XPathContext();

private:

    void clear() ;
};

/**
 * @class XmlNode
 * @brief Lightweight, transient wrapper around an xmlNodePtr.
 *
 * XmlNode does not own the underlying DOM node.  Wrappers may be constructed
 * freely from xmlNodePtr values and often exist only long enough to perform an
 * XPath query or mutation.  The owning XmlDoc is recovered from
 * xmlNodePtr::doc->_private, and any attached XmlJrnl is inherited from it.
 */
class XmlNode
{
private:
    /* data */
public:
    xmlDocPtr doc = nullptr;             ///< Parent libxml2 document.
    xmlNodePtr node = nullptr;           ///< Wrapped libxml2 node.
    ErrorPtr err = nullptr;              ///< Last error/status reported by this wrapper.
    xmlXPathContextPtr ctxt = nullptr;   ///< Borrowed document XPath context.
    XmlJrnl* JRNL = nullptr;             ///< Journal attached to the owning XmlDoc, if any.

    XmlNode() {}

   /**
    * @brief Construct a transient wrapper for an existing libxml2 node.
    * @param node Existing node pointer.
    *
    * The constructor recovers the canonical XmlDoc through doc->_private and
    * inherits its journal association.
    */
    XmlNode(xmlNodePtr node) {
        if (!node) { err = new Error{lvl::ERR, "Invalid/NULL node pointer", ""}; return; }
        this->node = node;
        doc = node ? node->doc : nullptr;

        XmlDoc* owner =  doc ? static_cast<XmlDoc*>(doc->_private) : nullptr;

        if (owner) JRNL = owner->JRNL;
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
     * @brief Replace this logical node with XML parsed from a string.
     * @param XML Replacement node XML.
     *
     * If journaling is enabled, the existing logical node is assigned a JID,
     * the prior XML is recorded as a Modify action, and the same JID is
     * propagated to the replacement xmlNodePtr.
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
     * @brief Remove this node from the XML tree and invalidate this wrapper.
     *
     * With journaling enabled, the parent and all element children of the
     * parent are first assigned JIDs, the Deletion action is recorded, and the
     * deleted node's JID remains reserved in the journal map with a null live
     * node pointer.
     */
    void Delete();

    /**
     * @brief Return libxml2's structural XPath for the current node.
     * @return Path from xmlGetNodePath(), or an empty string for a null node.
     *
     * This path is useful for diagnostics but is not used as persistent journal
     * identity; JID provides that role.
     */
    std::string GetPath() const {
        if (!node) return {};

        xmlChar* path = xmlGetNodePath(node);
        if (!path) return {};

        std::string result(reinterpret_cast<const char*>(path));
        xmlFree(path);
        return result;
    }

    /**
     * @brief Return this node's JID, creating one when journaling requires it.
     * @return Existing or newly assigned journal identity.
     *
     * A newly created JID is registered in XmlJrnl::jid_map.
     */
    std::string JID();

    /**
     * @brief Propagate an existing logical JID to this physical node.
     * @param jid Journal identity to assign.
     *
     * This overload deliberately rebinds the JID map to the current xmlNodePtr;
     * it is used when parse() or Undo() replaces the physical node while
     * preserving logical identity.
     */
    void JID(std::string jid);

/**
    * @brief Evaluate an XPath expression relative to this node.
    * @tparam T Desired C++ result type.
    * @param query XPath expression.
    * @return Result converted to T.
    *
    * The XPath context is borrowed from the canonical XmlDoc.  Structural
    * navigation is intentionally expressed through XPath so that element
    * semantics are not obscured by text, CDATA, or comment nodes.
    */
    template <typename T> T XPath(std::string query);
};

/**
 * @class XmlJrnl
 * @brief Mutation journal permanently associated with one canonical XmlDoc.
 *
 * XmlJrnl is itself an XmlDoc containing a release-oriented journal DOM, while
 * @ref source_doc identifies the separate source DOM being tracked.  The
 * source association is a reference and therefore cannot be rebound.
 *
 * Logical source nodes are identified by persistent hexadecimal JIDs.
 * @ref jid_map maps each reserved JID to its current live xmlNodePtr; a null
 * pointer represents a deleted logical node whose identity remains reserved by
 * retained journal history.
 *
 * Changes are recorded beneath the deepest open Release.  Action records use
 * JIDs and structural relationships (Parent, Before, After) rather than
 * xmlGetNodePath() as durable identity.  Undo conflicts caused by legitimate
 * intervening journal history are reported as lvl::INFO with a "Conflict"
 * message, leaving policy and resolution to the consuming application.
 */
class XmlJrnl : public XmlDoc
{
public:
    /// Numeric path of the active release; e.g. {0,2,1} => Release 0.2.1.
    std::vector<int> rel_no;

    /// Deepest currently open Release node to which changes are appended.
    XmlNode active_release;

    XmlDoc& source_doc;  ///< Canonical source DOM permanently attached to this journal.

    /// Reserved JID -> current live source node; nullptr means logically deleted.
    std::map<std::string, xmlNodePtr> jid_map;

    /**
     * @brief Open an existing journal for a canonical source document.
     * @param source Source XmlDoc whose mutations this journal represents.
     * @param filename Journal XML file.
     *
     * The constructor resolves the active release and indexes all existing JIDs
     * present in the source DOM.
     */
    XmlJrnl(XmlDoc& source, const char *filename);

    /**
     * @brief Construct a journal from XML text for a canonical source document.
     * @param source Source XmlDoc whose mutations this journal represents.
     * @param content Complete journal XML document.
     *
     * The constructor resolves the active release and indexes all existing JIDs
     * present in the source DOM.
     */
    XmlJrnl(XmlDoc& source, const std::string content);

    /**
     * @brief Journals cannot themselves have journals.
     *
     * Disabled to prevent recursively journaling an XmlJrnl.
     */
    void OpenJournal(const char*) = delete;

    /**
     * @brief Journals cannot themselves create journals.
     *
     * Disabled to prevent recursively journaling an XmlJrnl.
     */
    void CreateJournal(const char*, std::string) = delete;

    /**
     * @brief Record addition of a source node.
     * @param added Newly inserted node.
     *
     * Delegates action-specific recording to ActionAdd.  The Change record
     * contains the added node JID and the JID of its parent.
     */
    void LogAdd(XmlNode& added);

    /**
     * @brief Record replacement or modification of a source node.
     * @param node Logical node being modified.
     * @param oldXML Serialized state before replacement.
     *
     * Delegates to ActionModify, which records the node JID, parent JID, and
     * Base64-encoded prior XML required for reversal.
     */
    void LogModify(XmlNode& node, const std::string& oldXML);

    /**
     * @brief Record deletion of a source node before it is unlinked.
     * @param node Node immediately before removal.
     *
     * Delegates to ActionDelete, which records the deleted JID, parent JID,
     * optional immediate element sibling JIDs (Before/After), and Base64-
     * encoded node XML.  These relationships define the structural slot needed
     * for Undo().
     */
    void LogDelete(XmlNode& node);

    /**
     * @brief Undo the most recent unreversed Change in the active release.
     */
    void Undo();

    /**
     * @brief Undo one recorded Change.
     * @param action_node Journal Change node.
     *
     * Dispatches to ActionModify, ActionDelete, or ActionAdd according to
     * its Type attribute.  Already reversed actions return without further work.
     */
    void Undo(XmlNode action_node);

    /**
     * @brief Undo a sequence of actions in reverse order.
     * @param action_nodes Journal Change nodes in journal/document order.
     */
    void Undo(std::vector<XmlNode> action_nodes);


    /**
     * @brief Recalculate the currently active Release branch.
     *
     * Searches the journal for the deepest nested Release whose Close
     * attribute is empty, updates @ref active_release to that node, and
     * rebuilds @ref rel_no with its release-number path.
     */
    void RefreshActiveRelease();

    /**
     * @brief Rebuild the live JID index from JID attributes in source_doc.
     *
     * Duplicate JIDs are reported as errors.  XPath is used to obtain all JID
     * attributes without manual DOM traversal.
     */
    void BuildJIDMap();

    /**
     * @brief Generate a JID unique within this journal namespace.
     * @return Unused 16-character hexadecimal JID.
     *
     * Uniqueness is checked only against this journal's jid_map; JIDs are not
     * intended to be globally unique across unrelated documents.
     */
    std::string JID();

    // std::string ReleaseString() const;

private:
    /**
     * @brief Recursively locate the deepest open Release.
     * @param start Current Release node from which to continue searching.
     * @param path Release-number path accumulated during traversal.
     * @return Deepest open Release reachable from @p start.
     *
     * Each visited Release Number is appended to @p path.  When no further
     * open child Release exists, the current node is returned.
     */
    XmlNode FindActiveRelease(XmlNode start, std::vector<int>& path);
};

/**
 * @struct Action
 * @brief Base class for a journal Change transaction.
 *
 * Action owns behavior common to every transaction type: creation of the
 * Change element shell, Type, JID, and TimeStamp attributes, Reversed state, common
 * conflict reporting, and reversal stamping.  Derived classes contribute only
 * their action-specific payload and inverse DOM operation.
 */
struct Action {
    XmlJrnl& jrnl;             ///< Journal/source context for this action.
    XmlNode action_node;       ///< Change node in the journal DOM.
    Error* err = nullptr;      ///< Action-local error or INFO conflict status.

    std::string type;          ///< Change Type attribute supplied by the specialization.
    std::string jid;           ///< Logical source-node JID supplied by the specialization.

    Action(XmlJrnl& j) : jrnl(j) {}
    Action(XmlJrnl& j, XmlNode action) : jrnl(j), action_node(action) {}

    virtual ~Action() = default;

    /**
     * @brief Apply the inverse of this action to the source DOM.
     */
    virtual void Undo() = 0;

    /**
     * @brief Create the common journal Change node.
     *
     * Derived Record() implementations set @ref type and @ref jid before
     * calling this method, then append their action-specific child nodes.
     */
    void Record()
    {
        if (type.empty() || jid.empty()) {
            err = new Error{lvl::ERR, "Cannot record journal action: Type or JID is missing", ""};
            return;
        }

        std::string xml =
            "\n<Change Type=\"" + type + "\""
            " TimeStamp=\"" + CurrentIsoTimestampUTC() + "\""
            " JID=\"" + jid + "\">"
            "<Reversed TimeStamp=\"\" Value=\"false\"/>"
            "</Change>\n";

        action_node = jrnl.active_release.AddChild(xml);

        if (action_node.err)
            err = action_node.err;
    }

protected:
    /**
     * @brief Mark a successfully undone action as reversed and timestamp it.
     */
    void ReverseStamp();

    /**
     * @brief Report a legitimate journal-history conflict.
     * @param msg Human-readable conflict description.
     * @param cause Journal node that best identifies the precipitating action.
     *
     * Conflicts are lvl::INFO because they represent incompatible transaction
     * history rather than a library/programming failure.
     */
    void Conflict(const std::string& msg, XmlNode cause)
    {
        err = new Error{lvl::INFO, "Conflict: " + msg, cause.node ? cause.GetPath() : action_node.GetPath()};
    }
};

/**
 * @struct ActionModify
 * @brief Journal action for replacement/modification of one logical node.
 *
 * Record() stores the parent JID and the Base64-encoded prior node XML.
 * Undo() restores that prior XML while preserving the logical JID and updating
 * jid_map to the replacement xmlNodePtr.
 */
struct ActionModify : public Action {
    XmlNode node;              ///< Live source node while recording.
    std::string oldXML;        ///< Serialized state prior to modification.

    ActionModify(XmlJrnl& j, XmlNode n, const std::string& old);
    ActionModify(XmlJrnl& j, XmlNode action);

    void Record();
    void Undo() override;
};

/**
 * @struct ActionDelete
 * @brief Journal action for removal and structural restoration of a node.
 *
 * Record() stores Parent plus optional Before/After element-sibling JIDs and
 * the serialized deleted node.  Undo() validates that structural slot before
 * reinsertion; incompatible intervening changes are reported as INFO conflicts.
 */
struct ActionDelete : public Action {
    XmlNode node;              ///< Live source node while recording.

    ActionDelete(XmlJrnl& j, XmlNode n);
    ActionDelete(XmlJrnl& j, XmlNode action, bool);

    void Record();
    void Undo() override;
};

/**
 * @struct ActionAdd
 * @brief Journal action for insertion of a new logical node.
 *
 * Record() stores the added JID and parent JID.  Undo() removes the live node
 * when it still belongs to the recorded parent, leaves the JID reserved with a
 * null mapping, and stamps the Change reversed.
 */
struct ActionAdd : public Action {
    XmlNode node;              ///< Live source node while recording.

    ActionAdd(XmlJrnl& j, XmlNode n);
    ActionAdd(XmlJrnl& j, XmlNode action, bool);

    void Record();
    void Undo() override;
};
