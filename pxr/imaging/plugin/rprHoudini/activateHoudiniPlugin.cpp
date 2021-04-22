#include "houdiniPluginActivator.cpp"

int main() {
    int exitCode = ActivateHoudiniPlugin("RPR_for_Houdini", {
        {"RPR", ""},
        {"HOUDINI_PATH", "/houdini"},
        {"PYTHONPATH", "/lib/python"},
#if defined(_WIN32) || defined(_WIN64)
        {"PATH", "/lib"}
#endif
    });

#if defined(_WIN32) || defined(_WIN64)
    system("pause");
#endif

    return exitCode;
}
