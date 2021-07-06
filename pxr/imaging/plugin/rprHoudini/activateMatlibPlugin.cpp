#include "houdiniPluginActivator.cpp"

int main() {
    int exitCode = ActivateHoudiniPlugin("RPR_MaterialLibrary", {
    	{"RPR_MTLX_MATERIAL_LIBRARY_PATH", ""}
    });

#if defined(_WIN32) || defined(_WIN64)
    system("pause");
#endif

    return exitCode;
}
