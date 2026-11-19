// Guerrilla's campaign markers, from the world converter's campaign_objects.xml.
#define RFA_MOD_NAME "guerrilla_campaign"
#include "rfa_mod.h"
#include "world_objects.h"
#include "tables.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace campaign::world {
namespace {

std::vector<Object> g_objects;
bool g_loaded;

unsigned Hex(const std::string& text) {
    return text.empty() ? 0xFFFFFFFF : static_cast<unsigned>(strtoul(text.c_str(), nullptr, 16));
}

// Everything is a child element (the table reader keeps no attributes).
Object FromElement(const tables::Element& element) {
    Object object;
    object.objectClass = element.Text("class");
    object.handle = Hex(element.Text("handle"));
    object.parent = Hex(element.Text("parent"));
    object.zone = element.Text("zone");
    sscanf_s(element.Text("pos").c_str(), "%f %f %f", &object.pos.x, &object.pos.y, &object.pos.z);
    object.hasParentPos = sscanf_s(element.Text("parent_pos").c_str(), "%f %f %f", &object.parentPos.x, &object.parentPos.y, &object.parentPos.z) == 3;
    object.hasParentCenter = sscanf_s(element.Text("parent_center").c_str(), "%f %f %f", &object.parentCenter.x, &object.parentCenter.y, &object.parentCenter.z) == 3;
    object.hasParentSize = sscanf_s(element.Text("parent_size").c_str(), "%f %f %f", &object.parentSize.x, &object.parentSize.y, &object.parentSize.z) == 3;
    object.hasAreaCenter =sscanf_s(element.Text("area_center").c_str(), "%f %f %f", &object.areaCenter.x, &object.areaCenter.y, &object.areaCenter.z) == 3;
    for (const tables::Element* property : element.Children("property"))
        object.properties.emplace_back(property->Text("name"), property->Text("value"));
    return object;
}

}  // namespace

std::string Object::Property(const char* name, const char* fallback) const {
    for (const auto& property : properties)
        if (_stricmp(property.first.c_str(), name) == 0) return property.second;
    return fallback;
}

bool Load() {
    if (g_loaded) return !g_objects.empty();
    g_loaded = true;
    const std::wstring path = ModFolder() + L"\\world\\campaign_objects.xml";
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || !file) {
        rfa::Log("No campaign markers (mods\\guerrilla_campaign\\world\\campaign_objects.xml); the activities have no nodes");
        return false;
    }
    std::string text;
    char buffer[65536];
    size_t read;
    while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0) text.append(buffer, read);
    fclose(file);
    tables::Element root;
    if (!tables::ParseXml(text, root)) {
        rfa::Log("campaign_objects.xml couldn't be read");
        return false;
    }
    for (const tables::Element* element : root.Children("object")) g_objects.push_back(FromElement(*element));
    rfa::Log("%u campaign markers read", static_cast<unsigned>(g_objects.size()));
    return true;
}

std::vector<const Object*> OfClass(const char* objectClass) {
    std::vector<const Object*> found;
    for (const Object& object : g_objects)
        if (_stricmp(object.objectClass.c_str(), objectClass) == 0) found.push_back(&object);
    return found;
}

const Object* Find(unsigned handle) {
    for (const Object& object : g_objects)
        if (object.handle == handle) return &object;
    return nullptr;
}

const Object* Near(float x, float z, float within) {
    const Object* best = nullptr;
    float bestDistance = within * within;
    for (const Object& object : g_objects) {
        const float dx = object.pos.x - x, dz = object.pos.z - z;
        const float distance = dx * dx + dz * dz;
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = &object;
        }
    }
    return best;
}

}  // namespace campaign::world
