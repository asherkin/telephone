#ifndef _INCLUDE_SOURCEMOD_EXTENSION_PROPER_H_
#define _INCLUDE_SOURCEMOD_EXTENSION_PROPER_H_

#include "smsdk_ext.h"

class Telephone: public SDKExtension, public ITextListener_INI
{
public: // SDKExtension
	virtual bool SDK_OnLoad(char *error, size_t maxlength, bool late);
	virtual void SDK_OnUnload();
	virtual void OnCoreMapStart(edict_t *pEdictList, int edictCount, int clientMax);

public: // ITextListener_INI
	virtual bool ReadINI_KeyValue(const char *key, const char *value, bool invalid_tokens, bool equal_token, bool quotes, unsigned int *curtok);

private:
	bool ReloadConfigFile(char *error, size_t maxlength);
};

#endif // _INCLUDE_SOURCEMOD_EXTENSION_PROPER_H_
