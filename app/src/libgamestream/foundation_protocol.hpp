#pragma once

#include <string>
#include <vector>

namespace foundation {

struct Command {
    std::string id;
    std::string name;
};

// Encode a single query parameter, never a whole URL.
std::string encodeQuery(const std::string& value);

bool parseActionResponse(const std::string& xml, const std::string& action,
                         std::string& error);
// Modern hosts return the applied kbps (possibly clamped); legacy hosts return 1.
bool parseBitrateResponse(const std::string& xml, int requestedKbps,
                          int& appliedKbps, std::string& error);
bool parseCommands(const std::string& xml, int appId,
                   std::vector<Command>& commands, std::string& error);

} // namespace foundation
