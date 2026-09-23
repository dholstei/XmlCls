# XmlCls

## Overview
`XmlCls` is a lightweight C++ wrapper around **libxml2** that provides safer, more expressive access to XML documents, nodes, and XPath queries. It is designed for configuration-driven systems where XML is the primary interchange format and where explicit error propagation is preferred over exceptions.

Key characteristics:
- No exception throwing; all failures are reported through an explicit `Error` structure.
- Lightweight C++ wrappers around `libxml2` document, node, and XPath objects.
- Strongly-typed XPath accessors using templates.
- Optional XML mutation journaling through `XmlJrnl`.
- Minimal policy assumptions, making it suitable for console, GUI, embedded-host, or service environments.

The design aligns well with systems that require deterministic behavior, auditability, and predictable error handling.

## Files
- **XmlCls.h** – Public C++ API declarations: classes, methods, and inline helpers.
- **XmlCls.cpp** – Parsing, XPath evaluation, mutation, journaling, and undo implementations.
- **XmlClsLib.cpp** – Language-neutral `extern "C"` facade over the C++ `XmlDoc`, `XmlNode`, and `XmlJrnl` APIs.
- **XmlCls.py** – Lightweight Python/`ctypes` interface over `XmlClsLib.so`, preserving the C++ document, node, XPath, mutation, journal, and error semantics.
- **XmlClsEdit.py** – Lightweight PyQt6 tree editor built on the Python interface, including XPath result navigation.

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

The Python layer uses `ctypes` only as the ABI bridge into `XmlClsLib.so`; it does not independently implement parsing, XPath, mutation, or journaling with libxml2. `XmlCls` represents the C++ `XmlDoc` and `XmlNode` remains a lightweight wrapper around an `xmlNodePtr`. Document and node-relative XPath therefore use the same canonical C++ implementation and error semantics as native callers.

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

The C++ API is exposed through a language-neutral `extern "C"` facade in
`XmlClsLib.so`. The facade is intentionally an ABI adapter rather than a second
XML implementation: parsing, XPath evaluation, node mutation, journaling, and
path generation delegate to the corresponding `XmlDoc`, `XmlNode`, or
`XmlJrnl` methods.

The interface now covers document construction and lifetime, typed XPath,
journal access and state operations, node serialization and structural paths,
and all editor mutation operations. Representative entry points include:

```c
void* XmlDoc_FromFile(const char* filename);
void* XmlDoc_FromXML(const char* XML);
void* XmlDoc_Attach(xmlDocPtr raw);
void XmlDoc_Free(void* owner);

void XmlDoc_Save(void* owner, const char* filename);
void XmlDoc_OpenJournal(void* owner, const char* filename);
void XmlDoc_CreateJournal(void* owner, const char* filename, const char* XML);
int XmlDoc_HasJournal(void* owner);
void* XmlDoc_Journal(void* owner);

const char* XmlDoc_XPathString(void* owner, xmlNodePtr node, const char* query);
double XmlDoc_XPathDouble(void* owner, xmlNodePtr node, const char* query);
int XmlDoc_XPathInt(void* owner, xmlNodePtr node, const char* query);
int XmlDoc_XPathBool(void* owner, xmlNodePtr node, const char* query);
xmlNodePtr XmlDoc_XPathNode(void* owner, xmlNodePtr node, const char* query);
size_t XmlDoc_XPathNodes(void* owner, xmlNodePtr node, const char* query,
                         xmlNodePtr* nodes, size_t capacity);

const char* XmlNode_XML(xmlNodePtr node);
const char* XmlNode_GetPath(xmlNodePtr node);
xmlNodePtr XmlNode_Parse(xmlNodePtr node, const char* XML);
xmlNodePtr XmlNode_AddChild(xmlNodePtr node, const char* XML);
xmlNodePtr XmlNode_AddBefore(xmlNodePtr node, const char* XML);
xmlNodePtr XmlNode_AddAfter(xmlNodePtr node, const char* XML);
void XmlNode_MoveChild(xmlNodePtr node, xmlNodePtr parent);
void XmlNode_MoveBefore(xmlNodePtr node, xmlNodePtr sibling);
void XmlNode_MoveAfter(xmlNodePtr node, xmlNodePtr sibling);
void XmlNode_Delete(xmlNodePtr node);

void XmlJrnl_Undo(void* journal);
void XmlJrnl_Redo(void* journal);
void XmlJrnl_MarkRelease(void* journal, const char* note);
const char* XmlJrnl_StampState(void* journal, const char* type, const char* note);
void XmlJrnl_Restore(void* journal, const char* jid);
```

`XmlNode_GetPath()` is a thin wrapper around `XmlNode::GetPath()`. The editor
uses it to display the structural XPath of the selected node without duplicating
libxml2 traversal in either the C or Python layer.

#### Error propagation across the C ABI

The C++ library reports operational failures through `struct Error` rather than
exceptions:

```cpp
struct Error {
    lvl level;
    std::string msg;
    std::string data;
};
```

The C facade preserves the same information in its C-compatible `CError`
representation. Errors produced by a C++ `XmlDoc` or `XmlNode` operation are
transferred to the corresponding thread-local C error channel and retrieved
with `XmlDoc_Error()` or `XmlNode_Error()`. Python immediately copies the
severity, message, and diagnostic data into a Python-owned `CError` before the
C allocation is released.

Consequently, the same diagnostic information follows an operation through the
entire stack:

```text
XmlDoc / XmlNode / XmlJrnl
        ↓
      Error
        ↓
 XmlClsLib.so CError
        ↓
 Python CError
        ↓
 XmlClsEdit ErrorPopup
```

The adapters do not invent a second success/failure convention. C++ `void`
operations such as `XmlNode::Delete()`, `parse()`, and the Move methods remain
void-style operations in Python; the caller invokes the method and then
inspects the object's `err`. Methods such as `AddChild()` that return an
`XmlNode` carry their error on the returned node, matching the C++ API.

Strings returned by the C facade are borrowed thread-local strings and must be
copied by callers that need to retain them across subsequent interface calls.
All object code linked into `XmlClsLib.so`, including objects extracted from
static libraries, must be compiled as position-independent code (`-fPIC`).

### `XmlClsEdit.py`

`XmlClsEdit.py` presents the source DOM as a `QTreeWidget`. Each tree item stores
the corresponding `xmlNodePtr`; a lightweight Python `XmlNode` is created only
when an operation needs it. The canonical `XmlCls` document remains alive for
the lifetime of the editor window.

Run the editor with an optional XML filename:

```bash
python3 XmlClsEdit.py
python3 XmlClsEdit.py Config.xml
```

Between the menu bar and XML tree, the editor provides an XPath navigation
field with a result-position indicator and Up/Down navigation buttons. Selecting
a tree item displays that node's `XmlNode::GetPath()` value in the field.

The field can also be edited as an XPath node-list query. The Up/Down buttons
evaluate the query as `list[XmlNode]`, move the tree cursor among the matching
nodes, expand and scroll the tree as required, and display the current position
as `N/M` (for example, `3/7`). Navigation wraps at either end of the result set.
A query with no matching nodes displays `0/0`. While navigating a result set,
the manually entered XPath remains in the field rather than being replaced by
the structural path of each selected result.

The tree also retains the standard Qt `QTreeView` keyboard navigation:

- **Up / Down** – move to the previous or next visible item.
- **Left / Right** – collapse or expand the current item.
- **- / +** – collapse or expand the current item.
- **\*** – expand the current item and all of its descendants.
- **Home / End** – move to the first or last visible item.
- **Page Up / Page Down** – move through the tree by viewport pages.
- **F2** – open the editor's Direct XML editor for the current node.

Except for **F2**, which is connected to `Edit → Direct...`, these navigation
keys are standard `QTreeView` behavior and require no editor-specific key
handling. **View → Collapse All** and **View → Expand All** remain available
for whole-tree operations.

The menus provide:

- **File**: Open, Save, and Quit.
- **Edit**: Copy or Cut XML, paste as a child, paste before or after the selected
  node, delete the selected node, and directly edit the selected XML fragment.
- **View**: Collapse All and Expand All.
- **Journal**: Create, Undo, Redo, Mark Release, Mark Restore Point, and a
  Restore submenu populated from the attached journal.

The window title marks unsaved source-DOM changes with `*`. Structural drag and
drop is intentionally deferred. Attribute editing is available through the
Direct XML editor.

The editor and native C++ API use the same mutation implementation. Python does
not independently reproduce Add, Modify, Deletion, Undo, or Restore semantics;
those operations cross the C ABI into `XmlCls`.

Cut places the selected node's serialized XML on the system clipboard, so it
can still be pasted into another application. The editor also retains the
source `xmlNodePtr` and adds a private clipboard MIME marker. While that marker
remains present, the first Paste, Paste Before, or Paste After moves the live
node through `MoveChild()`, `MoveBefore()`, or `MoveAfter()` instead of parsing
a copy. Replacing the clipboard from any application removes the marker and
cancels the pending move. After a successful move, the XML remains ordinary
clipboard text and subsequent pastes create copies.

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

`Undo()` reverses the most recent live Change and `Redo()` reapplies reversed
Changes in their original order. Modify and Add capture the additional forward
XML needed for Redo when first undone; Delete and Move reuse their recorded
identity and structural anchors. Recording a new mutation after Undo preserves
the abandoned history but marks that branch non-redoable.

### Persistent Journal Association

A source document declares a persistent journal with attributes on its document
element:

```xml
<Config JRNL="Config.xml.jrnl" STATE_JID="0123456789abcdef">
```

The document element may have any name. `JRNL` contains the journal filename;
relative names are resolved from the source XML file's directory. Appending
`.jrnl` to the complete source filename gives predictable names such as
`Config.xml.jrnl`, `conf_file.yaml.jrnl`, or `conf_file.json.jrnl`.

Every `XmlDoc` construction path,including wrapping an existing `xmlDocPtr`,
opens and validates a declared journal automatically. Applications such as
`XmlClsEdit.py`, `LicenseIssue`, and future Python utilities do not repeat this
lifecycle logic. A missing or invalid declared journal is reported through the
normal `err`/`XmlCls_LastError()` channel and inhibits mutation while leaving
the source DOM readable.

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
- Named move methods with and without an attached journal.
- Move no-op detection and move-conflict handling.
- State stamping and source/journal state validation.
- Persistent journal metadata and relative journal filenames.

## Notes

`XmlCls` is intended as infrastructure code. It deliberately avoids policy
decisions about logging, UI notifications, and journal conflict resolution.
Those responsibilities belong to the consuming application.