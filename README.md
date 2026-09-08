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
- **XmlClsEdit.py** – Lightweight PyQt6 tree editor built on the Python and C interfaces.

## Dependencies
- **libxml2** (headers and library)
- **Python 3.10 or later** (optional Python interface and editor)
- **PyQt6** (optional editor)

Typical Linux packages:
```bash
libxml2-dev    (Debian/Ubuntu)
libxml2-devel  (RHEL/CentOS/Fedora)
```

On Windows, libxml2 must be provided explicitly (vcpkg, Conan, or a locally built distribution).

PyQt6 may be installed into the active Python environment with:

```bash
python3 -m pip install PyQt6
```

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

Selected C++ functionality is exposed through a small language-neutral `extern "C"` facade in `XmlClsLib.so`. The ABI remains independent of Python and covers document attachment, saving, journal control, restore points, node serialization, and mutation:

```c
void* XmlDoc_Attach(xmlDocPtr doc);
void XmlDoc_Detach(void* owner);
int XmlDoc_Save(void* owner, const char* filename);

int XmlDoc_OpenJournal(void* owner, const char* filename);
int XmlDoc_CreateJournal(void* owner, const char* filename, const char* XML);
int XmlDoc_Undo(void* owner);
int XmlDoc_HasJournal(void* owner);
int XmlDoc_MarkRelease(void* owner, const char* note);
int XmlDoc_MarkRestorePoint(void* owner, const char* note, char* jid, size_t capacity);
size_t XmlDoc_RestorePoints(void* owner, char* buffer, size_t capacity);
int XmlDoc_Restore(void* owner, const char* jid);

size_t XmlNode_XML(xmlNodePtr node, char* buffer, size_t capacity);
xmlNodePtr XmlNode_Parse(xmlNodePtr node, const char* XML);
xmlNodePtr XmlNode_AddChild(xmlNodePtr node, const char* XML);
xmlNodePtr XmlNode_AddBefore(xmlNodePtr node, const char* XML);
xmlNodePtr XmlNode_AddAfter(xmlNodePtr node, const char* XML);
int XmlNode_Delete(xmlNodePtr node);

const char* XmlCls_LastError(void);
```

The XML serialization interface uses a caller-provided buffer rather than returning an allocation owned by C++:

```c
size_t XmlNode_XML(xmlNodePtr node, char* buffer, size_t size);
```

This keeps allocation ownership on the caller's side and makes the same shared library usable from Python, C#, LabVIEW, and other environments capable of calling a C ABI.

All object code linked into `XmlClsLib.so`, including objects extracted from static libraries, must be compiled as position-independent code (`-fPIC`).

`XmlCls_LastError()` returns thread-local storage owned by the library. Callers
must copy the message if it must survive the next C-interface call on that
thread.

### `XmlClsEdit.py`

`XmlClsEdit.py` presents the source DOM as a `QTreeWidget`. Each tree item stores
the corresponding `xmlNodePtr`; a lightweight Python `XmlNode` is created only
when an operation needs it. The canonical `XmlCls` document remains alive for
the lifetime of the editor window.

Run the editor with an optional XML filename:

```bash
python3 XmlClsEdit.py
python3 XmlClsEdit.py PonziCoin.xml
```

The menus provide:

- **File**: Open, Save, and Quit.
- **Edit**: Copy XML, paste as a child, paste before or after the selected node,
  delete the selected node, and directly edit the selected XML fragment.
- **View**: Collapse All and Expand All.
- **Journal**: Create, one-level Undo, Mark Release, Mark Restore Point, and a
  Restore submenu populated from the attached journal.

The window title marks unsaved source-DOM changes with `*`. Structural drag and
drop is intentionally deferred. Attribute editing is available through the
Direct XML editor.

The editor and native C++ API use the same mutation implementation. Python does
not independently reproduce Add, Modify, Deletion, Undo, or Restore semantics;
those operations cross the C ABI into `XmlCls`.

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

### Persistent Journal Association

A source document declares a persistent journal with attributes on its document
element:

```xml
<PonziCoin JRNL="PonziCoin.xml.jrnl"
           STATE_JID="0123456789abcdef">
```

The document element may have any name. `JRNL` contains the journal filename;
relative names are resolved from the source XML file's directory. Appending
`.jrnl` to the complete source filename gives predictable names such as
`PonziCoin.xml.jrnl`, `conf_file.yaml.jrnl`, or `conf_file.json.jrnl`.

`STATE_JID` identifies the journal state represented by the saved source DOM.
On save, `XmlCls` stamps a new journal State, writes its JID to the source
document element, saves the source, and then saves the journal. Opening an
existing journal validates this association; an absent, unknown, or stale state
makes the source immutable and reports a warning.

Journal metadata is stored as attributes rather than as a child element so it
does not change application child counts, sibling relationships, deletion
anchors, or move semantics.

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
node.AddBefore(xml);
node.AddAfter(xml);
node.MoveChild(parent);
node.MoveBefore(sibling);
node.MoveAfter(sibling);
node.Delete();
node.GetPath();
node.JID();
node.JID(jid);
```

When a node belongs to a journal-enabled document, replacement, addition,
movement, and deletion automatically generate the corresponding journal
transaction.

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
std::string StampState(std::string type, std::string note);
void MarkRelease(std::string note = "");
void Restore(std::string jid);
void ValidateState();
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
- Move-before, move-after, and move-child recording and undo.
- Move no-op detection and move-conflict handling.
- State stamping and source/journal state validation.
- Persistent journal metadata and relative journal filenames.

## Notes

`XmlCls` is intended as infrastructure code. It deliberately avoids policy
decisions about logging, UI notifications, and journal conflict resolution.
Those responsibilities belong to the consuming application.