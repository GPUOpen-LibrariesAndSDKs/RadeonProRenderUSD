macro(parseVersion version_file prefix)
    if(NOT EXISTS ${version_file})
        message(FATAL_ERROR "Invalid ${prefix} SDK: missing ${version_file} file")
    endif()

    file(STRINGS "${version_file}" _major_version_str
         REGEX "^#define[\t ]+${prefix}_VERSION_MAJOR[\t ]+.*")
    file(STRINGS "${version_file}" _minor_version_str
         REGEX "^#define[\t ]+${prefix}_VERSION_MINOR[\t ]+.*")
    file(STRINGS "${version_file}" _revision_version_str
         REGEX "^#define[\t ]+${prefix}_VERSION_REVISION[\t ]+.*")

    string(REGEX REPLACE "^.*MAJOR[\t ]+([0-9]*).*$" "\\1"
           ${prefix}_MAJOR_VERSION "${_major_version_str}")
    string(REGEX REPLACE "^.*MINOR[\t ]+([0-9]*).*$" "\\1"
           ${prefix}_MINOR_VERSION "${_minor_version_str}")
    string(REGEX REPLACE "^.*REVISION[\t ]+([0-9]*).*$" "\\1"
           ${prefix}_REVISION_VERSION "${_revision_version_str}")

    set(${prefix}_VERSION_STRING "${${prefix}_MAJOR_VERSION}.${${prefix}_MINOR_VERSION}.${${prefix}_REVISION_VERSION}")
endmacro()