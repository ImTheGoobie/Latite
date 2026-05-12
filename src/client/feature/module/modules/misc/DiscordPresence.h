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
	void onChatMessage(Event& ev);
	void onSendPacket(Event& ev);
	void publishPresence(bool force);
	DiscordIpcClient::Activity makeActivity();
	std::string getPresenceState() const;
	void requestHiveConnectionInfo(std::chrono::steady_clock::time_point now);
	bool hasPendingHiveConnectionProbe() const;

	std::optional<DiscordIpcClient> ipcClient;
	std::optional<DiscordIpcClient::Activity> lastSentActivity;
	std::optional<std::string> hiveGameModeCode;
	std::chrono::steady_clock::time_point lastCheck{};
	std::chrono::steady_clock::time_point lastRefresh{};
	std::chrono::steady_clock::time_point lastHiveConnectionProbe{};
	std::chrono::steady_clock::time_point lastUserHiveConnectionCommand{};
	std::int64_t sessionStart = 0;
	int pendingHiveConnectionMessages = 0;
	bool sendingHiveConnectionProbe = false;

	const std::chrono::seconds presenceCheckInterval = 5s;
	const std::chrono::seconds presenceRefreshInterval = 60s;
	const std::chrono::seconds hiveConnectionProbeInterval = 5s;
	const std::chrono::seconds hiveUserConnectionBackoff = 10s;
	const std::chrono::seconds hiveConnectionProbeResponseWindow = 10s;
	const std::string discordApplicationId = "1066896173799047199";
};
