#include "ClientDiagnostics.hpp"
#include <borealis/core/thread.hpp>
#include <filesystem>
#include <fstream>

void writeClientDiagnostics(const std::string& directory, const std::string& sample) {
    const auto path = (std::filesystem::path(directory) / "performance.log").string();
    const auto line = std::string("VPlus ") + VPLUS_VERSION + " build=" + VPLUS_BUILD_ID + " " + sample + "\n";
    // Never redirect the general HTTP logger here: it includes pairing and
    // session secrets. Serialize bounded writes off the rendering thread.
    brls::async([path, line] {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (!error && size >= 512 * 1024) return;
        std::ofstream file(path, std::ios::app | std::ios::binary);
        if (file) file << line;
    });
}
