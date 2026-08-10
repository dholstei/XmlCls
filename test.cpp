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
    banner("XmlJrnl constructor / active Release branch");

    static const char journal_xml[] =
        "<JRNL>"
        "  <Release Number=\"0\" Open=\"2026-01-01T00:00:00Z\" Close=\"\">"
        "    <Release Number=\"1\" Open=\"2026-01-01T00:00:00Z\" Close=\"\">"
        "    </Release>"
        "  </Release>"
        "</JRNL>";

    XmlJrnl journal((std::string(journal_xml)));
    CHECK(!journal.err);

    journal.RefreshActiveRelease();
    print_error("RefreshActiveRelease", journal.err);
    CHECK(!journal.err);
    CHECK(journal.active_release.node != nullptr);
    CHECK_EQ(journal.rel_no.size(), std::size_t{2});
    CHECK_EQ(journal.rel_no[0], 0);
    CHECK_EQ(journal.rel_no[1], 1);
    CHECK_EQ(journal.active_release.XPath<int>("number(@Number)"), 1);
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
    test_journal_aware_mutations();

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
