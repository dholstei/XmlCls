# XmlCls

## Overview
`XmlCls` is a lightweight C++ wrapper around **libxml2** that provides safer, more expressive access to XML documents, nodes, and XPath queries. It is designed for configuration‑driven systems where XML is the primary interchange format and where explicit error propagation is preferred over exceptions.

Key characteristics:
- No exception throwing; all failures are reported through an explicit `Error` structure.
- RAII‑style management of `libxml2` objects (`xmlDocPtr`, `xmlNodePtr`, `xmlXPathContextPtr`).
- Strongly‑typed XPath accessors using templates.
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

### XPath Result Conversion

**Purpose**  
Provide strongly typed access to XPath results while centralizing conversion logic.

Supported conversions typically include:
- `int`, `double`
- `std::string`
- `std::vector<XmlNode>`

Invalid conversions or empty result sets populate the `Error` state instead of throwing.

## Usage Example

```cpp
XmlDoc doc("config.xml");
HANDLE_ERR(doc.err);

XmlNode root = doc.XPath<XmlNode>("/Config");
HANDLE_ERR(root.err);

int rate = root.XPath<int>("number(@Rate)");
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

