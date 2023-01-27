#include "houdiniPluginActivator.cpp"

int main() {
    // Get version name from 
    std::ifstream versionFile("version");
    std::string coreVersion;
    std::string pluginVersion;
    const char* version = nullptr;

    if (versionFile.is_open()) {
        /* This file contains two strings like
        core:<core version>
        plugin:<plugin version>
        */
        versionFile >> coreVersion;
        versionFile >> pluginVersion;
        std::string findString = "plugin:";
        pluginVersion = pluginVersion.substr(findString.length(), pluginVersion.length() - findString.length());
        version = pluginVersion.c_str();
        versionFile.close();
    }
    
    int exitCode = ActivateHoudiniPlugin("RPR_for_Houdini", {
        {"HOUDINI_PATH", "/houdini"},
        {"PYTHONPATH", "/lib/python"},
#if defined(_WIN32) || defined(_WIN64)
        {"PATH", "/lib"}
#endif
    }, version);

#if defined(_WIN32) || defined(_WIN64)
    system("pause");
#endif

    return exitCode;
}
