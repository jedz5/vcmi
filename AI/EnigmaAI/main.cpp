#include "StdInc.h"

#include "../../lib/AI_Base.h"
#include "EnigmaAI.h"

#ifdef __GNUC__
#define strcpy_s(a, b, c) strncpy(a, c, b)
#endif

#ifdef VCMI_ANDROID
#define GetGlobalAiVersion EnigmaAI_GetGlobalAiVersion
#define GetAiName EnigmaAI_GetAiName
#define GetNewBattleAI EnigmaAI_GetNewBattleAI
#endif

static const char *g_cszAiName = "Enigma AI";

extern "C" DLL_EXPORT int GetGlobalAiVersion()
{
	return AI_INTERFACE_VER;
}

extern "C" DLL_EXPORT void GetAiName(char* name)
{
	strcpy_s(name, strlen(g_cszAiName) + 1, g_cszAiName);
}

extern "C" DLL_EXPORT void GetNewBattleAI(shared_ptr<CBattleGameInterface> &out)
{
	out = make_shared<CEnigmaAI>();
}
