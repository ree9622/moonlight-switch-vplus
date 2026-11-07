#pragma once

#include "GameStreamClient.hpp"
#include <borealis.hpp>
#include <memory>

class FoundationTab : public brls::Box {
  public:
    FoundationTab(const Host& host, int appId);
    ~FoundationTab() override { *alive = false; }

  private:
    Host host;
    int appId;
    bool busy = false;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    std::vector<foundation::Command> commands;
    void setBusy(bool value);
    void loadCommands();
    void chooseBitrate();
    void chooseFrameBuffer();
    void choosePacing();
    void chooseCommand();
    void executeCommand(const foundation::Command& command);
    void showFailure(const std::string& error);
    BRLS_BIND(brls::DetailCell, bitrate, "vplus_bitrate");
    BRLS_BIND(brls::DetailCell, frameBuffer, "vplus_frame_buffer");
    BRLS_BIND(brls::DetailCell, pacing, "vplus_pacing");
    BRLS_BIND(brls::DetailCell, command, "vplus_command");
    BRLS_BIND(brls::DetailCell, refresh, "vplus_refresh");
    BRLS_BIND(brls::Label, status, "vplus_status");
};
