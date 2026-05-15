#include "pch.h"
#include "MinecraftGame.h"
#include "mc/Addresses.h"

bool SDK::MinecraftGame::isCursorGrabbed() {
    return hat::member_at<bool>(this, 0x1E0);
}

SDK::ClientInstance* SDK::MinecraftGame::getPrimaryClientInstance() {
    const auto map = hat::member_at<std::map<uint8_t, std::shared_ptr<ClientInstance>>>(this, 0x990);
    return map.at(0).get();
}
