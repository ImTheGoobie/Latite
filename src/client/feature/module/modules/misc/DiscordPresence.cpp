#include "pch.h"
#include "DiscordPresence.h"

#include "client/Latite.h"
#include "client/event/events/UpdateEvent.h"
#include "mc/common/network/RakNetConnector.h"

namespace {
	struct PresenceDetails {
		std::string_view address;
		std::string_view name;
		std::string_view logoKey;
		std::string_view logoTooltip;
	};

	constexpr std::array knownServers = {
		PresenceDetails{ "hivebedrock.network", "The Hive", "thehive", "The Hive Logo" },
		PresenceDetails{ "cubecraft.net", "CubeCraft", "cubecraft", "CubeCraft Games Logo" },
		PresenceDetails{ "play.galaxite.net", "Galaxite", "galaxite", "Galaxite Network Logo" },
		PresenceDetails{ "zeqa.net", "Zeqa", "zeqa", "Zeqa Practice Logo" },
		PresenceDetails{ "nethergames.org", "NetherGames", "nethergames", "NetherGames Network Logo" }
	};

	const PresenceDetails* findServerPresence(std::string_view serverAddress) {
		for (const auto& server : knownServers) {
			if (serverAddress.find(server.address) != std::string_view::npos) {
				return &server;
			}
		}

		return nullptr;
	}

	void useMinecraftAssets(DiscordIpcClient::Activity& activity) {
		activity.largeImageKey = "minecraft";
		activity.largeImageText = "Minecraft Bedrock Logo";
		activity.smallImageKey = "latite";
		activity.smallImageText = "Latite Client Logo";
	}

	void useLatiteAsset(DiscordIpcClient::Activity& activity) {
		activity.largeImageKey = "latite";
		activity.largeImageText = "Latite Client Logo";
		activity.smallImageKey.clear();
		activity.smallImageText.clear();
	}

	std::int64_t nowUnixSeconds() {
		return std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	}
}

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
		sessionStart = nowUnixSeconds();
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
	activity.startTimestamp = sessionStart > 0 ? sessionStart : nowUnixSeconds();

	SDK::ClientInstance* clientInstance = SDK::ClientInstance::get();
	if (!clientInstance || !clientInstance->minecraft || !clientInstance->minecraft->getLevel() || !clientInstance->getLocalPlayer()) {
		activity.details = "Playing Minecraft Bedrock";
		activity.state = "In menus";

		useLatiteAsset(activity);
		return activity;
	}

	SDK::RakNetConnector* connector = SDK::RakNetConnector::get();
	if (connector) {
		const std::string serverAddress = !connector->dns.empty() ? connector->dns : connector->ipAddress;
		if (const PresenceDetails* server = findServerPresence(serverAddress)) {
			activity.details = "Playing Minecraft Bedrock";
			activity.state = std::string(server->name);

			activity.largeImageKey = std::string(server->logoKey);
			activity.largeImageText = std::string(server->logoTooltip);
			activity.smallImageKey = "latite";
			activity.smallImageText = "Latite Client Logo";
			return activity;
		}
	}

	activity.details = "Playing Minecraft Bedrock";
	activity.state = getPresenceState();
	useMinecraftAssets(activity);
	return activity;
}

std::string DiscordPresence::getPresenceState() const {
    SDK::RakNetConnector* connector = SDK::RakNetConnector::get();
	// path for generic servers
    if (connector) {
        if (!connector->featuredServer.empty()) {
            return connector->featuredServer;
        }

        if (!connector->dns.empty()) {
            std::string server = connector->dns;
            if (connector->port != 19132) {
                server += std::format(":{}", connector->port);
            }
            return server;
        }
    }

    return "Singleplayer";
}
