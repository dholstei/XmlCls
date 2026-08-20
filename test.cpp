/**
 * @file test.cpp
 * @brief Smoke/regression tests for XmlCls.h / XmlCls.cpp.
 *
 * Build example:
 * @code
 * g++ -std=c++17 -Wall -Wextra -pedantic \
 *     test.cpp XmlCls.cpp base64.cpp \
 *     $(pkg-config --cflags --libs libxml-2.0) \
 *     -o test_xmlcls
 * ./test_xmlcls
 * @endcode
 *
 * These tests exercise the DOM mutation methods:
 * XmlNode::AddChild(), AddBefore(), AddAfter(), parse(), Delete(), and the
 * journal-aware XmlNode behavior.  Tests intentionally call XmlDoc::CreateJournal()
 * immediately after XmlDoc construction when journaling is expected.
 */

#include "XmlCls.h"

#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int failures = 0;
int passes   = 0;

void pass(const char* expr, const char* file, int line)
{
    std::cout << "SUCCESS: " << file << ':' << line << ": " << expr << '\n';
    ++passes;
}

void fail(const char* expr, const char* file, int line)
{
    std::cerr << "FAIL: " << file << ':' << line << ": " << expr << '\n';
    ++failures;
}

#define CHECK(expr) \
    do { \
        if (expr) pass(#expr, __FILE__, __LINE__); \
        else      fail(#expr, __FILE__, __LINE__); \
    } while (0)

#define CHECK_EQ(actual, expected) \
    do { \
        const auto a_ = (actual); \
        const auto e_ = (expected); \
        std::ostringstream oss_; \
        oss_ << #actual " == " #expected \
             << " [actual='" << a_ << "', expected='" << e_ << "']"; \
        if (a_ == e_) pass(oss_.str().c_str(), __FILE__, __LINE__); \
        else { \
            std::cerr << "FAIL: " << __FILE__ << ':' << __LINE__ \
                      << ": " << oss_.str() << '\n'; \
            ++failures; \
        } \
    } while (0)

void print_error(const char* where, ErrorPtr err)
{
    if (!err) return;
    std::cerr << where << ": " << err->msg << " [" << err->data << "]\n";
}

void banner(const std::string& title)
{
    std::cout << "\n========== " << title << " ==========\n";
}

void print_xml(const std::string& label, const XmlDoc& doc)
{
    std::cout << "\n--- " << label << " ---\n";
    std::cout << doc.XML() << '\n';
}

void print_xml(const std::string& label, const XmlDoc* doc)
{
    std::cout << "\n--- " << label << " ---\n";
    if (doc) std::cout << doc->XML() << '\n';
    else     std::cout << "<no journal open>\n";
}

std::vector<XmlNode> require_nodes(XmlDoc& doc, const std::string& xpath)
{
    auto nodes = doc.XPath<std::vector<XmlNode>>(xpath);
    print_error(xpath.c_str(), doc.err);
    CHECK(!doc.err);
    CHECK(!nodes.empty());
    return nodes;
}

void test_document_xpath()
{
    banner("document XPath");

    static const std::string xml =
        "<Root>"
        "  <Item Name=\"alpha\" Value=\"10\">A</Item>"
        "  <Item Name=\"beta\"  Value=\"20\">B</Item>"
        "</Root>";

    XmlDoc doc(xml);
    CHECK(!doc.err);
    print_xml("source XML", doc);

    CHECK_EQ(doc.XPath<std::string>("/Root/Item[@Name='alpha']"), std::string("A"));
    CHECK_EQ(doc.XPath<int>("count(/Root/Item)"), 2);
    CHECK_EQ(doc.XPath<double>("/Root/Item[@Name='beta']/@Value"), 20.0);
    CHECK_EQ(doc.XPath<bool>("/Root/Item[@Name='beta']"), true);

    auto items = doc.XPath<std::vector<XmlNode>>("/Root/Item");
    CHECK_EQ(items.size(), std::size_t{2});
}

void test_add_child_before_after_and_vectors()
{
    banner("AddChild / AddBefore / AddAfter / vector overloads");

    XmlDoc doc(std::string("<Root><A id=\"1\"/></Root>"));
    CHECK(!doc.err);
    print_xml("before mutations", doc);

    auto root = require_nodes(doc, "/Root")[0];
    XmlNode b = root.AddChild("<B id=\"2\"><Leaf>ok</Leaf></B>");
    print_xml("after AddChild(<B...>)", doc);
    CHECK(b.node != nullptr);
    CHECK_EQ(doc.XPath<int>("count(/Root/B)"), 1);
    CHECK_EQ(b.XPath<std::string>("./Leaf"), std::string("ok"));

    auto a = require_nodes(doc, "/Root/A")[0];
    XmlNode before = a.AddBefore("<Before/>");
    print_xml("after AddBefore(<Before/>)", doc);
    CHECK(before.node != nullptr);
    CHECK_EQ(doc.XPath<std::string>("name(/Root/*[1])"), std::string("Before"));

    XmlNode after = a.AddAfter("<After/>");
    print_xml("after AddAfter(<After/>)", doc);
    CHECK(after.node != nullptr);
    CHECK_EQ(doc.XPath<std::string>("name(/Root/A/following-sibling::*[1])"), std::string("After"));

    root.AddChild(std::vector<std::string>{"<C/>", "<D/>"});
    print_xml("after AddChild(vector{<C/>, <D/>})", doc);
    CHECK_EQ(doc.XPath<int>("count(/Root/C | /Root/D)"), 2);
}

void test_parse_replace_node()
{
    banner("parse replace node");

    XmlDoc doc(std::string("<Root><Old id=\"1\">old</Old><Tail/></Root>"));
    CHECK(!doc.err);
    print_xml("before parse()", doc);

    auto old = require_nodes(doc, "/Root/Old")[0];
    old.parse(std::string("<New id=\"2\">new</New>"));
    print_xml("after parse(<New...>)", doc);
    print_error("parse", old.err);
    CHECK(!old.err);
    CHECK(old.node != nullptr);

    CHECK_EQ(doc.XPath<int>("count(/Root/Old)"), 0);
    CHECK_EQ(doc.XPath<int>("count(/Root/New)"), 1);
    CHECK_EQ(doc.XPath<std::string>("/Root/New"), std::string("new"));
    CHECK_EQ(doc.XPath<std::string>("name(/Root/*[1])"), std::string("New"));
    CHECK_EQ(doc.XPath<std::string>("name(/Root/*[2])"), std::string("Tail"));
}

void test_delete_node()
{
    banner("Delete node");

    XmlDoc doc(std::string("<Root><A/><B/><C/></Root>"));
    CHECK(!doc.err);
    print_xml("before Delete()", doc);

    auto b = require_nodes(doc, "/Root/B")[0];
    b.Delete();
    print_xml("after Delete(/Root/B)", doc);

    CHECK(b.node == nullptr);
    CHECK(b.doc == nullptr);
    CHECK_EQ(doc.XPath<int>("count(/Root/B)"), 0);
    CHECK_EQ(doc.XPath<int>("count(/Root/*)"), 2);
}

void test_save_and_reload()
{
    banner("Save and reload");

    const char* path = "/tmp/xmlcls_test_save.xml";

    XmlDoc doc(std::string("<Root><A>saved</A></Root>"));
    CHECK(!doc.err);
    print_xml("before Save(path)", doc);

    doc.Save(path);
    print_error("Save(path)", doc.err);
    CHECK(!doc.err);

    auto root = require_nodes(doc, "/Root")[0];
    root.AddChild("<B>added</B>");
    print_xml("after AddChild(<B>added</B>), before Save()", doc);

    doc.Save();
    print_error("Save()", doc.err);
    CHECK(!doc.err);

    XmlDoc reloaded(path);
    CHECK(!reloaded.err);
    print_xml("reloaded XML", reloaded);
    CHECK_EQ(reloaded.XPath<std::string>("/Root/A"), std::string("saved"));
    CHECK_EQ(reloaded.XPath<std::string>("/Root/B"), std::string("added"));

    std::remove(path);
}

void test_xmljrnl_constructor_and_active_release()
{
    banner("XmlJrnl constructor / source DOM / active Release / JID map");

    static const std::string source_xml =
        "<Root JID=\"1234567890abcdef\">"
        "  <A JID=\"aaaaaaaaaaaaaaaa\"/>"
        "  <B JID=\"bbbbbbbbbbbbbbbb\"/>"
        "</Root>";

    static const std::string journal_xml =
        "<JRNL>"
        "  <Release Number=\"0\" Open=\"2026-01-01T00:00:00Z\" Close=\"\">"
        "    <Release Number=\"1\" Open=\"2026-01-01T00:00:00Z\" Close=\"\">"
        "    </Release>"
        "  </Release>"
        "</JRNL>";

    XmlDoc source(source_xml);
    CHECK(!source.err);

    XmlJrnl journal(source, journal_xml);
    CHECK(!journal.err);

    /*
     * XmlJrnl is permanently associated with its canonical source DOM.
     */
    CHECK(&journal.source_doc == &source);

    /*
     * Constructor should have resolved the active Release.
     */
    CHECK(journal.active_release.node != nullptr);
    CHECK_EQ(journal.rel_no.size(), std::size_t{2});
    CHECK_EQ(journal.rel_no[0], 0);
    CHECK_EQ(journal.rel_no[1], 1);
    CHECK_EQ(journal.active_release.XPath<int>("@Number"), 1);

    /*
     * Constructor should also have indexed all existing source-document JIDs.
     * jid_map values are the @JID attribute nodes themselves.
     */
    CHECK_EQ(journal.jid_map.size(), std::size_t{3});

    CHECK(journal.jid_map.find("1234567890abcdef") != journal.jid_map.end());
    CHECK(journal.jid_map.find("aaaaaaaaaaaaaaaa") != journal.jid_map.end());
    CHECK(journal.jid_map.find("bbbbbbbbbbbbbbbb") != journal.jid_map.end());

    /*
     * Verify that the map points into the source DOM, not the journal DOM.
     */
    for (const auto& [jid, node] : journal.jid_map) {
        CHECK(node != nullptr);
        CHECK(node->doc == source.doc);
    }
}

void test_journal_aware_mutations()
{
    banner("journal-aware XmlNode mutations");

    const char* path = "/tmp/xmlcls_test.jrnl.xml";

    XmlDoc doc(std::string("<Root><A/></Root>"));
    CHECK(!doc.err);

    // Journaling is an explicit, immediate post-construction opt-in.
    doc.CreateJournal(path);
    CHECK(doc.JRNL != nullptr);

    if (doc.JRNL) {
        doc.JRNL->RefreshActiveRelease();
        CHECK(doc.JRNL->active_release.node != nullptr);
        CHECK_EQ(doc.JRNL->rel_no.size(), std::size_t{2});
        CHECK_EQ(doc.JRNL->rel_no[0], 0);
        CHECK_EQ(doc.JRNL->rel_no[1], 1);
    }

    print_xml("source before journal-aware mutations", doc);
    print_xml("journal before journal-aware mutations", doc.JRNL);

    auto root = require_nodes(doc, "/Root")[0];
    CHECK(root.JRNL == doc.JRNL);

    XmlNode b = root.AddChild("<B/>");
    print_xml("source after root.AddChild(<B/>)", doc);
    print_xml("journal after automatic Add log", doc.JRNL);
    CHECK(b.node != nullptr);
    CHECK(b.JRNL == doc.JRNL);

    b.parse("<B changed=\"true\"/>");
    print_xml("source after b.parse(<B changed=\"true\"/>)", doc);
    print_xml("journal after automatic Modify log", doc.JRNL);
    CHECK(b.node != nullptr);
    CHECK(b.JRNL == doc.JRNL);

    b.Delete();
    print_xml("source after b.Delete()", doc);
    print_xml("journal after automatic Deletion log", doc.JRNL);
    CHECK(b.node == nullptr);
    CHECK(b.doc == nullptr);

    CHECK(doc.JRNL != nullptr);
    doc.JRNL->Save(path);

    XmlDoc journal(path);
    CHECK(!journal.err);
    print_xml("journal reloaded from disk", journal);

    // Changes belong under the deepest open Release, not directly under /JRNL.
    CHECK_EQ(journal.XPath<int>("count(/JRNL/Change)"), 0);
    CHECK_EQ(journal.XPath<int>("count(/JRNL/Release/Release/Change)"), 3);

    CHECK_EQ(journal.XPath<std::string>("/JRNL/Release/Release/Change[1]/@Type"), std::string("Add"));
    CHECK_EQ(journal.XPath<std::string>("/JRNL/Release/Release/Change[2]/@Type"), std::string("Modify"));
    CHECK_EQ(journal.XPath<std::string>("/JRNL/Release/Release/Change[3]/@Type"), std::string("Deletion"));

    // Journal entries should carry a timestamp attribute on <Change>,
    // not an empty child node such as <TimeStamp/>.
    CHECK_EQ(journal.XPath<int>("count(//Change/TimeStamp)"), 0);
    CHECK_EQ(journal.XPath<int>("count(//Change[@TimeStamp and string-length(@TimeStamp) > 0])"), 3);
    CHECK_EQ(journal.XPath<bool>("//Change[2][@Type='Modify'][@TimeStamp]"), true);

    const std::string modifyTimestamp =
        journal.XPath<std::string>("//Change[2]/@TimeStamp");
    CHECK(!modifyTimestamp.empty());
    CHECK(modifyTimestamp.find('T') != std::string::npos);
    CHECK(modifyTimestamp.back() == 'Z');

    std::remove(path);
}

void test_journal_log_modify_jid()
{
    banner("XmlJrnl::LogModify JID");

    static const std::string source_xml =
        "<Root>"
        "  <A JID=\"0123456789abcdef\">old A</A>"
        "  <B>old B</B>"
        "</Root>";

    XmlDoc doc(source_xml);
    CHECK(!doc.err);

    doc.CreateJournal("/tmp/xmlcls_test_jid.jrnl.xml");
    CHECK(doc.JRNL != nullptr);
    CHECK(!doc.JRNL->err);

    XmlJrnl& journal = *doc.JRNL;

    /*
     * ------------------------------------------------------------
     * Existing JID
     * ------------------------------------------------------------
     */
    XmlNode a = require_nodes(doc, "/Root/A")[0];

    CHECK_EQ(a.XPath<std::string>("@JID"),
             std::string("0123456789abcdef"));

    journal.LogModify(a, a.XML());

    print_error("LogModify(A)", journal.err);
    CHECK(!journal.err);

    /*
     * Existing JID must remain unchanged and map to A itself.
     */
    CHECK_EQ(a.XPath<std::string>("@JID"),
             std::string("0123456789abcdef"));

    auto ait = journal.jid_map.find("0123456789abcdef");

    CHECK(ait != journal.jid_map.end());

    if (ait != journal.jid_map.end())
        CHECK(ait->second == a.node);

    /*
     * Change record must identify the transaction by @JID.
     */
    CHECK_EQ(
        journal.active_release.XPath<std::string>(
            "./Change[1]/@JID"),
        std::string("0123456789abcdef")
    );

    /*
     * ------------------------------------------------------------
     * Newly assigned JID
     * ------------------------------------------------------------
     */
    XmlNode b = require_nodes(doc, "/Root/B")[0];

    CHECK_EQ(
        b.XPath<std::vector<XmlNode>>("./@JID").size(),
        std::size_t{0}
    );

    journal.LogModify(b, b.XML());

    print_error("LogModify(B)", journal.err);
    CHECK(!journal.err);

    /*
     * LogModify() must assign B a JID.
     */
    std::string new_jid = b.XPath<std::string>("@JID");

    CHECK(!new_jid.empty());
    CHECK_EQ(new_jid.size(), std::size_t{16});
    CHECK(new_jid != "0123456789abcdef");

    /*
     * Newly created JID must also be indexed to B itself.
     */
    auto bit = journal.jid_map.find(new_jid);

    CHECK(bit != journal.jid_map.end());

    if (bit != journal.jid_map.end())
        CHECK(bit->second == b.node);

    /*
     * The second journal transaction must carry exactly B's new JID
     * as an attribute of Change.
     */
    CHECK_EQ(
        journal.active_release.XPath<std::string>(
            "./Change[2]/@JID"),
        new_jid
    );

    CHECK_EQ(
        journal.active_release.XPath<int>(
            "count(./Change)"),
        2
    );

    print_xml("source after JID tests", doc);
    print_xml("journal after JID tests", doc.JRNL);

    std::remove("/tmp/xmlcls_test_jid.jrnl.xml");
}

void test_journal_undo_modify()
{
    banner("XmlJrnl::Undo Modify");

    const char* path = "/tmp/xmlcls_test_undo_modify.jrnl.xml";

    XmlDoc doc( std::string( "<Root>" "  <A>original</A>" "</Root>" ) );

    CHECK(!doc.err);

    doc.CreateJournal(path);
    CHECK(doc.JRNL != nullptr);
    CHECK(!doc.JRNL->err);

    XmlNode a = doc.XPath<std::vector<XmlNode>>("/Root/A")[0];

    CHECK(!a.XPath<bool>("./@JID"));

    /*
     * parse() should cause LogModify() to assign a JID and preserve
     * that identity on the replacement node.
     */
    a.parse("<A changed=\"true\">modified</A>");

    CHECK(!a.err);
    CHECK(!doc.JRNL->err);

    CHECK_EQ( a.XPath<std::string>("."), std::string("modified") );
    CHECK_EQ( a.XPath<std::string>("@changed"), std::string("true") );

    const std::string jid = a.XPath<std::string>("@JID");

    CHECK(!jid.empty());
    CHECK_EQ(jid.size(), std::size_t{16});

    auto it = doc.JRNL->jid_map.find(jid);

    CHECK(it != doc.JRNL->jid_map.end());

    if (it != doc.JRNL->jid_map.end())
        CHECK(it->second == a.node);

    /*
     * Verify the journal transaction.
     */
    XmlNode change = doc.JRNL->active_release     .XPath<std::vector<XmlNode>>("./Change[last()]")[0];

    CHECK_EQ( change.XPath<std::string>("@Type"), std::string("Modify") );
    CHECK_EQ( change.XPath<std::string>("@JID"), jid );
    CHECK_EQ( change.XPath<std::string>("./Reversed/@Value"), std::string("false") );

    /*
     * Undo the specific Modify transaction.
     */
    doc.JRNL->Undo(change);

    CHECK(!doc.JRNL->err);

    /*
     * Undo replaces the physical xmlNodePtr, so fetch the node again.
     */
    XmlNode restored = doc.XPath<std::vector<XmlNode>>("/Root/A")[0];

    CHECK_EQ( restored.XPath<std::string>("."), std::string("original") );
    CHECK(!restored.XPath<bool>("./@changed"));

    /*
     * Logical identity must survive the round trip.
     */
    CHECK_EQ( restored.XPath<std::string>("@JID"), jid );

    auto restored_it = doc.JRNL->jid_map.find(jid);

    CHECK(restored_it != doc.JRNL->jid_map.end());

    if (restored_it != doc.JRNL->jid_map.end())
        CHECK(restored_it->second == restored.node);

    /*
     * Journal transaction remains, but is marked reversed.
     */
    CHECK_EQ( change.XPath<std::string>("./Reversed/@Value"), std::string("true") );

    const std::string reversed_ts = change.XPath<std::string>("./Reversed/@TimeStamp");

    CHECK(!reversed_ts.empty());
    CHECK(reversed_ts.find('T') != std::string::npos);
    CHECK(reversed_ts.back() == 'Z');

    std::remove(path);
}

void test_journal_delete()
{
    banner("XmlNode::Delete with journal");

    const char* path = "/tmp/xmlcls_test_delete.jrnl.xml";

    XmlDoc doc(std::string(
        "<Root>"
        "  <A/>"
        "  <B>delete me</B>"
        "  <C/>"
        "</Root>"
    ));

    CHECK(!doc.err);

    doc.CreateJournal(path);
    CHECK(doc.JRNL != nullptr);
    CHECK(!doc.JRNL->err);

    XmlNode root = doc.XPath<std::vector<XmlNode>>("/Root")[0];
    XmlNode a = doc.XPath<std::vector<XmlNode>>("/Root/A")[0];
    XmlNode b = doc.XPath<std::vector<XmlNode>>("/Root/B")[0];
    XmlNode c = doc.XPath<std::vector<XmlNode>>("/Root/C")[0];

    /*
     * None of these should require a JID before Delete().
     */
    CHECK(!root.XPath<bool>("./@JID"));
    CHECK(!a.XPath<bool>("./@JID"));
    CHECK(!b.XPath<bool>("./@JID"));
    CHECK(!c.XPath<bool>("./@JID"));

    /*
     * Delete() should assign JIDs to the parent and all siblings/children
     * before LogDelete() records the transaction.
     */
    b.Delete();

    print_error("Delete(B)", b.err);
    CHECK(!b.err);
    CHECK(!doc.JRNL->err);

    /*
     * B is gone from the live DOM.
     */
    CHECK_EQ(doc.XPath<int>("count(/Root/B)"), 0);
    CHECK(b.node == nullptr);
    CHECK(b.doc == nullptr);

    /*
     * Re-fetch surviving nodes because their DOM attributes were modified.
     */
    root = doc.XPath<std::vector<XmlNode>>("/Root")[0];
    a = doc.XPath<std::vector<XmlNode>>("/Root/A")[0];
    c = doc.XPath<std::vector<XmlNode>>("/Root/C")[0];

    const std::string root_jid = root.XPath<std::string>("@JID");
    const std::string a_jid = a.XPath<std::string>("@JID");
    const std::string c_jid = c.XPath<std::string>("@JID");

    CHECK(!root_jid.empty());
    CHECK(!a_jid.empty());
    CHECK(!c_jid.empty());

    CHECK_EQ(root_jid.size(), std::size_t{16});
    CHECK_EQ(a_jid.size(), std::size_t{16});
    CHECK_EQ(c_jid.size(), std::size_t{16});

    /*
     * The deletion transaction itself preserves B's JID.
     */
    XmlNode change = doc.JRNL->active_release.XPath<std::vector<XmlNode>>("./Change[last()]")[0];

    CHECK_EQ(change.XPath<std::string>("@Type"), std::string("Deletion"));
    std::string xml = change.XML();
    printf("Deletion transaction XML:\n%s\n", xml.c_str());

    const std::string b_jid = change.XPath<std::string>("@JID");

    CHECK(!b_jid.empty());
    CHECK_EQ(b_jid.size(), std::size_t{16});

    /*
     * Parent and immediate sibling identities describe B's original slot.
     */
    CHECK_EQ(change.XPath<std::string>("./Parent/@JID"), root_jid);
    CHECK_EQ(change.XPath<std::string>("./Before/@JID"), a_jid);
    CHECK_EQ(change.XPath<std::string>("./After/@JID"), c_jid);

    /*
     * All four JIDs must remain reserved by the journal.
     */
    CHECK(doc.JRNL->jid_map.find(root_jid) != doc.JRNL->jid_map.end());
    CHECK(doc.JRNL->jid_map.find(a_jid) != doc.JRNL->jid_map.end());
    CHECK(doc.JRNL->jid_map.find(b_jid) != doc.JRNL->jid_map.end());
    CHECK(doc.JRNL->jid_map.find(c_jid) != doc.JRNL->jid_map.end());

    CHECK(doc.JRNL->jid_map[root_jid] == root.node);
    CHECK(doc.JRNL->jid_map[a_jid] == a.node);
    CHECK(doc.JRNL->jid_map[c_jid] == c.node);

    /*
     * Deleted identity remains reserved but has no live source node.
     */
    CHECK(doc.JRNL->jid_map[b_jid] == nullptr);

    /*
     * Saved XML must contain the deleted logical node, including its JID.
     */
    const std::string encoded = change.XPath<std::string>("./Node");
    CHECK(!encoded.empty());

    const std::string deleted_xml = base64_decode(encoded);

    CHECK(deleted_xml.find("<B") != std::string::npos);
    CHECK(deleted_xml.find("delete me") != std::string::npos);
    CHECK(deleted_xml.find("JID=\"" + b_jid + "\"") != std::string::npos);

    /*
     * New deletion transaction begins unreversed.
     */
    CHECK_EQ(change.XPath<std::string>("./Reversed/@Value"), std::string("false"));

    print_xml("source after journaled Delete(B)", doc);
    print_xml("journal after journaled Delete(B)", doc.JRNL);

    std::remove(path);
}

} // namespace

int main()
{
    xmlInitParser();

    test_document_xpath();
    test_add_child_before_after_and_vectors();
    test_parse_replace_node();
    test_delete_node();
    test_save_and_reload();
    test_xmljrnl_constructor_and_active_release();
    test_journal_log_modify_jid();
    test_journal_undo_modify();
    test_journal_aware_mutations();
    test_journal_undo_modify();
    test_journal_delete();

    xmlCleanupParser();

    std::cout << "\n========== summary ==========" << '\n';
    std::cout << passes << " check(s) passed.\n";

    if (failures) {
        std::cerr << failures << " check(s) failed.\n";
        return EXIT_FAILURE;
    }

    std::cout << "SUCCESS: All XmlCls tests passed.\n";
    return EXIT_SUCCESS;
}
