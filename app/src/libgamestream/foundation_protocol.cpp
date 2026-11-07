#include "foundation_protocol.hpp"

#include <expat.h>
#include <jansson.h>

#include <charconv>
#include <memory>
#include <set>
#include <utility>

namespace foundation {
namespace {
constexpr size_t MAX_RESPONSE_BYTES = 1024 * 1024;
constexpr size_t MAX_XML_NODES = 32768;
constexpr size_t MAX_XML_DEPTH = 32;
constexpr size_t MAX_COMMANDS = 100;

struct Node {
    std::string name;
    std::string text;
    std::string status;
    std::string message;
    std::vector<std::unique_ptr<Node>> children;
};

struct Document {
    XML_Parser parser = nullptr;
    std::unique_ptr<Node> root;
    std::vector<Node*> stack;
    size_t nodes = 0;
    bool forbidden = false;
};

void stop(Document& document) {
    document.forbidden = true;
    XML_StopParser(document.parser, XML_FALSE);
}

void XMLCALL startElement(void* context, const char* name, const char** attributes) {
    auto& document = *static_cast<Document*>(context);
    if (++document.nodes > MAX_XML_NODES || document.stack.size() >= MAX_XML_DEPTH) {
        stop(document);
        return;
    }
    auto node = std::make_unique<Node>();
    node->name = name;
    for (size_t i = 0; attributes[i]; i += 2) {
        const std::string key = attributes[i];
        if (key == "status_code") node->status = attributes[i + 1];
        if (key == "status_message") node->message = attributes[i + 1];
    }
    Node* current = node.get();
    if (document.stack.empty()) {
        document.root = std::move(node);
    } else {
        document.stack.back()->children.push_back(std::move(node));
    }
    document.stack.push_back(current);
}

void XMLCALL endElement(void* context, const char*) {
    auto& document = *static_cast<Document*>(context);
    if (!document.stack.empty()) document.stack.pop_back();
}

void XMLCALL characterData(void* context, const char* text, int length) {
    auto& document = *static_cast<Document*>(context);
    if (!document.stack.empty()) document.stack.back()->text.append(text, length);
}

void XMLCALL startDoctype(void* context, const char*, const char*, const char*, int) {
    stop(*static_cast<Document*>(context));
}

std::string trim(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    return value.substr(begin, value.find_last_not_of(" \t\r\n") - begin + 1);
}

bool parseDocument(const std::string& xml, Document& document, std::string& error) {
    error.clear();
    if (xml.empty() || xml.size() > MAX_RESPONSE_BYTES) {
        error = "Empty or oversized host response";
        return false;
    }
    document.parser = XML_ParserCreate("UTF-8");
    if (!document.parser) {
        error = "Unable to create XML parser";
        return false;
    }
    XML_SetUserData(document.parser, &document);
    XML_SetElementHandler(document.parser, startElement, endElement);
    XML_SetCharacterDataHandler(document.parser, characterData);
    XML_SetStartDoctypeDeclHandler(document.parser, startDoctype);
    XML_SetParamEntityParsing(document.parser, XML_PARAM_ENTITY_PARSING_NEVER);
    const bool parsed = XML_Parse(document.parser, xml.data(),
                                  static_cast<int>(xml.size()), XML_TRUE) == XML_STATUS_OK;
    XML_ParserFree(document.parser);
    document.parser = nullptr;
    if (!parsed || document.forbidden || !document.root || document.root->name != "root") {
        error = "Invalid host XML response";
        return false;
    }
    if (document.root->status != "200") {
        error = "Host rejected request";
        if (!document.root->message.empty())
            error += ": " + document.root->message.substr(0, 256);
        return false;
    }
    return true;
}

const Node* child(const Node& node, const std::string& name, bool& duplicate) {
    const Node* result = nullptr;
    for (const auto& item : node.children) {
        if (item->name != name) continue;
        if (result) duplicate = true;
        result = item.get();
    }
    return result;
}

bool appIdentifier(const Node& app, int& value) {
    bool duplicate = false;
    const auto* id = child(app, "ID", duplicate);
    if (!id || duplicate || !id->children.empty()) return false;
    const auto text = trim(id->text);
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc() && parsed.ptr == text.data() + text.size() && value > 0;
}

bool commandString(json_t* object, const char* key, size_t limit, std::string& value) {
    const auto* field = json_object_get(object, key);
    if (!json_is_string(field)) return false;
    const auto length = json_string_length(field);
    if (length == 0 || length > limit) return false;
    value.assign(json_string_value(field), length);
    return value.find('\0') == std::string::npos && !trim(value).empty();
}
} // namespace

std::string encodeQuery(const std::string& value) {
    static constexpr char HEX[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(value.size());
    for (unsigned char byte : value) {
        if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
            (byte >= '0' && byte <= '9') || byte == '-' || byte == '.' ||
            byte == '_' || byte == '~') {
            result.push_back(static_cast<char>(byte));
        } else {
            result.push_back('%');
            result.push_back(HEX[byte >> 4]);
            result.push_back(HEX[byte & 15]);
        }
    }
    return result;
}

bool parseActionResponse(const std::string& xml, const std::string& action,
                         std::string& error) {
    Document document;
    if (!parseDocument(xml, document, error)) return false;
    bool duplicate = false;
    const auto* result = child(*document.root, action, duplicate);
    if (action.empty() || !result || duplicate || !result->children.empty() ||
        trim(result->text) != "1") {
        error = "Host did not confirm the requested action";
        return false;
    }
    return true;
}

bool parseBitrateResponse(const std::string& xml, int requestedKbps,
                          int& appliedKbps, std::string& error) {
    appliedKbps = 0;
    error.clear();
    if (requestedKbps < 1000 || requestedKbps > 100000) {
        error = "Invalid requested bitrate";
        return false;
    }
    Document document;
    if (!parseDocument(xml, document, error)) return false;
    bool duplicate = false;
    const auto* result = child(*document.root, "bitrate", duplicate);
    if (!result || duplicate || !result->children.empty()) {
        error = "Host did not confirm the requested bitrate";
        return false;
    }
    const auto text = trim(result->text);
    int value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc() || parsed.ptr != text.data() + text.size() ||
        (value != 1 && (value < 1000 || value > 500000))) {
        error = "Host returned an invalid applied bitrate";
        return false;
    }
    appliedKbps = value == 1 ? requestedKbps : value;
    return true;
}

bool parseCommands(const std::string& xml, int appId,
                   std::vector<Command>& commands, std::string& error) {
    commands.clear();
    Document document;
    if (!parseDocument(xml, document, error)) return false;
    const Node* selected = nullptr;
    for (const auto& item : document.root->children) {
        if (item->name != "App") continue;
        int id = 0;
        if (!appIdentifier(*item, id) || id != appId) continue;
        if (selected) {
            error = "Duplicate application in host response";
            return false;
        }
        selected = item.get();
    }
    if (!selected) {
        error = "Application not found in host response";
        return false;
    }
    bool duplicate = false;
    const auto* advertised = child(*selected, "SuperCmds", duplicate);
    if (duplicate || (advertised && !advertised->children.empty())) {
        error = "Invalid host command list";
        return false;
    }
    if (!advertised) return true;
    const auto text = trim(advertised->text);
    if (text.empty() || text == "null") return true;

    json_error_t jsonError{};
    std::unique_ptr<json_t, decltype(&json_decref)> array(
        json_loadb(text.data(), text.size(), JSON_REJECT_DUPLICATES, &jsonError), json_decref);
    if (!array || !json_is_array(array.get()) || json_array_size(array.get()) > MAX_COMMANDS) {
        error = "Invalid host command list";
        return false;
    }
    std::vector<Command> parsed;
    std::set<std::string> ids;
    for (size_t i = 0; i < json_array_size(array.get()); ++i) {
        auto* object = json_array_get(array.get(), i);
        Command command;
        if (!json_is_object(object) || !commandString(object, "id", 256, command.id) ||
            !commandString(object, "name", 1024, command.name) || !ids.insert(command.id).second) {
            error = "Invalid host command entry";
            return false;
        }
        parsed.push_back(std::move(command));
    }
    commands = std::move(parsed);
    return true;
}
} // namespace foundation
