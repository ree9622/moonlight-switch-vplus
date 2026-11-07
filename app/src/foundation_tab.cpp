#include "foundation_tab.hpp"
#include "Settings.hpp"
#include <algorithm>

using namespace brls;
using namespace brls::literals;

FoundationTab::FoundationTab(const Host& host, int appId) : host(host), appId(appId) {
    inflateFromXMLRes("xml/views/ingame_overlay/foundation_tab.xml");
    bitrate->setText("vplus/bitrate"_i18n);
    bitrate->setDetailText(std::to_string(Settings::instance().bitrate() / 1000) + " Mbps");
    command->setText("vplus/commands"_i18n);
    refresh->setText("vplus/refresh"_i18n);
    bitrate->registerClickAction([this](View*) { if (!busy) chooseBitrate(); return true; });
    command->registerClickAction([this](View*) { if (!busy) chooseCommand(); return true; });
    refresh->registerClickAction([this](View*) { if (!busy) loadCommands(); return true; });
    loadCommands();
}

void FoundationTab::setBusy(bool value) {
    busy = value;
    bitrate->setActionAvailable(BUTTON_A, !value);
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
    auto* dropdown = new Dropdown("vplus/bitrate"_i18n, labels,
        [this, life = alive, rates](int index) {
            if (!*life || index < 0 || static_cast<size_t>(index) >= rates.size() || busy) return;
            setBusy(true);
            status->setText("vplus/applying"_i18n);
            const int rate = rates[index];
            GameStreamClient::instance().foundation_set_bitrate(host, rate,
                [this, life, rate](GSResult<bool> result) {
                    if (!*life) return;
                    setBusy(false);
                    if (!result.isSuccess()) { showFailure(result.error()); return; }
                    bitrate->setDetailText(std::to_string(rate / 1000) + " Mbps");
                    status->setText("vplus/applied"_i18n);
                });
        }, selected);
    Application::pushActivity(new Activity(dropdown));
}

void FoundationTab::chooseCommand() {
    if (commands.empty()) return;
    std::vector<std::string> labels;
    for (const auto& item : commands) labels.push_back(item.name);
    const auto snapshot = commands;
    auto* dropdown = new Dropdown("vplus/commands"_i18n, labels,
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
