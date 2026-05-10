#include "pch.h"
#include "DiscordPresence.h"

#include "client/Latite.h"
#include "client/event/events/UpdateEvent.h"
#include "mc/common/network/RakNetConnector.h"

DiscordPresence::DiscordPresence() : Module(
	"DiscordPresence",
	LocalizeString::get("client.module.discordPresence.name"),
	LocalizeString::get("client.module.discordPresence.desc"),
	GAME
) {
	listen<UpdateEvent>(static_cast<EventListenerFunc>(&DiscordPresence::onUpdate));
}

DiscordPresence::~DiscordPresence() {
	DiscordPresence::onDisable();
}

void DiscordPresence::onEnable() {
	if (sessionStart <= 0) {
		sessionStart = DiscordPresence::nowUnixSeconds();
	}
	lastCheck = {};
	lastRefresh = {};
	lastSentActivity.reset();
	ipcClient.emplace(discordApplicationId);
	DiscordPresence::publishPresence(true);
}

void DiscordPresence::onDisable() {
	if (ipcClient) {
		ipcClient->clearActivity();
		ipcClient.reset();
	}

	lastSentActivity.reset();
}

void DiscordPresence::onUpdate(Event&) {
	DiscordPresence::publishPresence(false);
}

void DiscordPresence::publishPresence(bool force) {
	if (!ipcClient) {
		return;
	}

	const auto now = std::chrono::steady_clock::now();
	if (!force && now - lastCheck < presenceCheckInterval) {
		return;
	}
	lastCheck = now;

	const DiscordIpcClient::Activity activity = DiscordPresence::makeActivity();
	bool shouldRefresh = false;

	if (!lastSentActivity) {
		shouldRefresh = true;
	} else if (*lastSentActivity != activity) {
		shouldRefresh = true;
	} else if (now - lastRefresh >= presenceRefreshInterval) {
		shouldRefresh = true;
	}

	if (!force && !shouldRefresh) {
		return;
	}

	if (ipcClient->setActivity(activity)) {
		lastSentActivity = activity;
		lastRefresh = now;
	}
}

DiscordIpcClient::Activity DiscordPresence::makeActivity() const {
	DiscordIpcClient::Activity activity;
	activity.details = Latite::get().gameVersion.empty()
		? "Minecraft"
		: std::format("Minecraft {}", Latite::get().gameVersion);
	activity.state = DiscordPresence::getPresenceState();
	activity.startTimestamp = sessionStart > 0 ? sessionStart : DiscordPresence::nowUnixSeconds();
	return activity;
}

std::string DiscordPresence::getPresenceState() const {
	SDK::ClientInstance* clientInstance = SDK::ClientInstance::get();
	if (!clientInstance || !clientInstance->minecraft || !clientInstance->minecraft->getLevel() || !clientInstance->getLocalPlayer()) {
		return "In menus";
	}

	SDK::RakNetConnector* connector = SDK::RakNetConnector::get();
	if (connector) {
		if (!connector->featuredServer.empty()) {
			return "On " + connector->featuredServer;
		}

		if (!connector->dns.empty()) {
			std::string server = connector->dns;
			if (connector->port != 19132) {
				server += std::format(":{}", connector->port);
			}
			return "On " + server;
		}
	}

	return "Singleplayer";
}

std::int64_t DiscordPresence::nowUnixSeconds() const {
	return std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
}