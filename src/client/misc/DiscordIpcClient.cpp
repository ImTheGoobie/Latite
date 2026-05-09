#include "pch.h"
#include "DiscordIpcClient.h"

#include <algorithm>
#include <array>
#include <limits>

namespace {
	constexpr auto reconnectDelay = 10s;

	void writeUint32(std::uint8_t* dst, std::uint32_t value) {
		dst[0] = static_cast<std::uint8_t>(value);
		dst[1] = static_cast<std::uint8_t>(value >> 8);
		dst[2] = static_cast<std::uint8_t>(value >> 16);
		dst[3] = static_cast<std::uint8_t>(value >> 24);
	}

	std::uint32_t readUint32(const std::vector<std::uint8_t>& src, std::size_t offset) {
		return static_cast<std::uint32_t>(src[offset])
			| (static_cast<std::uint32_t>(src[offset + 1]) << 8)
			| (static_cast<std::uint32_t>(src[offset + 2]) << 16)
			| (static_cast<std::uint32_t>(src[offset + 3]) << 24);
	}
}

DiscordIpcClient::DiscordIpcClient(std::string applicationId) : applicationId(std::move(applicationId)) {
}

DiscordIpcClient::~DiscordIpcClient() {
	disconnect();
}

bool DiscordIpcClient::setActivity(const Activity& activity) {
	if (!ensureConnected()) {
		return false;
	}

	nlohmann::json activityJson = {
		{ "type", 0 },
		{ "details", activity.details },
		{ "state", activity.state },
		{
			"timestamps", {
				{ "start", activity.startTimestamp }
			}
		},
		{
			"assets", {
				{ "large_image", "latite" },
				{ "large_text", std::format("Latite Client {}", Latite::version) }
			}
		}
	};

	if (!sendSetActivity(activityJson)) {
		scheduleReconnect();
		return false;
	}

	return drainIncoming();
}

void DiscordIpcClient::clearActivity() {
	if (!isConnected()) {
		return;
	}

	sendSetActivity(nullptr);
	drainIncoming();
	closePipe();
}

void DiscordIpcClient::disconnect() {
	closePipe();
	readBuffer.clear();
}

bool DiscordIpcClient::isConnected() const noexcept {
	return pipe != INVALID_HANDLE_VALUE;
}

bool DiscordIpcClient::ensureConnected() {
	if (isConnected()) {
		return true;
	}

	if (applicationId.empty() || applicationId == "0") {
		return false;
	}

	const auto now = std::chrono::steady_clock::now();
	if (now < nextConnectAttempt) {
		return false;
	}

	if (connect()) {
		return true;
	}

	scheduleReconnect();
	return false;
}

bool DiscordIpcClient::connect() {
	closePipe();

	for (int pipeIndex = 0; pipeIndex < 10; ++pipeIndex) {
		if (!openPipe(pipeIndex)) {
			continue;
		}

		if (sendHandshake()) {
			drainIncoming();
			return true;
		}

		closePipe();
	}

	return false;
}

bool DiscordIpcClient::openPipe(int pipeIndex) {
	const std::wstring pipePath = L"\\\\?\\pipe\\discord-ipc-" + std::to_wstring(pipeIndex);
	pipe = CreateFileW(
		pipePath.c_str(),
		GENERIC_READ | GENERIC_WRITE,
		0,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr
	);

	return isConnected();
}

bool DiscordIpcClient::sendHandshake() {
	const nlohmann::json payload = {
		{ "v", 1 },
		{ "client_id", applicationId }
	};

	return sendFrame(Opcode::Handshake, payload);
}

bool DiscordIpcClient::sendSetActivity(const nlohmann::json& activity) {
	const nlohmann::json payload = {
		{ "cmd", "SET_ACTIVITY" },
		{
			"args", {
				{ "pid", GetCurrentProcessId() },
				{ "activity", activity }
			}
		},
		{ "nonce", std::format("latite-discord-presence-{}", ++nonce) }
	};

	return sendFrame(Opcode::Frame, payload);
}

bool DiscordIpcClient::sendFrame(Opcode opcode, const nlohmann::json& payload) {
	if (!isConnected()) {
		return false;
	}

	if (!drainIncoming() && !isConnected()) {
		return false;
	}

	const std::string body = payload.dump();
	if (body.size() > std::numeric_limits<std::uint32_t>::max()) {
		return false;
	}

	std::array<std::uint8_t, 8> header{};
	writeUint32(header.data(), static_cast<std::uint32_t>(opcode));
	writeUint32(header.data() + 4, static_cast<std::uint32_t>(body.size()));

	if (!writeAll(header.data(), header.size()) || !writeAll(body.data(), body.size())) {
		closePipe();
		return false;
	}

	return true;
}

bool DiscordIpcClient::writeAll(const void* data, std::size_t size) const {
	const auto* cursor = static_cast<const std::uint8_t*>(data);
	auto remaining = size;

	while (remaining > 0) {
		const auto chunkSize = static_cast<DWORD>(std::min<std::size_t>(
			remaining,
			std::numeric_limits<DWORD>::max()
		));
		DWORD written = 0;

		if (!WriteFile(pipe, cursor, chunkSize, &written, nullptr) || written == 0) {
			return false;
		}

		cursor += written;
		remaining -= written;
	}

	return true;
}

bool DiscordIpcClient::drainIncoming() {
	if (!isConnected()) {
		return false;
	}

	while (true) {
		DWORD available = 0;
		if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) {
			closePipe();
			return false;
		}

		if (available == 0) {
			break;
		}

		std::vector<std::uint8_t> chunk(available);
		DWORD bytesRead = 0;
		if (!ReadFile(pipe, chunk.data(), available, &bytesRead, nullptr)) {
			closePipe();
			return false;
		}
		if (bytesRead == 0) {
			closePipe();
			return false;
		}

		chunk.resize(bytesRead);
		readBuffer.insert(readBuffer.end(), chunk.begin(), chunk.end());

		while (readBuffer.size() >= 8) {
			const auto payloadSize = readUint32(readBuffer, 4);
			const auto frameSize = static_cast<std::size_t>(payloadSize) + 8;

			if (frameSize < 8) {
				closePipe();
				return false;
			}

			if (readBuffer.size() < frameSize) {
				break;
			}

			readBuffer.erase(readBuffer.begin(), readBuffer.begin() + frameSize);
		}
	}

	return true;
}

void DiscordIpcClient::closePipe() {
	if (pipe != INVALID_HANDLE_VALUE) {
		CloseHandle(pipe);
		pipe = INVALID_HANDLE_VALUE;
	}
}

void DiscordIpcClient::scheduleReconnect() {
	closePipe();
	nextConnectAttempt = std::chrono::steady_clock::now() + reconnectDelay;
}
