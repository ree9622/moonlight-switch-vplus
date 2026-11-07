#include "foundation_protocol.hpp"

#include <cstdlib>
#include <iostream>

namespace {
int checks = 0;
void expect(bool condition, const char* description) {
    ++checks;
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        std::exit(1);
    }
}
std::string envelope(const std::string& body) {
    return "<root status_code=\"200\">" + body + "</root>";
}
std::string app(int id, const std::string& commands) {
    return "<App><ID>" + std::to_string(id) + "</ID><SuperCmds>" + commands + "</SuperCmds></App>";
}
}

int main() {
    std::string error;
    using foundation::parseActionResponse;
    expect(parseActionResponse(envelope("<bitrate>1</bitrate>"), "bitrate", error), "confirmed bitrate");
    expect(error.empty(), "success clears stale error");
    expect(parseActionResponse(envelope("<supercmd> 1\n</supercmd>"), "supercmd", error), "confirmed command");
    for (const auto& body : {"", "<bitrate/>", "<bitrate>0</bitrate>", "<bitrate>2</bitrate>",
             "<bitrate>yes</bitrate>", "<resume>1</resume>", "<other><bitrate>1</bitrate></other>",
             "<bitrate><nested/>1</bitrate>", "<bitrate>1</bitrate><bitrate>1</bitrate>"}) {
        expect(!parseActionResponse(envelope(body), "bitrate", error), "unconfirmed action rejected");
        expect(!error.empty(), "rejection supplies error");
    }
    for (const auto& xml : {"", "<html>1</html>", "<root><bitrate>1</bitrate></root>",
             "<root status_code=\"200\"><bitrate>1</bitrate>",
             "<root status_code=\"500\" status_message=\"failed\"><bitrate>1</bitrate></root>",
             "<root status_code=\"200\"><bitrate>1</bitrate></root>junk",
             "<!DOCTYPE root [<!ENTITY x '1'>]><root status_code=\"200\"><bitrate>&x;</bitrate></root>",
             "<!DOCTYPE root SYSTEM 'file:///etc/passwd'><root status_code=\"200\"><bitrate>1</bitrate></root>"}) {
        expect(!parseActionResponse(xml, "bitrate", error), "invalid XML rejected");
    }
    expect(!parseActionResponse(std::string(1024 * 1024 + 1, 'x'), "bitrate", error), "response size bounded");
    std::string deep = "<bitrate>1</bitrate>";
    for (int i = 0; i < 40; ++i) deep = "<n>" + deep + "</n>";
    expect(!parseActionResponse(envelope(deep), "bitrate", error), "XML nesting bounded");

    int applied = -1;
    expect(foundation::parseBitrateResponse(envelope("<bitrate>20000</bitrate>"), 20000, applied, error)
        && applied == 20000 && error.empty(), "modern host applied bitrate acknowledged");
    expect(foundation::parseBitrateResponse(envelope("<bitrate>12000</bitrate>"), 20000, applied, error)
        && applied == 12000, "host clamp is authoritative");
    expect(foundation::parseBitrateResponse(envelope("<bitrate>1</bitrate>"), 20000, applied, error)
        && applied == 20000, "legacy acknowledgment preserves request");
    expect(foundation::parseBitrateResponse(envelope("<bitrate> 12000\n</bitrate>"), 20000, applied, error)
        && applied == 12000, "formatted numeric response accepted");
    expect(foundation::parseBitrateResponse(envelope("<bitrate>1000</bitrate>"), 1000, applied, error)
        && applied == 1000, "minimum applied bitrate accepted");
    expect(foundation::parseBitrateResponse(envelope("<bitrate>500000</bitrate>"), 100000, applied, error)
        && applied == 500000, "host absolute ceiling accepted");
    for (const auto& value : {"", "0", "-1", "-12000", "2", "999", "500001", "12000.0", "1e4",
             "+12000", "12000kbps", "12000 20000", "2147483648", "999999999999999999999"}) {
        applied = 20000;
        expect(!foundation::parseBitrateResponse(envelope("<bitrate>" + std::string(value) + "</bitrate>"),
            20000, applied, error), "invalid applied bitrate rejected");
        expect(applied == 0 && !error.empty(), "failed bitrate does not expose stale applied value");
    }
    for (const auto& xml : {envelope(""), envelope("<bitrate/>"), envelope("<supercmd>1</supercmd>"),
             envelope("<other><bitrate>12000</bitrate></other>"),
             envelope("<bitrate><nested/>12000</bitrate>"),
             envelope("<bitrate>12000</bitrate><bitrate>12000</bitrate>"),
             std::string("<root status_code=\"404\"><bitrate>12000</bitrate></root>"),
             std::string("<root status_code=\"200\"><bitrate>12000</bitrate>"),
             std::string("<!DOCTYPE root [<!ENTITY n '12000'>]><root status_code=\"200\"><bitrate>&n;</bitrate></root>")}) {
        expect(!foundation::parseBitrateResponse(xml, 20000, applied, error), "unconfirmed bitrate response rejected");
    }
    for (int requested : {-1, 0, 999, 100001}) {
        expect(!foundation::parseBitrateResponse(envelope("<bitrate>1</bitrate>"), requested, applied, error),
            "legacy acknowledgment requires valid request");
        expect(!foundation::parseBitrateResponse(envelope("<bitrate>12000</bitrate>"), requested, applied, error),
            "modern acknowledgment requires valid request");
    }
    expect(foundation::parseBitrateResponse(envelope("<bitrate>1</bitrate>"), 100000, applied, error)
        && applied == 100000 && error.empty(), "successful bitrate clears previous rejection");

    std::vector<foundation::Command> commands;
    const auto valid = envelope(app(4, "[{\"id\":\"wrong\",\"name\":\"Other app\"}]") +
        app(7, "[{\"id\":\"a&amp;b=2\",\"name\":\"화면 &amp; 소리 🎮\"}]"));
    expect(foundation::parseCommands(valid, 7, commands, error), "requested application selected");
    expect(commands.size() == 1 && commands[0].id == "a&b=2", "XML entities decoded");
    expect(commands[0].name == "화면 & 소리 🎮", "Unicode preserved");
    expect(!foundation::parseCommands(valid, 99, commands, error), "missing application rejected");
    expect(commands.empty(), "failure clears old commands");
    for (const auto& xml : {envelope("<App><ID>7</ID></App>"), envelope(app(7, "null")),
             envelope(app(7, "")), envelope(app(7, "[]"))}) {
        expect(foundation::parseCommands(xml, 7, commands, error) && commands.empty(), "unadvertised commands are empty");
    }
    for (const auto& value : {"{", "{}", "42", "[{}]", "[{\"id\":7,\"name\":\"Bad\"}]",
             "[{\"id\":\"x\",\"name\":null}]", "[{\"id\":\"\",\"name\":\"Bad\"}]",
             "[{\"id\":\"x\",\"name\":\" \"}]", "[{\"id\":\"x\\u0000y\",\"name\":\"Bad\"}]",
             "[{\"id\":\"x\",\"id\":\"y\",\"name\":\"Bad\"}]",
             "[{\"id\":\"x\",\"name\":\"One\"},{\"id\":\"x\",\"name\":\"Two\"}]"}) {
        expect(!foundation::parseCommands(envelope(app(7, value)), 7, commands, error), "malformed command JSON rejected");
        expect(commands.empty(), "no partial command list on error");
    }
    expect(!foundation::parseCommands(envelope(app(7, "[]") + app(7, "[]")), 7, commands, error), "duplicate app rejected");
    expect(!foundation::parseCommands(envelope("<App><ID>7junk</ID><SuperCmds>[]</SuperCmds></App>"), 7, commands, error), "non-numeric app ID rejected");
    expect(!foundation::parseCommands(envelope("<App><ID>7</ID><SuperCmds>[]</SuperCmds><SuperCmds>[]</SuperCmds></App>"), 7, commands, error), "duplicate command list rejected");
    std::string excessive = "[";
    for (int i = 0; i < 101; ++i) {
        if (i) excessive += ',';
        excessive += "{\"id\":\"" + std::to_string(i) + "\",\"name\":\"Action\"}";
    }
    excessive += ']';
    expect(!foundation::parseCommands(envelope(app(7, excessive)), 7, commands, error), "command count bounded");
    expect(!foundation::parseCommands(envelope(app(7,
        "[{\"id\":\"" + std::string(257, 'x') + "\",\"name\":\"Action\"}]")),
        7, commands, error), "command identifier size bounded");
    expect(!foundation::parseCommands(envelope(app(7,
        "[{\"id\":\"x\",\"name\":\"" + std::string(1025, 'x') + "\"}]")),
        7, commands, error), "command label size bounded");

    expect(foundation::encodeQuery("AZaz09-._~") == "AZaz09-._~", "unreserved query characters preserved");
    expect(foundation::encodeQuery("a&bitrate=999#? +/%") == "a%26bitrate%3D999%23%3F%20%2B%2F%25", "query injection encoded");
    expect(foundation::encodeQuery("한") == "%ED%95%9C", "UTF-8 query encoded by bytes");
    expect(foundation::encodeQuery(std::string("a\0b", 3)) == "a%00b", "embedded NUL encoded");
    std::cout << "Passed " << checks << " protocol checks\n";
}
