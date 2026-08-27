# XmlCls

## Overview
`XmlCls` is a lightweight C++ wrapper around **libxml2** that provides safer, more expressive access to XML documents, nodes, and XPath queries. It is designed for configuration‑driven systems where XML is the primary interchange format and where explicit error propagation is preferred over exceptions.

Key characteristics:
- No exception throwing; all failures are reported through an explicit `Error` structure.
- Lightweight C++ wrappers around `libxml2` document, node, and XPath objects.
- Strongly‑typed XPath accessors using templates.
- Optional XML mutation journaling through `XmlJrnl`.
- Minimal policy assumptions, making it suitable for console, GUI, embedded‑host, or service environments.

The design aligns well with systems that require deterministic behavior, auditability, and predictable error handling.

## Files
- **XmlCls.h** – Public C++ API declarations: classes, methods, and inline helpers.
- **XmlCls.cpp** – Parsing, XPath evaluation, mutation, journaling, and undo implementations.
- **XmlClsLib.cpp** – Minimal language-neutral `extern "C"` facade over selected C++ `XmlNode` operations.
- **XmlCls.py** – Lightweight Python/`ctypes` interface using libxml2 directly for document parsing and XPath, with selected C++ operations exposed through `XmlClsLib.so`.

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

`XmlCls` does not use exceptions for normal library failures. Operations report
status through an `Error` pointer containing a severity level, message, and
optional diagnostic data.

Journal conflicts are deliberately distinguished from implementation failures.
An undo that cannot be applied because of a later, incompatible journal
transaction reports `lvl::INFO` with a `"Conflict"` message. Conflict resolution
is policy and is therefore left to the consuming editor, GUI, or application.

### XPath as the Primary Navigation Mechanism

XPath is the primary mechanism for both document queries and structural
navigation. This avoids manual libxml2 sibling/child traversal where text,
CDATA, comments, and other non-element nodes can obscure the XML structure of
interest.

`XmlDoc::XPath<T>()` and `XmlNode::XPath<T>()` provide typed results for:

- `std::string`
- `double`
- `int`
- `bool`
- `std::vector<XmlNode>`

For scalar requests, a node-set resolving to one node is implicitly converted
using the corresponding XPath value of that node. For example:

```cpp
std::string name = doc.XPath<std::string>("/Config/@Name");
double voltage   = doc.XPath<double>("/Config/@Voltage");
bool enabled     = doc.XPath<bool>("/Config/@Enabled");
```

The caller therefore specifies the desired C++ type rather than repeatedly
embedding `string(.)`, `number(.)`, or `boolean(.)` conversion logic.

### Canonical `XmlDoc` and Transient `XmlNode`

An `XmlDoc` is the canonical C++ wrapper for one libxml2 DOM. The association is
stored in `xmlDoc::_private`, allowing an `XmlNode` constructed from an
`xmlNodePtr` to recover its owning `XmlDoc` without a global document map.

`XmlNode` is intentionally lightweight and transient. It does not own the
underlying node and can be constructed temporarily for XPath evaluation,
serialization, or mutation.

`XmlNode::GetPath()` remains available for diagnostics, but structural XPath is
not used as persistent journal identity because a path can change as the DOM is
modified.

## Python Interface

A lightweight Python interface is provided for applications and scripting environments that need the core `XmlCls` navigation model without duplicating the C++ implementation.

The Python layer uses `ctypes` to call libxml2 directly for parsing and XPath evaluation. `XmlCls` represents the document and `XmlNode` is a lightweight wrapper around an `xmlNodePtr`. Relative node XPath uses `xmlXPathNodeEval()`, so evaluation does not modify the persistent XPath context node.

Supported typed XPath results currently include:

- `str`
- `float`
- `int`
- `bool`
- `XmlNode`
- `list[XmlNode]`

For example:

```python
from XmlCls import XmlCls

if __name__ == "__main__":
    dom = XmlCls("<root><child value='3.14'>text</child></root>")

    pi = dom.XPath("//child/@value", float)
    print(pi)

    txt = dom.XPath("//child", str)
    print(txt)

    child = dom.XPath("//child")
    val_str = child.XPath("@value", str)
    print(val_str)

    print(child.XML())
```

Output:

```text
3.14
text
3.14
<child value="3.14">text</child>
```

The serialized XML uses libxml2's serialization conventions. In the example above, the input attribute uses single quotes (`value='3.14'`), while serialization produces the equivalent double-quoted form (`value="3.14"`). Applications should therefore treat serialized XML as XML rather than expect byte-for-byte preservation of the original source formatting.

As in the C++ interface, routine XML and XPath errors are reported through an `Error` object rather than exceptions. Exceptions are reserved for programming/API-contract errors that require programmer intervention.

### `XmlClsLib.so`

Selected C++ functionality is exposed through a small language-neutral `extern "C"` facade in `XmlClsLib.so`. The initial interface exposes `XmlNode::XML()` while keeping the ABI independent of Python.

The XML serialization interface uses a caller-provided buffer rather than returning an allocation owned by C++:

```c
size_t XmlNode_XML(xmlNodePtr node, char* buffer, size_t size);
```

This keeps allocation ownership on the caller's side and makes the same shared library usable from Python, C#, LabVIEW, and other environments capable of calling a C ABI.

All object code linked into `XmlClsLib.so`, including objects extracted from static libraries, must be compiled as position-independent code (`-fPIC`).

## Mutation Journaling

### `XmlJrnl`

`XmlJrnl` extends `XmlDoc` with a journal DOM permanently associated with one
canonical source `XmlDoc`:

```cpp
XmlDoc& source_doc;
```

The reference cannot be rebound after construction. This prevents journal
operations from accidentally being applied to a different open DOM.

A journal is organized as nested `<Release>` nodes. `active_release` identifies
the deepest currently open release and `rel_no` contains its numeric path; for
example `{0, 2, 1}` represents Release `0.2.1`.

`OpenJournal()` and `CreateJournal()` are deleted on `XmlJrnl` itself so that a
journal cannot recursively journal another journal.

### Journal IDs (`JID`)

When journaling is enabled, logical XML nodes participating in transactions are
identified by a persistent hexadecimal `JID` attribute.

```xml
<Item JID="e3e2ebd167168d3c"/>
```

JIDs need only be unique within a journal; they are not intended as globally
unique identifiers. `XmlJrnl` maintains:

```cpp
std::map<std::string, xmlNodePtr> jid_map;
```

A live JID maps directly to its current `xmlNodePtr`. A deleted logical node
remains reserved in the map with a `nullptr` value so retained journal history
cannot accidentally reuse its identity.

`XmlNode::JID()` returns an existing JID or creates and registers one.
`XmlNode::JID(std::string jid)` propagates an existing logical identity to a new
physical `xmlNodePtr`, such as after `parse()` or `Undo()` replaces a node.

`BuildJIDMap()` reconstructs the live map directly with XPath over `//@JID`;
manual DOM traversal is unnecessary.

### Action Model

Journal transactions are represented by a small action hierarchy:

```text
Action
├── ActionModify
├── ActionDelete
├── ActionMove
└── ActionAdd
```

`Action` owns mechanics common to every transaction:

- creation of the `<Change>` node,
- `Type`, `TimeStamp`, and `JID`,
- the initial `<Reversed TimeStamp="" Value="false"/>` state,
- reversal timestamping,
- common `lvl::INFO` conflict reporting.

Each specialization records only the state required by its mutation and
implements its inverse operation.

A typical journal record is:

```xml
<Change Type="Modify"
        TimeStamp="..."
        JID="e3e2ebd167168d3c">
    <Reversed TimeStamp="" Value="false"/>
    ...
</Change>
```

### `ActionModify`

A Modify transaction records the logical node JID, its parent JID, and the
Base64-encoded XML that existed before the modification:

```xml
<Parent JID="..."/>
<Node Encoding="Base64">...</Node>
```

`Undo()` restores the previous serialized state while retaining the same JID.
Because replacement creates a new `xmlNodePtr`, `jid_map` is updated to point to
the restored physical node.

A missing or structurally incompatible parent is treated as a journal conflict,
not a programming error.

### `ActionDelete`

A Deletion transaction records enough structural context to restore the node to
its previous element position:

```xml
<Parent JID="..."/>
<Before JID="..."/>
<After JID="..."/>
<Node Encoding="Base64">...</Node>
```

`Before` and `After` are omitted when the deleted node had no corresponding
element sibling.

Before deletion, the parent and its element children are assigned JIDs. The
deleted node's JID remains reserved with a null live mapping.

`Undo()` validates the recorded parent and sibling relationships before
reinsertion. It supports restoration of middle, first, last, and only-child
nodes. If later transactions have removed or invalidated the required
structural context, the operation returns an INFO-level conflict and leaves the
transaction unreversed.

### `ActionAdd`

An Add transaction records the new logical node JID and its parent JID.

Undoing an Add verifies that the live node still belongs to the recorded parent,
removes it, changes its `jid_map` entry to `nullptr`, and stamps the transaction
as reversed.

### Undo Dispatch

`XmlJrnl::Undo(XmlNode action_node)` is intentionally a dispatcher rather than a
large mutation implementation. It selects the action specialization from the
journal `@Type` and delegates the inverse operation.

The overloads are:

```cpp
void Undo();
void Undo(XmlNode action_node);
void Undo(std::vector<XmlNode> action_nodes);
```

`Undo()` selects the most recent unreversed action in the active release.
The vector overload processes actions in reverse order.

An already reversed action is a no-op.

The `<Reversed>` state is changed only after the DOM operation succeeds:

```xml
<Reversed TimeStamp="2026-..." Value="true"/>
```

A failed or conflicted undo remains:

```xml
<Reversed TimeStamp="" Value="false"/>
```

## Public API Summary

### `XmlDoc`

`XmlDoc` parses and represents one canonical XML DOM, caches its XPath context,
and optionally owns an attached `XmlJrnl`.

Typical operations include:

```cpp
XmlDoc doc("config.xml");

auto nodes = doc.XPath<std::vector<XmlNode>>("/Config/Subsystem");
std::string name = doc.XPath<std::string>("/Config/@Name");

doc.CreateJournal("config.jrnl.xml");
doc.Save();
```

Copy and move operations are disabled so that the canonical DOM association
cannot silently change.

### `XmlNode`

`XmlNode` provides relative XPath queries and mutation methods around an
existing `xmlNodePtr`.

Important operations include:

```cpp
node.XPath<T>(query);
node.XML();
node.parse(xml);
node.AddChild(xml);
node.Delete();
node.GetPath();
node.JID();
node.JID(jid);
```

When a node belongs to a journal-enabled document, `parse()`, `AddChild()`, and
`Delete()` automatically generate the corresponding journal transaction.

### `XmlJrnl`

Important state and operations include:

```cpp
XmlDoc& source_doc;
std::vector<int> rel_no;
XmlNode active_release;
std::map<std::string, xmlNodePtr> jid_map;

void LogAdd(XmlNode& node);
void LogModify(XmlNode& node, const std::string& oldXML);
void LogDelete(XmlNode& node);

void Undo();
void Undo(XmlNode action_node);
void Undo(std::vector<XmlNode> action_nodes);

void RefreshActiveRelease();
void BuildJIDMap();
std::string JID();
```

## Usage Example

```cpp
XmlDoc doc("config.xml");
HANDLE_ERR(doc.err);

doc.CreateJournal("config.jrnl.xml");
HANDLE_ERR(doc.JRNL->err);

XmlNode subsystem =
    doc.XPath<std::vector<XmlNode>>("/Config/Subsystem[@Name='Cooling']")[0];

std::string name = subsystem.XPath<std::string>("@Name");
int channels = subsystem.XPath<int>("@Channels");

subsystem.AddChild("<Channel Name=\"Return\"/>");
HANDLE_ERR(subsystem.err);
```

The added node is assigned a JID and an Add transaction is written beneath the
active journal release.

## Threading Notes

The current implementation caches one XPath context per `XmlDoc`; operations
against that shared context should therefore be treated as serialized.

A future read-only use case could allow multiple worker threads to share an
immutable `xmlDocPtr` while using independent XPath contexts. A bounded pool of
contexts is one possible implementation if XPath throughput ever justifies the
additional synchronization and lifecycle complexity.

DOM mutation and simultaneous XPath traversal require document-level
synchronization. Internal synchronization is not currently part of the public
`XmlCls` contract.

## Design Rationale

- Explicit error propagation rather than exception-driven control flow.
- XPath-centric navigation instead of manual libxml2 structural traversal.
- Lightweight transient `XmlNode` wrappers.
- Canonical `XmlDoc` identity through libxml2 `_private`.
- Persistent logical node identity through journal-local JIDs.
- Separation of common transaction mechanics from action-specific behavior.
- INFO-level conflicts distinguish incompatible journal history from software
  faults.
- Conflict resolution remains application policy rather than being embedded in
  the XML infrastructure layer.

## Current Validation

The journal implementation has regression coverage for:

- Modify recording and undo.
- JID preservation across node replacement.
- Delete recording and restoration.
- First, middle, last, and only-child deletion undo.
- Parent-deletion conflict detection with `lvl::INFO`.
- Add recording and undo.
- Reversal state and timestamp behavior.

At the current development checkpoint, the XmlCls test suite reports:

```text
249 check(s) passed.
SUCCESS: All XmlCls tests passed.
```

## Notes

`XmlCls` is intended as infrastructure code. It deliberately avoids policy
decisions about logging, UI notifications, and journal conflict resolution.
Those responsibilities belong to the consuming application.
