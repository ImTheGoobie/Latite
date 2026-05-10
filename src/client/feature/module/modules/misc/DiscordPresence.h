#pragma once
#include "../../Module.h"
#include "client/misc/DiscordIpcClient.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

class DiscordPresence : public Module {
public:
	DiscordPresence();
	~DiscordPresence() override;

	void onEnable() override;
	void onDisable() override;

private:
	void onUpdate(Event& ev);
	void publishPresence(bool force);
	DiscordIpcClient::Activity makeActivity() const;
	std::string getPresenceState() const;

	std::optional<DiscordIpcClient> ipcClient;
	std::optional<DiscordIpcClient::Activity> lastSentActivity;
	std::chrono::steady_clock::time_point lastCheck{};
	std::chrono::steady_clock::time_point lastRefresh{};
	std::int64_t sessionStart = 0;

	const std::chrono::seconds presenceCheckInterval = 5s;
	const std::chrono::seconds presenceRefreshInterval = 60s;
	const std::string discordApplicationId = "1066896173799047199";
};
