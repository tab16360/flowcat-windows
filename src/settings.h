#pragma once
#include "common.h"
namespace fc {
bool preference(const wchar_t* name,bool fallback);
void setPreference(const wchar_t* name,bool value);
bool autoStart();bool setAutoStart(bool value);
void initializePreferences();
}
