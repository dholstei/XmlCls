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
 * These tests intentionally exercise the newest DOM mutation methods:
 * XmlNode::AddChild(), AddBefore(), AddAfter(), parse(), Delete(), and the
 * XmlDoc journal helpers.  They also verify that XPath evaluation still works
 * after each mutation.
 */

#include "XmlCls.h"

#include <cstdlib>
#include <fstream>
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

void print_xml(const std::string& label, XmlDoc& doc)
{
    std::cout << "\n--- " << label << " ---\n";
    std::cout << doc.XML() << '\n';
}

void print_xml(const std::string& label, XmlDoc* doc)
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

    static const char xml[] =
        "<Root>"
        "  <Item Name=\"alpha\" Value=\"10\">A</Item>"
        "  <Item Name=\"beta\"  Value=\"20\">B</Item>"
        "</Root>";

    XmlDoc doc(xml, static_cast<int>(sizeof(xml) - 1));
    CHECK(!doc.err);
    print_xml("source XML", doc);

    CHECK_EQ(doc.XPath<std::string>("string(/Root/Item[@Name='alpha'])"), std::string("A"));
    CHECK_EQ(doc.XPath<int>("count(/Root/Item)"), 2);
    CHECK_EQ(doc.XPath<double>("number(/Root/Item[@Name='beta']/@Value)"), 20.0);
    CHECK_EQ(doc.XPath<bool>("boolean(/Root/Item[@Name='beta'])"), true);

    auto items = doc.XPath<std::vector<XmlNode>>("/Root/Item");
    CHECK_EQ(items.size(), std::size_t{2});
}

void test_add_child_before_after_and_vectors()
{
    banner("AddChild / AddBefore / AddAfter / vector overloads");

    XmlDoc doc("<Root><A id=\"1\"/></Root>", 25);
    CHECK(!doc.err);
    print_xml("before mutations", doc);

    auto root = require_nodes(doc, "/Root")[0];
    XmlNode b = root.AddChild("<B id=\"2\"><Leaf>ok</Leaf></B>");
    print_xml("after AddChild(<B...>)", doc);
    CHECK(b.node != nullptr);
    CHECK_EQ(doc.XPath<int>("count(/Root/B)"), 1);
    CHECK_EQ(b.XPath<std::string>("string(./Leaf)"), std::string("ok"));

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

    XmlDoc doc("<Root><Old id=\"1\">old</Old><Tail/></Root>", 43);
    CHECK(!doc.err);
    print_xml("before parse()", doc);

    auto old = require_nodes(doc, "/Root/Old")[0];
    old.parse("<New id=\"2\">new</New>");
    print_xml("after parse(<New...>)", doc);
    print_error("parse", old.err);
    CHECK(!old.err);
    CHECK(old.node != nullptr);

    CHECK_EQ(doc.XPath<int>("count(/Root/Old)"), 0);
    CHECK_EQ(doc.XPath<int>("count(/Root/New)"), 1);
    CHECK_EQ(doc.XPath<std::string>("string(/Root/New)"), std::string("new"));
    CHECK_EQ(doc.XPath<std::string>("name(/Root/*[1])"), std::string("New"));
    CHECK_EQ(doc.XPath<std::string>("name(/Root/*[2])"), std::string("Tail"));
}

void test_delete_node()
{
    banner("Delete node");

    XmlDoc doc("<Root><A/><B/><C/></Root>", 25);
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

    XmlDoc doc("<Root><A>saved</A></Root>", 25);
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
    CHECK_EQ(reloaded.XPath<std::string>("string(/Root/A)"), std::string("saved"));
    CHECK_EQ(reloaded.XPath<std::string>("string(/Root/B)"), std::string("added"));

    std::remove(path);
}

void test_journal_helpers()
{
    banner("journal helpers");

    const char* path = "/tmp/xmlcls_test.jrnl.xml";

    XmlDoc doc("<Root><A/></Root>", 17);
    CHECK(!doc.err);
    doc.CreateJournal(path);
    CHECK(doc.JRNL != nullptr);

    print_xml("source before journaled mutations", doc);
    print_xml("journal before journaled mutations", doc.JRNL);

    auto root = require_nodes(doc, "/Root")[0];
    XmlNode b = root.AddChild("<B/>");
    JRNL::Add(b);
    print_xml("source after AddChild(<B/>)", doc);
    print_xml("journal after JRNL::Add(B)", doc.JRNL);

    std::string old = std::string(b);
    b.parse("<B changed=\"true\"/>");
    JRNL::Modify(b, old);
    print_xml("source after parse(<B changed=\"true\"/>)", doc);
    print_xml("journal after JRNL::Modify(B, oldXML)", doc.JRNL);

    JRNL::Delete(b);
    b.Delete();
    print_xml("source after JRNL::Delete(B), then B.Delete()", doc);
    print_xml("journal after JRNL::Delete(B)", doc.JRNL);

    doc.JRNL->Save();
    XmlDoc journal(path);
    CHECK(!journal.err);
    print_xml("journal reloaded from disk", journal);
    CHECK_EQ(journal.XPath<int>("count(/JRNL/Change)"), 3);
    CHECK_EQ(journal.XPath<std::string>("string(/JRNL/Change[1]/@Type)"), std::string("Add"));
    CHECK_EQ(journal.XPath<std::string>("string(/JRNL/Change[2]/@Type)"), std::string("Modify"));
    CHECK_EQ(journal.XPath<std::string>("string(/JRNL/Change[3]/@Type)"), std::string("Deletion"));

    // Journal entries should carry a timestamp attribute on <Change>,
    // not an empty child node such as <TimeStamp/>.
    CHECK_EQ(journal.XPath<int>("count(/JRNL/Change/TimeStamp)"), 0);
    CHECK_EQ(journal.XPath<int>("count(/JRNL/Change[@TimeStamp and string-length(@TimeStamp) > 0])"), 3);
    CHECK_EQ(journal.XPath<bool>("boolean(/JRNL/Change[2][@Type='Modify'][@TimeStamp])"), true);

    const std::string modifyTimestamp =
        journal.XPath<std::string>("string(/JRNL/Change[2]/@TimeStamp)");
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
    test_journal_helpers();

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
