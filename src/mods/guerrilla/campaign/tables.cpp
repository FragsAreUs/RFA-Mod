// Reading Guerrilla's tables. The mod reads them from its own folder, where they are copied from
// the player's own copy of Guerrilla (misc.vpp_pc holds all 305 of Guerrilla's tables).
#define RFA_MOD_NAME "guerrilla_campaign"
#include "rfa_mod.h"
#include "tables.h"
#include "campaign.h"
#include <stdlib.h>
#include <string.h>

namespace campaign::tables {
namespace {

class Parser {
public:
    explicit Parser(const std::string& text) : s_(text) {}

    bool Document(Element& root) {
        SkipMisc();
        if (!ElementAt(root)) return false;
        return true;
    }

private:
    const std::string& s_;
    size_t i_ = 0;

    bool At(const char* literal) const { return s_.compare(i_, strlen(literal), literal) == 0; }

    void SkipSpace() {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\r' || s_[i_] == '\n')) ++i_;
    }

    // Comments, <?xml ...?> declarations and <!DOCTYPE ...>.
    void SkipMisc() {
        for (;;) {
            SkipSpace();
            if (At("<!--")) {
                size_t end = s_.find("-->", i_ + 4);
                i_ = end == std::string::npos ? s_.size() : end + 3;
            } else if (At("<?")) {
                size_t end = s_.find("?>", i_ + 2);
                i_ = end == std::string::npos ? s_.size() : end + 2;
            } else if (At("<!")) {
                size_t end = s_.find('>', i_ + 2);
                i_ = end == std::string::npos ? s_.size() : end + 1;
            } else {
                return;
            }
        }
    }

    static void Decode(std::string& text) {
        static const struct { const char* entity; char c; } kEntities[] = {
            {"&lt;", '<'}, {"&gt;", '>'}, {"&amp;", '&'}, {"&quot;", '"'}, {"&apos;", '\''}};
        for (size_t at = 0; (at = text.find('&', at)) != std::string::npos; ++at) {
            for (const auto& e : kEntities) {
                if (text.compare(at, strlen(e.entity), e.entity) == 0) {
                    text.replace(at, strlen(e.entity), 1, e.c);
                    break;
                }
            }
        }
    }

    static std::string Trim(const std::string& text) {
        size_t a = text.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return {};
        size_t b = text.find_last_not_of(" \t\r\n");
        return text.substr(a, b - a + 1);
    }

    bool ElementAt(Element& element) {
        if (!At("<")) return false;
        ++i_;
        size_t nameEnd = s_.find_first_of(" \t\r\n/>", i_);
        if (nameEnd == std::string::npos) return false;
        element.name = s_.substr(i_, nameEnd - i_);
        size_t tagEnd = s_.find('>', nameEnd);
        if (tagEnd == std::string::npos) return false;
        const bool empty = s_[tagEnd - 1] == '/';
        i_ = tagEnd + 1;
        if (empty) return true;
        std::string text;
        for (;;) {
            if (i_ >= s_.size()) return false;
            if (At("<!--") || At("<?")) {
                SkipMisc();
                continue;
            }
            if (At("<![CDATA[")) {
                size_t end = s_.find("]]>", i_);
                if (end == std::string::npos) return false;
                text += s_.substr(i_ + 9, end - i_ - 9);
                i_ = end + 3;
                continue;
            }
            if (At("</")) {
                size_t end = s_.find('>', i_);
                if (end == std::string::npos) return false;
                i_ = end + 1;
                Decode(text);
                element.text = Trim(text);
                return true;
            }
            if (At("<")) {
                element.children.emplace_back();
                if (!ElementAt(element.children.back())) return false;
                continue;
            }
            size_t next = s_.find('<', i_);
            if (next == std::string::npos) return false;
            text += s_.substr(i_, next - i_);
            i_ = next;
        }
    }
};

}  // namespace

const Element* Element::Child(const char* childName) const {
    for (const Element& child : children)
        if (_stricmp(child.name.c_str(), childName) == 0) return &child;
    return nullptr;
}

std::vector<const Element*> Element::Children(const char* childName) const {
    std::vector<const Element*> found;
    for (const Element& child : children)
        if (_stricmp(child.name.c_str(), childName) == 0) found.push_back(&child);
    return found;
}

std::string Element::Text(const char* childName, const char* fallback) const {
    const Element* child = Child(childName);
    return child ? child->text : fallback;
}

float Element::Number(const char* childName, float fallback) const {
    const Element* child = Child(childName);
    return child && !child->text.empty() ? static_cast<float>(atof(child->text.c_str())) : fallback;
}

int Element::Int(const char* childName, int fallback) const {
    const Element* child = Child(childName);
    return child && !child->text.empty() ? atoi(child->text.c_str()) : fallback;
}

bool Element::Flag(const char* childName, bool fallback) const {
    const Element* child = Child(childName);
    if (!child || child->text.empty()) return fallback;
    return _stricmp(child->text.c_str(), "true") == 0 || child->text == "1";
}

bool ParseXml(const std::string& text, Element& root) {
    root = {};
    return Parser(text).Document(root);
}

bool Load(const char* name, std::vector<Element>& entries) {
    entries.clear();
    const std::wstring wide(name, name + strlen(name));
    const std::wstring path = ModFolder() + L"\\tables\\" + wide + L".xtbl";
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        rfa::Log("Guerrilla's table %s.xtbl isn't in mods\\guerrilla_campaign\\tables", name);
        return false;
    }
    std::string text;
    LARGE_INTEGER size{};
    GetFileSizeEx(file, &size);
    text.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    const bool ok = text.empty() || ReadFile(file, &text[0], static_cast<DWORD>(text.size()), &read, nullptr);
    CloseHandle(file);
    Element root;
    if (!ok || !ParseXml(text, root)) {
        rfa::Log("Guerrilla's table %s.xtbl couldn't be read", name);
        return false;
    }
    const Element* table = root.Child("Table");
    if (!table) {
        rfa::Log("Guerrilla's table %s.xtbl has no <Table>", name);
        return false;
    }
    entries = table->children;
    return true;
}

}  // namespace campaign::tables
