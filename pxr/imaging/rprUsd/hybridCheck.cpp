/************************************************************************
Copyright 2022 Advanced Micro Devices, Inc
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
    http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
************************************************************************/

#include <vulkan/vulkan.h>
#include "hybridCheck.h"

#ifdef _WIN32
#include <Windows.h>
#elif defined __linux
#include <dlfcn.h>
#endif

#ifdef _WIN32
#define LIBRARY_TYPE HMODULE
#elif defined __linux
#define LIBRARY_TYPE void*
#endif


bool LoadVulkanLibrary(LIBRARY_TYPE& vulkan_library) {
#if defined _WIN32
    vulkan_library = LoadLibrary(TEXT("vulkan-1.dll"));
#elif defined __linux
    vulkan_library = dlopen("libvulkan.so.1", RTLD_NOW);
#elif defined(__APPLE__)
    vulkan_library = dlopen("libvulkan.1.dylib", RTLD_NOW);
#endif

    if (vulkan_library == nullptr) {
        return false;
    }
    return true;
}

struct VulcanFunctions {
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
    PFN_vkCreateInstance vkCreateInstance = nullptr;
    PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices = nullptr;
    PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties = nullptr;
    PFN_vkDestroyInstance vkDestroyInstance = nullptr;
};

void UnloadVulkanLibrary(LIBRARY_TYPE& vulkan_library) {
#if defined _WIN32
    FreeLibrary(vulkan_library);
#elif defined __linux
    dlclose(vulkan_library);
#elif defined(__APPLE__)
    dlclose(vulkan_library);
#endif
}

bool LoadGlobalFunctions(LIBRARY_TYPE const& vulkan_library, VulcanFunctions& vkf) {
#if defined _WIN32
#define LoadFunction GetProcAddress
#elif defined __linux
#define LoadFunction dlsym
#endif

    vkf.vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)LoadFunction(vulkan_library, "vkGetInstanceProcAddr");
    if (vkf.vkGetInstanceProcAddr == nullptr) {
        return false;
    }
    
    vkf.vkCreateInstance = (PFN_vkCreateInstance)vkf.vkGetInstanceProcAddr(nullptr, "vkCreateInstance");
    if (vkf.vkCreateInstance == nullptr) {
        return false;
    }

    return true;
}

bool LoadInstanceFunctions(LIBRARY_TYPE const& vulkan_library, VkInstance instance, VulcanFunctions& vkf) {
    vkf.vkEnumeratePhysicalDevices = (PFN_vkEnumeratePhysicalDevices)vkf.vkGetInstanceProcAddr(instance, "vkEnumeratePhysicalDevices");
    if (vkf.vkEnumeratePhysicalDevices == nullptr) {
        return false;
    }

    vkf.vkGetPhysicalDeviceProperties = (PFN_vkGetPhysicalDeviceProperties)vkf.vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties");
    if (vkf.vkGetPhysicalDeviceProperties == nullptr) {
        return false;
    }

    vkf.vkDestroyInstance = (PFN_vkDestroyInstance)vkf.vkGetInstanceProcAddr(instance, "vkDestroyInstance");
    if (vkf.vkDestroyInstance == nullptr) {
        return false;
    }

    return true;
}

bool CreateVulkanInstance(VkInstance& instance, VulcanFunctions& vkf) {
    VkApplicationInfo application_info = {
        VK_STRUCTURE_TYPE_APPLICATION_INFO,               // VkStructureType           sType
        nullptr,                                          // const void              * pNext
        "Rpr",                                            // const char              * pApplicationName
        VK_MAKE_VERSION(1, 0, 0),                         // uint32_t                  applicationVersion
        "Rpr",                                            // const char              * pEngineName
        VK_MAKE_VERSION(1, 0, 0),                         // uint32_t                  engineVersion
        VK_MAKE_VERSION(1, 0, 0)                          // uint32_t                  apiVersion
    };

    VkInstanceCreateInfo instance_create_info = {
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,           // VkStructureType           sType
        nullptr,                                          // const void              * pNext
        0,                                                // VkInstanceCreateFlags     flags
        &application_info,                                // const VkApplicationInfo * pApplicationInfo
        0,                                                // uint32_t                  enabledLayerCount
        nullptr,                                          // const char * const      * ppEnabledLayerNames
        0,                                                // uint32_t                  enabledExtensionCount
        nullptr                                           // const char * const      * ppEnabledExtensionNames
    };

    VkResult result = vkf.vkCreateInstance(&instance_create_info, nullptr, &instance);
    if ((result != VK_SUCCESS) ||
        (instance == VK_NULL_HANDLE)) {
        return false;
    }

    return true;
}

bool EnumeratePhysicalDevices(VkInstance instance, std::vector<VkPhysicalDevice>& available_devices, VulcanFunctions& vkf) {
    uint32_t devices_count = 0;
    VkResult result = VK_SUCCESS;

    result = vkf.vkEnumeratePhysicalDevices(instance, &devices_count, nullptr);
    if ((result != VK_SUCCESS) ||
        (devices_count == 0)) {
        return false;
    }

    available_devices.resize(devices_count);
    result = vkf.vkEnumeratePhysicalDevices(instance, &devices_count, available_devices.data());
    if ((result != VK_SUCCESS) ||
        (devices_count == 0)) {
        return false;
    }

    return true;
}

void GetPhysicalDeviceProperties(VkPhysicalDevice physical_device, VkPhysicalDeviceProperties& device_properties, VulcanFunctions& vkf) {
    vkf.vkGetPhysicalDeviceProperties(physical_device, &device_properties);
}

void DestroyInstance(VkInstance instance, VulcanFunctions& vkf) {
    if (instance) { 
        vkf.vkDestroyInstance(instance, nullptr);
    }
}

bool IsDeviceSupported(uint32_t vendorID, uint32_t deviceID) {
    static const uint32_t AMDVendorId = 0x1002;
    if (vendorID != AMDVendorId) {
        return false;
    }

    if (deviceID == 0x743f // Navi 24 [Radeon RX 6400 / 6500 XT]
        || deviceID == 0x7422 // Navi 24 [Radeon PRO W6400]
        || deviceID == 0x7423 // Navi 24 [Radeon PRO W6300/W6300M]
        || deviceID == 0x7424 // Navi 24 [Radeon RX 6300]
        || deviceID == 0x73ef // Navi 23 [Radeon RX 6650 XT]
        || deviceID == 0x7421 // Navi 24 [Radeon PRO W6500M]
        || deviceID == 0x1002 // Navi 21 [Radeon RX 6900 XT]
        || deviceID == 0x73bf // Navi 21 [Radeon RX 6800/6800 XT / 6900 XT]
        || deviceID == 0x73c3 // Navi 22 ??? 
        || deviceID == 0x73e0 // Navi 23 ???
        || deviceID == 0x73df // Navi 22 [Radeon RX 6700/6700 XT/6750 XT / 6800M]
        || deviceID == 0x73ff // Navi 23 [Radeon RX 6600/6600 XT/6600M]
        || deviceID == 0x73e1 // Navi 23 WKS-XM [Radeon PRO W6600M] 
        || deviceID == 0x73e3 // Navi 23 WKS-XL [Radeon PRO W6600],
        || deviceID == 0x731f // Navi 10 [Radeon RX 5600 OEM/5600 XT / 5700/5700 XT]
        || deviceID == 0x7340 // Navi 14 [Radeon RX 5500/5500M / Pro 5500M]

        || deviceID == 0x7341 // Navi 14 [Radeon Pro W5500] 
        || deviceID == 0x7347 // Navi 14 [Radeon Pro W5500M] 
        || deviceID == 0x734f // Navi 14 [Radeon Pro W5300M] 
        || deviceID == 0x7360 // Navi 12 [Radeon Pro 5600M/V520/BC-160]  
        || deviceID == 0x73a5 // Navi 21 [Radeon RX 6950 XT] 
        || deviceID == 0x73a1 // Navi 21 [Radeon Pro V620]  
        || deviceID == 0x7362 // Navi 12 [Radeon Pro V520]

        || deviceID == 0x7310 // Navi 10 [Radeon Pro W5700X] 
        || deviceID == 0x7312 // Navi 10 [Radeon Pro W5700]
        ) { return true; }
    return false;
}

HybridSupportCheck::HybridSupportCheck() {
    LIBRARY_TYPE vulkan_library = nullptr;
    VulcanFunctions vkf;

    if (!LoadVulkanLibrary(vulkan_library)) {
        return;
    }

    if (!LoadGlobalFunctions(vulkan_library, vkf)) {
        UnloadVulkanLibrary(vulkan_library);
        return;
    }

    VkInstance instance = nullptr;

    if (!CreateVulkanInstance(instance, vkf)) {
        UnloadVulkanLibrary(vulkan_library);
        return;
    }

    if (!LoadInstanceFunctions(vulkan_library, instance, vkf)) {
        DestroyInstance(instance, vkf);
        UnloadVulkanLibrary(vulkan_library);
        return;
    }

    std::vector<VkPhysicalDevice> physical_devices;
    if (!EnumeratePhysicalDevices(instance, physical_devices, vkf)) {
        DestroyInstance(instance, vkf);
        UnloadVulkanLibrary(vulkan_library);
        return;
    }

    for (auto& physical_device : physical_devices) {
        VkPhysicalDeviceProperties device_properties;
        GetPhysicalDeviceProperties(physical_device, device_properties, vkf);
        m_supported.push_back(IsDeviceSupported(device_properties.vendorID, device_properties.deviceID));
    }

    DestroyInstance(instance, vkf);
    UnloadVulkanLibrary(vulkan_library);
}

bool HybridSupportCheck::supported(size_t index) {
    return index < m_supported.size() ? m_supported[index] : false;
}
