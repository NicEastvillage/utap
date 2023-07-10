#ifndef INCLUDE_UTAP_DOCUMENT_FIXTURE_HPP
#define INCLUDE_UTAP_DOCUMENT_FIXTURE_HPP

#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <cstring>

#include <utap/StatementBuilder.hpp>
#include <utap/property.h>
#include <utap/typechecker.h>
#include <utap/utap.h>

inline std::string read_content(const std::string& file_name)
{
    const auto path = std::filesystem::path{MODELS_DIR} / file_name;
    auto ifs = std::ifstream{path};
    if (ifs.fail())
        throw std::system_error{errno, std::system_category(), "Failed to open " + path.string()};
    auto content = std::string{std::istreambuf_iterator<char>{ifs}, std::istreambuf_iterator<char>{}};
    if (content.empty())
        throw std::runtime_error("No data was read from model file");
    return content;
}

std::unique_ptr<UTAP::Document> read_document(const std::string& file_name)
{
    auto doc = std::make_unique<UTAP::Document>();
    auto res = parse_XML_buffer(read_content(file_name).c_str(), doc.get(), true);
    if (res != 0)
        throw std::logic_error("Failed to parse document");
    return doc;
}

template <typename... Args>
std::string string_format(const std::string& format, Args... args)
{
    using namespace std::string_literals;
    auto size = std::snprintf(nullptr, 0, format.c_str(), args...);
    if (size <= 0)
        throw std::logic_error("Failed to format: "s + std::strerror(errno));
    auto res = std::string(size, ' ');
    if (auto s = std::snprintf(&res[0], size + 1, format.c_str(), args...); s != size)
        throw std::logic_error("Failed to format: "s + std::strerror(errno));
    return res;
}

std::string replace_all(std::string text, const std::string& what, const std::string& with)
{
    for (auto pos = text.find(what); pos != std::string::npos; pos = text.find(what, pos + 1))
        text.replace(pos, what.length(), with);
    return std::move(text);
}

std::string escape_xml(std::string text)
{
    text = replace_all(std::move(text), "&", "&amp;");
    text = replace_all(std::move(text), "<", "&lt;");
    text = replace_all(std::move(text), ">", "&gt;");
    return std::move(text);
}

class template_fixture
{
    std::string name;
    std::string parameters{};
    std::string declarations{};

public:
    template_fixture(std::string name): name{std::move(name)} {}
    void set_name(std::string new_name) { name = std::move(new_name); }
    template_fixture& add_parameter(std::string param)
    {
        if (!parameters.empty())
            parameters += ", ";
        parameters += escape_xml(std::move(param));
        return *this;
    }
    template_fixture& add_declaration(std::string text)
    {
        if (!declarations.empty())
            declarations += '\n';
        declarations += escape_xml(std::move(text));
        return *this;
    }
    std::string str() const
    {
        static constexpr const char* simple_template = R"XML("<template>
        <name x="5" y="5">%s</name>
        <parameter>%s</parameter>
        <declaration>%s</declaration>
        <location id="id0" x="0" y="0"/>
        <init ref="id0"/>
    </template>")XML";
        return string_format(simple_template, name.c_str(), parameters.c_str(), declarations.c_str());
    }

private:
};

class QueryBuilder : public UTAP::StatementBuilder
{
    UTAP::expression_t query;
    UTAP::TypeChecker checker;

public:
    explicit QueryBuilder(UTAP::Document& doc): UTAP::StatementBuilder{doc}, checker{doc} {}
    void property() override
    {
        if (fragments.size() == 0)
            throw std::logic_error("No query fragments after building query");

        query = fragments[0];
        fragments.pop();
    }
    void strategy_declaration(const char* strategy_name) override {}
    void typecheck() { checker.checkExpression(query); }
    [[nodiscard]] UTAP::expression_t getQuery() const { return query; }
    UTAP::variable_t* addVariable(UTAP::type_t type, const std::string& name, UTAP::expression_t init,
                                  UTAP::position_t pos) override
    {
        throw UTAP::NotSupportedException(__FUNCTION__);
    }
    bool addFunction(UTAP::type_t type, const std::string& name, UTAP::position_t pos) override
    {
        throw UTAP::NotSupportedException(__FUNCTION__);
    }
};

class QueryFixture
{
    std::unique_ptr<UTAP::Document> doc;
    UTAP::TigaPropertyBuilder query_builder;

public:
    QueryFixture(std::unique_ptr<UTAP::Document> new_doc): doc{std::move(new_doc)}, query_builder{*doc} {}
    auto get_errors() const { return doc->get_errors(); }
    [[nodiscard]] const UTAP::PropInfo& parse_query(std::string_view query)
    {
        if (parseProperty(query.data(), &query_builder) == -1 || !doc->get_errors().empty()) {
            if (doc->get_errors().empty())
                throw std::logic_error("Query parsing failed with no errors");
            else
                throw std::logic_error(doc->get_errors()[0].msg);
        }
        return query_builder.getProperties().back();
    }
};

class document_fixture
{
    std::string global_decls{};
    std::string templates{};
    std::string system_decls{};
    std::string processes{};

public:
    /** Adds text at the global declarations section of the document */
    document_fixture& add_global_decl(std::string text)
    {
        if (!global_decls.empty())
            global_decls += '\n';
        global_decls += escape_xml(std::move(text));
        return *this;
    }
    /** Adds a template text, @see template_fixture::str() */
    document_fixture& add_template(std::string text)
    {
        templates += '\n';
        templates += std::move(text);
        return *this;
    }
    /** Adds a default template and default process just like default initial document */
    document_fixture& add_default_process()
    {
        add_template(template_fixture("Template").str());
        add_system_decl("Process = Template();");
        add_process("Process");
        return *this;
    }
    /** Adds text to system declarations */
    document_fixture& add_system_decl(std::string text)
    {
        system_decls += '\n';
        system_decls += escape_xml(std::move(text));
        return *this;
    }
    /** Adds a process to the system */
    document_fixture& add_process(std::string name)
    {
        if (!processes.empty())
            processes += ", ";
        processes += std::move(name);
        return *this;
    }
    /** Compiles an XML string representation of a document */
    std::string str() const
    {
        static constexpr const char* document_template = R"XML(<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE nta PUBLIC '-//Uppaal Team//DTD Flat System 1.5//EN' 'http://www.it.uu.se/research/group/darts/uppaal/flat-1_5.dtd'>
<nta>
    <declaration>%s</declaration>%s
    <system>%s
system %s;
    </system>
</nta>
)XML";
        return string_format(document_template, global_decls.c_str(), templates.c_str(), system_decls.c_str(),
                             processes.c_str());
    }

    /** Derives a document from a document template and parses it */
    [[nodiscard]] std::unique_ptr<UTAP::Document> parse() const
    {
        auto doc = std::make_unique<UTAP::Document>();
        auto data = str();
        parse_XML_buffer(data.c_str(), doc.get(), true);
        return doc;
    }

    [[nodiscard]] QueryFixture build_query_fixture() const { return QueryFixture(parse()); }
};

#endif  // INCLUDE_UTAP_DOCUMENT_FIXTURE_HPP
