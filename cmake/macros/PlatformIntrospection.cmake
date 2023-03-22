# Returns a lowercased version of a given lsb_release field.
macro(_LSB_RELEASE field retval)
    execute_process(
        COMMAND lsb_release "--${field}"
        OUTPUT_VARIABLE _output ERROR_VARIABLE _output RESULT_VARIABLE _result)
    if(_result)
        message(FATAL_ERROR
            "Cannot determine Linux revision! Output from "
            "lsb_release --${field}: ${_output}")
    endif(_result)
    string(REGEX REPLACE "^[^:]*:" "" _output "${_output}")
    string(TOLOWER "${_output}" _output)
    string(STRIP "${_output}" ${retval})
endmacro(_LSB_RELEASE)

# Returns a lowercased version of a given /etc/os-release field.
macro(_OS_RELEASE field retval)
    file(STRINGS /etc/os-release vars)
    set(${_value} "${field}-NOTFOUND")
    foreach(var ${vars})
        if(var MATCHES "^${field}=(.*)")
            set(_value "${CMAKE_MATCH_1}")
            # Value may be quoted in single- or double-quotes; strip them
            if(_value MATCHES "^['\"](.*)['\"]\$")
                set(_value "${CMAKE_MATCH_1}")
            endif()
            break()
        endif()
    endforeach()

    set(${retval} "${_value}")
endmacro(_OS_RELEASE)

# Returns a simple string describing the current platform. Possible
# return values currently include: windows_msvc2017; windows_msvc2015;
# windows_msvc2013; windows_msvc2012; macosx; or any value from
# _DETERMINE_LINUX_DISTRO.
macro(DETERMINE_PLATFORM var)
    if(DEFINED CB_DOWNLOAD_DEPS_PLATFORM)
        set(_plat ${CB_DOWNLOAD_DEPS_PLATFORM})
    else(DEFINED CB_DOWNLOAD_DEPS_PLATFORM)
        set(_plat ${CMAKE_SYSTEM_NAME})
        if(_plat STREQUAL "Windows" OR _plat STREQUAL "WindowsStore")
            if(${MSVC_VERSION} LESS 1800)
                set(_plat "windows_msvc2012")
            elseif(${MSVC_VERSION} LESS 1900)
                set(_plat "windows_msvc2013")
            elseif(${MSVC_VERSION} LESS 1910)
                set(_plat "windows_msvc2015")
            elseif(${MSVC_VERSION} LESS 1920)
                set(_plat "windows_msvc2017")
            elseif(${MSVC_VERSION} LESS 1930)
                set(_plat "windows_msvc2019")
            elseif(${MSVC_VERSION} LESS 1940)
                set(_plat "windows_msvc2022")
            else()
                message(FATAL_ERROR "Unsupported MSVC version: ${MSVC_VERSION}")
            endif()
        elseif(_plat STREQUAL "Darwin")
            set(_plat "macosx")
        elseif(_plat STREQUAL "Linux")
            _DETERMINE_LINUX_DISTRO (_plat)
        elseif(_plat STREQUAL "SunOS")
            set(_plat "sunos")
        elseif(_plat STREQUAL "FreeBSD")
            set(_plat "freebsd")
        else(_plat STREQUAL "Windows")
            message(WARNING "Sorry, don't recognize your system ${_plat}. ")
            set(_plat "unknown")
        endif(_plat STREQUAL "Windows" OR _plat STREQUAL "WindowsStore")
        set(CB_DOWNLOAD_DEPS_PLATFORM ${_plat} CACHE STRING
            "Platform for downloaded dependencies")
        MARK_AS_ADVANCED (CB_DOWNLOAD_DEPS_PLATFORM)
    endif(DEFINED CB_DOWNLOAD_DEPS_PLATFORM)
    set(${var} ${_plat})
endmacro(DETERMINE_PLATFORM)


# Returns a simple string describing the current Linux distribution
# compatibility. Possible return values currently include:
# ubuntu14.04, ubuntu12.04, ubuntu10.04, centos5, centos6, debian7, debian8.
macro(_DETERMINE_LINUX_DISTRO _distro)
    if(EXISTS "/etc/os-release")
        _OS_RELEASE(ID _id)
        _OS_RELEASE(VERSION_ID _ver)
    endif()
    if(NOT ( _id AND _ver ) )
        find_program(LSB_RELEASE lsb_release)
        if(LSB_RELEASE)
            _LSB_RELEASE(id _id)
            _LSB_RELEASE(release _ver)
        else(LSB_RELEASE)
            message(WARNING "Can't determine Linux platform without /etc/os-release or lsb_release")
            set(_id "unknown")
            set(_ver "")
        endif(LSB_RELEASE)
    endif()
    if(_id STREQUAL "linuxmint")
        # Linux Mint is an Ubuntu derivative; estimate nearest Ubuntu equivalent
        set(_id "ubuntu")
        if(_ver VERSION_LESS 13)
            set(_ver 10.04)
        elseif(_ver VERSION_LESS 17)
            set(_ver 12.04)
        elseif(_ver VERSION_LESS 18)
            set(_ver 14.04)
        else()
            set(_ver 16.04)
        endif()
    elseif(_id STREQUAL "debian" OR _id STREQUAL "centos" OR _id STREQUAL "rhel")
        # Just use the major version from the CentOS/Debian/RHEL identifier;
        # we don't need different builds for different minor versions.
        string(REGEX MATCH "[0-9]+" _ver "${_ver}")
    elseif(_id STREQUAL "fedora" AND _ver VERSION_LESS 26)
        set(_id "centos")
        set(_ver "7")
    elseif(_id MATCHES "opensuse.*" OR _id MATCHES "suse.*" OR _id MATCHES "sles.*")
        SET(_id "suse")
        # Just use the major version from the SuSE identifier - we don't
        # need different builds for different minor versions.
        string(REGEX MATCH "[0-9]+" _ver "${_ver}")
    endif(_id STREQUAL "linuxmint")
    set(${_distro} "${_id}${_ver}")
endmacro(_DETERMINE_LINUX_DISTRO)
