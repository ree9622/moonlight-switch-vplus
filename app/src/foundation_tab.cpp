#include "foundation_tab.hpp"
#include "Settings.hpp"
#include <algorithm>
#include <fmt/format.h>

using namespace brls;
using namespace brls::literals;

FoundationTab::FoundationTab(const Host& host, int appId) : host(host), appId(appId) {
    inflateFromXMLRes("xml/views/ingame_overlay/foundation_tab.xml");
    bitrate->setText("vplus/bitrate"_i18n);
    bitrate->setDetailText(fmt::format("{:.1f} Mbps", Settings::instance().bitrate() / 1000.0));
    frameBuffer->setText("vplus/frame_buffer"_i18n);
    frameBuffer->setDetailText(std::to_string(Settings::instance().frames_queue_size()));
    pacing->setText("vplus/pacing"_i18n);
    pacing->setDetailText(Settings::instance().enable_host_cadence_precise_sync() ? "vplus/pacing_host"_i18n : "vplus/pacing_legacy"_i18n);
    command->setText("vplus/commands"_i18n);
    refresh->setText("vplus/refresh"_i18n);
    bitrate->registerClickAction([this](View*) { if (!busy) chooseBitrate(); return true; });
    frameBuffer->registerClickAction([this](View*) { if (!busy) chooseFrameBuffer(); return true; });
    pacing->registerClickAction([this](View*) { if (!busy) choosePacing(); return true; });
    command->registerClickAction([this](View*) { if (!busy) chooseCommand(); return true; });
    refresh->registerClickAction([this](View*) { if (!busy) loadCommands(); return true; });
    loadCommands();
}

void FoundationTab::setBusy(bool value) {
    busy = value;
    bitrate->setActionAvailable(BUTTON_A, !value);
    frameBuffer->setActionAvailable(BUTTON_A, !value);
    pacing->setActionAvailable(BUTTON_A, !value);
    command->setActionAvailable(BUTTON_A, !value && !commands.empty());
    refresh->setActionAvailable(BUTTON_A, !value);
}

void FoundationTab::showFailure(const std::string& error) {
    status->setText("vplus/failed"_i18n);
    auto* dialog = new Dialog("vplus/failed"_i18n + "\n\n" + error);
    dialog->addButton("hints/ok"_i18n, [] {});
    dialog->open();
}

void FoundationTab::loadCommands() {
    setBusy(true);
    status->setText("vplus/loading"_i18n);
    GameStreamClient::instance().foundation_commands(host, appId,
        [this, life = alive](GSResult<std::vector<foundation::Command>> result) {
            if (!*life) return;
            commands.clear();
            if (result.isSuccess()) commands = result.value();
            command->setDetailText(std::to_string(commands.size()));
            setBusy(false);
            // A normal Sunshine server may advertise no extension commands.
            status->setText(result.isSuccess()
                ? (commands.empty() ? "vplus/no_commands"_i18n : "vplus/ready"_i18n)
                : "vplus/load_failed"_i18n);
        });
}

void FoundationTab::chooseBitrate() {
    const std::vector<int> rates = {3000, 5000, 8000, 10000, 15000, 20000, 30000, 40000, 60000};
    std::vector<std::string> labels;
    for (int rate : rates) labels.push_back(std::to_string(rate / 1000) + " Mbps");
    int selected = 0;
    const auto current = std::find(rates.begin(), rates.end(), Settings::instance().bitrate());
    if (current != rates.end()) selected = static_cast<int>(current - rates.begin());
    // Dropdown closes itself after its selection callback. Start the request
    // only after dismissal so a synchronous error cannot pop the error dialog.
    auto* dropdown = new Dropdown("vplus/bitrate"_i18n, labels, [](int) {}, selected,
        [this, life = alive, rates](int index) {
            if (!*life || index < 0 || static_cast<size_t>(index) >= rates.size() || busy) return;
            setBusy(true);
            status->setText("vplus/applying"_i18n);
            const int rate = rates[index];
            GameStreamClient::instance().foundation_set_bitrate(host, rate,
                [this, life](GSResult<int> result) {
                    if (!*life) return;
                    setBusy(false);
                    if (!result.isSuccess()) { showFailure(result.error()); return; }
                    bitrate->setDetailText(fmt::format("{:.1f} Mbps", result.value() / 1000.0));
                    status->setText("vplus/applied"_i18n);
                });
        });
    Application::pushActivity(new Activity(dropdown));
}

void FoundationTab::chooseCommand() {
    if (commands.empty()) return;
    std::vector<std::string> labels;
    for (const auto& item : commands) labels.push_back(item.name);
    const auto snapshot = commands;
    auto* dropdown = new Dropdown("vplus/commands"_i18n, labels, [](int) {}, 0,
        [this, life = alive, snapshot](int index) {
            if (!*life || index < 0 || static_cast<size_t>(index) >= snapshot.size() || busy) return;
            const auto selected = snapshot[index];
            auto* dialog = new Dialog(selected.name + "\n\n" + "vplus/confirm_command"_i18n);
            dialog->addButton("common/cancel"_i18n, [] {});
            dialog->addButton("vplus/execute"_i18n, [this, life, selected] {
                if (*life && !busy) executeCommand(selected);
            });
            dialog->open();
        });
    Application::pushActivity(new Activity(dropdown));
}

void FoundationTab::chooseFrameBuffer() {
    const std::vector<std::string> choices = {"vplus/buffer_low"_i18n, "vplus/buffer_balanced"_i18n,
                                             "vplus/buffer_smooth"_i18n};
    const int selected = std::clamp(Settings::instance().frames_queue_size(), 1, 3) - 1;
    auto* dropdown = new Dropdown("vplus/frame_buffer"_i18n, choices, [](int) {}, selected,
        [this, life = alive](int index) {
            if (!*life || index < 0 || index > 2 || busy) return;
            Settings::instance().set_frames_queue_size(index + 1);
            Settings::instance().save();
            frameBuffer->setDetailText(std::to_string(index + 1));
            status->setText("vplus/buffer_saved"_i18n);
        });
    Application::pushActivity(new Activity(dropdown));
}

void FoundationTab::choosePacing() {
    const std::vector<std::string> choices = {"vplus/pacing_legacy"_i18n, "vplus/pacing_host"_i18n};
    const int selected = Settings::instance().enable_host_cadence_precise_sync() ? 1 : 0;
    auto* dropdown = new Dropdown("vplus/pacing"_i18n, choices, [](int) {}, selected,
        [this, life = alive](int index) {
            if (!*life || index < 0 || index > 1 || busy) return;
            Settings::instance().set_enable_host_cadence_precise_sync(index == 1);
            Settings::instance().save();
            pacing->setDetailText(index == 1 ? "vplus/pacing_host"_i18n : "vplus/pacing_legacy"_i18n);
            status->setText("vplus/buffer_saved"_i18n);
        });
    Application::pushActivity(new Activity(dropdown));
}

void FoundationTab::executeCommand(const foundation::Command& selected) {
    setBusy(true);
    status->setText("vplus/applying"_i18n);
    GameStreamClient::instance().foundation_command(host, selected.id,
        [this, life = alive](GSResult<bool> result) {
            if (!*life) return;
            setBusy(false);
            if (!result.isSuccess()) { showFailure(result.error()); return; }
            status->setText("vplus/command_accepted"_i18n);
        });
}
