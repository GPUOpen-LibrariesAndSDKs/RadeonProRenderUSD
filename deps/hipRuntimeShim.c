/* The dependency supplies the HIP API. The legacy soname lets an older SDK
 * loader find an explicitly selected HIP 7 runtime without system symlinks. */
extern int hipRuntimeGetVersion(int *version);
int rprSelectedHipRuntimeVersion(int *version) {
    return hipRuntimeGetVersion(version);
}
