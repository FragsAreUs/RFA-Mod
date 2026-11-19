// Guerrilla's tables (.xtbl): XML, <root><Table> then one element per entry, each entry's fields as
// child elements (a field may itself hold a list of elements).
#pragma once
#include <string>
#include <vector>

namespace campaign::tables {

struct Element {
    std::string name;
    std::string text;
    std::vector<Element> children;

    // The first child of that name (any case), or null.
    const Element* Child(const char* childName) const;
    // Every child of that name (any case).
    std::vector<const Element*> Children(const char* childName) const;
    // A child's text, or `fallback` if there is no such child.
    std::string Text(const char* childName, const char* fallback = "") const;
    float Number(const char* childName, float fallback = 0.0f) const;
    int Int(const char* childName, int fallback = 0) const;
    // "true"/"false" (any case), "1"/"0".
    bool Flag(const char* childName, bool fallback = false) const;
};

// Reads mods\guerrilla_campaign\tables\<name>.xtbl (copied there from Guerrilla's misc.vpp_pc).
// Gives the entries under <Table>; false, and a line in the log, if the table isn't there or
// can't be read.
bool Load(const char* name, std::vector<Element>& entries);

// Parses XML text (elements and their text; comments, declarations and attributes are skipped,
// the five standard entities decoded). False if it isn't well formed.
bool ParseXml(const std::string& text, Element& root);

}  // namespace campaign::tables
