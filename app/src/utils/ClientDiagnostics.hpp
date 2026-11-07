#pragma once
#include <string>

// Only sanitized, whitelisted performance fields may be passed here.
void writeClientDiagnostics(const std::string& directory, const std::string& sample);
