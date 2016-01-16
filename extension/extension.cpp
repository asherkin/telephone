#include "extension.h"

#include "CDetour/detours.h"

Telephone g_Telephone;
SMEXT_LINK(&g_Telephone);

IGameConfig *gameConfig;

CDetour *detourBroadcastVoiceData;

#ifndef NDEBUG
#define DEBUG_LOG rootconsole->ConsolePrint
#else
#define DEBUG_LOG(...)
#endif

class IClient;

DETOUR_DECL_MEMBER4(BroadcastVoiceData, void, IClient *, client, int, bytes, char *, data, long long, xuid)
{
	DEBUG_LOG(">>> SV_BroadcastVoiceData(%p, %d, %p, %lld)", client, bytes, data, xuid);

	DETOUR_MEMBER_CALL(BroadcastVoiceData)(client, bytes, data, xuid);
}

bool Telephone::SDK_OnLoad(char *error, size_t maxlength, bool late)
{
	if (!gameconfs->LoadGameConfigFile("telephone.games", &gameConfig, error, maxlength)) {
		return false;
	}

	CDetourManager::Init(smutils->GetScriptingEngine(), gameConfig);

	detourBroadcastVoiceData = DETOUR_CREATE_MEMBER(BroadcastVoiceData, "BroadcastVoiceData");
	if (!detourBroadcastVoiceData) {
		gameconfs->CloseGameConfigFile(gameConfig);

		strncpy(error, "Error setting up BroadcastVoiceData detour", maxlength);
		return false;
	}

	detourBroadcastVoiceData->EnableDetour();

	return true;
}

void Telephone::SDK_OnUnload()
{
	detourBroadcastVoiceData->DisableDetour();

	gameconfs->CloseGameConfigFile(gameConfig);
}
