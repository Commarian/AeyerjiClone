// File: Source/Aeyerji/Private/Attributes/AeyerjiStatTuning.cpp
#include "Attributes/AeyerjiStatTuning.h"

UAeyerjiStatSettings::UAeyerjiStatSettings() {}

const UAeyerjiAttributeTuning* UAeyerjiStatSettings::Get()
{
    const UAeyerjiStatSettings* Settings = GetDefault<UAeyerjiStatSettings>();
    if (!Settings)
    {
        return nullptr;
    }
    if (Settings->DefaultTuning.IsNull())
    {
        return nullptr;
    }
    return Settings->DefaultTuning.LoadSynchronous();
}
