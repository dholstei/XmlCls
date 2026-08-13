# XmlCls

## Overview
`XmlCls` is a lightweight C++ wrapper around **libxml2** that provides safer, more expressive access to XML documents, nodes, and XPath queries. It is designed for configuration‑driven systems where XML is the primary interchange format and where explicit error propagation is preferred over exceptions.

Key characteristics:
- No exception throwing; all failures are reported through an explicit `Error` structure.
- RAII‑style management of `libxml2` objects (`xmlDocPtr`, `xmlNodePtr`, `xmlXPathContextPtr`).
- Strongly‑typed XPath accessors using templates.
- Optional XML mutation journaling through `XmlJrnl`.
- Minimal policy assumptions, making it suitable for console, GUI, embedded‑host, or service environments.

The design aligns well with systems that require deterministic behavior, auditability, and predictable error handling.

## Files
- **XmlCls.h** – Public API declarations: classes, methods, and inline helpers.
- **XmlCls.cpp** – Implementations of parsing, XPath evaluation, and lifecycle management.

## Dependencies
- **libxml2** (headers and library)

Typical Linux packages:
```bash
libxml2-dev    (Debian/Ubuntu)
libxml2-devel  (RHEL/CentOS/Fedora)
```

On Windows, libxml2 must be provided explicitly (vcpkg, Conan, or a locally built distribution).

## Core Concepts

### Explicit Error Handling
All operations update an `Error` member rather than throwing exceptions. Callers are expected to check and handle errors explicitly, which supports:
- Deterministic control flow
- Compatibility with safety‑critical or embedded coding standards
- Easy redirection of error reporting (console, GUI, logging framework, etc.)

### XPath as the Primary Query Mechanism
XPath expressions are treated as first‑class inputs. Templated helpers convert XPath results directly into C++ scalar or container types.

## Public API Documentation (docsys‑style)

### Class: `XmlDoc`

**Purpose**  
Encapsulates an XML document and its associated XPath context. Responsible for document load, lifetime management, and XPath context caching.

**Responsibilities**
- Parse XML from file or memory
- Maintain a shared XPath context per document
- Own and release the underlying `xmlDocPtr`

**Key Members**
- `xmlDocPtr doc` – Underlying libxml2 document handle
- `Error err` – Last error state for operations on the document

**Key Methods**
- `XmlDoc(const std::string& filename)`  
  Loads and parses an XML document from disk.

- `XmlDoc(const char* buffer, size_t size)`  
  Parses an XML document from an in‑memory buffer.

- `~XmlDoc()`  
  Releases the document and associated XPath context.

- `xmlDocPtr get()`  
  Returns the raw libxml2 document pointer.

---

### Class: `XmlNode`

**Purpose**  
Represents a single XML node associated with an owning `XmlDoc`. Provides scoped XPath queries relative to the node.

**Responsibilities**
- Safe access to `xmlNodePtr`
- XPath evaluation within the node’s context
- Typed extraction of attributes, text, and child nodes

**Key Members**
- `xmlNodePtr node` – Underlying libxml2 node
- `XmlDoc* owner` – Owning document
- `Error err` – Last error state

**Key Methods**
- `XmlNode(xmlNodePtr n, XmlDoc* d)`  
  Constructs a wrapper around an existing libxml2 node.

- `std::string name() const`  
  Returns the node name.

- `template<typename T> XPath(const std::string& expr)`  
  Evaluates an XPath expression relative to this node and converts the result to type `T`.

---

### Class: `XmlJrnl`

**Purpose**  
Extends `XmlDoc` with an XML-based mutation journal for tracking reversible changes to another XML document.

A journal is organized as nested `<Release>` nodes. Mutations are appended beneath the deepest currently open release. The active release path is retained as a vector of integers; for example `{0, 2, 1}` represents Release `0.2.1`.

**Key Members**
- `std::vector<int> rel_no` – Numeric path of the active release.
- `XmlNode active_release` – Deepest currently open `<Release>` node.

**Construction**
- `XmlJrnl(const char* filename)`  
  Opens an existing journal file and resolves its active release.

- `XmlJrnl(const std::string content)`  
  Constructs a journal from XML text and resolves its active release.

`OpenJournal()` and `CreateJournal()` are deleted for `XmlJrnl` itself so that journals cannot recursively journal journals.

**Mutation Logging**
- `LogAdd(XmlNode& added)`  
  Records addition of a node.

- `LogModify(XmlNode& node, const std::string& oldXML)`  
  Records modification of a node, preserving the previous XML representation so the change can be reversed.

- `LogDelete(XmlNode& node)`  
  Records deletion of a node. This must occur before the underlying `xmlNodePtr` is unlinked or freed so its path and XML content remain available.

Mutation records are written beneath `active_release` and include a UTC timestamp plus information identifying the affected XML location. `XmlNode::GetPath()` uses `xmlGetNodePath()` to provide the node's XPath within the document.

**Release Management**
- `RefreshActiveRelease()`  
  Re-scans the journal and selects the deepest nested release whose `Close` attribute is empty.

- `FindActiveRelease(XmlNode start, std::vector<int>& path)`  
  Recursively walks open nested releases while building `rel_no`.

**Undo**
- `Undo(XmlNode action_node)`  
  Applies the inverse of a previously recorded mutation.

### XPath Result Conversion

**Purpose**  
Provide strongly typed access to XPath results while centralizing conversion logic.

`XmlDoc::XPath<T>()` and `XmlNode::XPath<T>()` use the requested C++ template type to select the corresponding libxml2 XPath result conversion.

Supported conversions include:
- `std::string` for XPath string results
- `double` for XPath numeric results
- `int` for numeric results converted to integer
- `bool` for XPath boolean results
- `std::vector<XmlNode>` for XPath node sets

Typical usage:

```cpp
std::string name  = doc.XPath<std::string>("string(/Config/@Name)");
                  = doc.XPath<std::string>("/Config/@Name");  //  if resolves to single node, the XPath `string(.)` function applied
double voltage    = doc.XPath<double>("number(/Config/@Voltage)");
                  = doc.XPath<double>("/Config/@Voltage");  //  if resolves to single node, the XPath `number(.)` function applied
int count         = doc.XPath<int>("count(/Config/Item)");
bool enabled      = doc.XPath<bool>("boolean(/Config/@Enabled)");
                  = doc.XPath<bool>("/Config/@Enabled");    //  if resolves to single node, the XPath `boolean(.)` function applied
auto nodes        = doc.XPath<std::vector<XmlNode>>("/Config/Item");
```

The conversion is implicit in the sense that callers specify only the desired C++ result type; the XPath implementation performs the libxml2 result-type extraction and C++ conversion internally.

For node sets, the returned `XmlNode` objects are lightweight wrappers around nodes owned by the source document. They retain association with the source document and, when journaling is enabled, inherit the document's active `XmlJrnl`.

Invalid conversions, malformed expressions, or incompatible XPath result types populate the `Error` state instead of throwing.

## Usage Example

```cpp
XmlDoc doc("config.xml");
HANDLE_ERR(doc.err);

XmlNode root = doc.XPath<std::vector<XmlNode>>("/Config")[0];
HANDLE_ERR(root.err);

int rate = root.XPath<int>("number(@Rate)");
bool enabled = root.XPath<bool>("boolean(@Enabled)");
std::string name = root.XPath<std::string>("string(@Name)");

HANDLE_ERR(root.err);
```

## Threading Notes
- `libxml2` global initialization must be performed once at program start if used in a multi‑threaded environment.
- Individual `XmlDoc` instances are not internally synchronized.

## Design Rationale
- Avoids implicit control flow via exceptions
- Encourages explicit, auditable error paths
- Keeps XML parsing and schema semantics outside of compiled code when possible

## Notes
This module is intended as infrastructure code. It deliberately avoids policy decisions about logging, UI notifications, or recovery strategies, delegating those responsibilities to the caller.

