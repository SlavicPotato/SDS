#include "pch.h"

ISKSE ISKSE::m_Instance;

void ISKSE::OnLogOpen() 
{
}

const char* ISKSE::GetLogPath() const 
{
    return PLUGIN_LOG_PATH;
}

const char* ISKSE::GetPluginName() const 
{
    return PLUGIN_NAME;
};

UInt32 ISKSE::GetPluginVersion() const 
{
    return MAKE_PLUGIN_VERSION(
        PLUGIN_VERSION_MAJOR,
        PLUGIN_VERSION_MINOR,
        PLUGIN_VERSION_REVISION);
};

bool ISKSE::CheckRuntimeVersion(UInt32 a_version) const
{
    return a_version >= MIN_RUNTIME_VERSION;
}

/*bool ISKSE::CheckInterfaceVersion(UInt32 a_interfaceID, UInt32 a_interfaceVersion, UInt32 a_compiledInterfaceVersion) const
{
    switch (a_interfaceID)
    {
    case SKSETaskInterface::INTERFACE_TYPE:
    case SKSEMessagingInterface::INTERFACE_TYPE:
    }
}*/