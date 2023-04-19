#include "resourceWatcher.h"

#ifdef BUILD_AS_HOUDINI_PLUGIN
#include <iostream>
#include <thread>
#include <chrono>
#include <memory>
#include <map>
#include <hboost/interprocess/detail/os_thread_functions.hpp>
#include <hboost/interprocess/sync/interprocess_condition.hpp>
#include <hboost/interprocess/sync/interprocess_mutex.hpp>
#include <hboost/interprocess/shared_memory_object.hpp>
#include <hboost/interprocess/mapped_region.hpp>
#include <hboost/interprocess/sync/scoped_lock.hpp>

#include <HOM/HOM_Module.h>
#include <HOM/HOM_Node.h>
#include <HOM/HOM_ui.h>
#include <HOM/HOM_SceneViewer.h>

#include <HOM/HOM_ChopNode.h>
#include <HOM/HOM_CopNode.h>
#include <HOM/HOM_DopNode.h>
#include <HOM/HOM_LopNode.h>
#include <HOM/HOM_RopNode.h>
#include <HOM/HOM_SopNode.h>
#include <HOM/HOM_TopNode.h>
#include <HOM/HOM_VopNode.h>
#include "pxr/base/arch/env.h"
#include "pxr/imaging/rprUsd/config.h"
#include <ghc/filesystem.hpp>
namespace fs = ghc::filesystem;

PXR_NAMESPACE_OPEN_SCOPE

#ifdef GET_IS_BYPASSED
#error "GET_IS_BYPASSED is defined elsewhere"
#else
#define GET_IS_BYPASSED(classname) \
    {\
        classname* n = dynamic_cast<classname*>(node); \
        if (n) { \
            hasBypassParam = true; \
            bypass = n->isBypassed(); \
            return; \
        } \
    }
#endif

void GetBypassed(HOM_Node* node, bool& hasBypassParam, bool& bypass) {
    GET_IS_BYPASSED(HOM_ChopNode)
    GET_IS_BYPASSED(HOM_CopNode)
    GET_IS_BYPASSED(HOM_DopNode)
    GET_IS_BYPASSED(HOM_LopNode)
    GET_IS_BYPASSED(HOM_RopNode)
    GET_IS_BYPASSED(HOM_SopNode)
    GET_IS_BYPASSED(HOM_TopNode)
    GET_IS_BYPASSED(HOM_VopNode)
    hasBypassParam = false;
    bypass = false;
}

#undef GET_IS_BYPASSED

#ifdef SET_BYPASS
#error "SET_BYPASS is defined elsewhere"
#else
#define SET_BYPASS(classname) \
    {\
        classname* n = dynamic_cast<classname*>(node); \
        if (n) { \
            n->bypass(bypass); \
            return; \
        } \
    }
#endif

void SetBypassed(HOM_Node* node, bool bypass) {
    SET_BYPASS(HOM_ChopNode)
    SET_BYPASS(HOM_CopNode)
    SET_BYPASS(HOM_DopNode)
    SET_BYPASS(HOM_LopNode)
    SET_BYPASS(HOM_RopNode)
    SET_BYPASS(HOM_SopNode)
    SET_BYPASS(HOM_TopNode)
    SET_BYPASS(HOM_VopNode)
}

#undef SET_BYPASS

typedef std::map<std::string, bool> NodesToRestoreSet;

bool ResourceManagementActive(HOM_Module& hom) {
    auto result = hom.hscript("if( $RPR_MEM_MANAGEMENT ) then\necho 1;\nelse\necho 0;\nendif");
    return result.size() > 0 && result[0].size() > 0 && result[0][0] == '1';
}

void ReadMemManagementFlag() {
    HOM_Module& hom = HOM();
    RprUsdConfig* config;
    auto configLock = RprUsdConfig::GetInstance(&config);
    hom.hscript(config->GetMemManagement() ? "set -g RPR_MEM_MANAGEMENT = 1" : "set -g RPR_MEM_MANAGEMENT = 0");
    hom.hscript("varchange RPR_MEM_MANAGEMENT");
}

void WriteMemManagementFlag() {
    HOM_Module& hom = HOM();
    RprUsdConfig* config;
    auto configLock = RprUsdConfig::GetInstance(&config);
    config->SetMemManagement(ResourceManagementActive(hom));
}

bool IsHoudiniInstance() {
    static bool tested = false;
    static bool isHoudiniInstance = false;
    if (!tested) {
        HOM_Module& hom = HOM();
        isHoudiniInstance = hom.applicationName().rfind("houdini", 0) == 0;
    }
    return isHoudiniInstance;
}

void DeActivateScene(NodesToRestoreSet& nodesToRestore) {
    HOM_Module& hom = HOM();
    if (nodesToRestore.size() != 0 // already deactivated
        || !IsHoudiniInstance() || ResourceManagementActive(hom))
    {
        return;
    }

    HOM_Node* root = hom.root();
    auto children = root->children();
    for (HOM_ElemPtr<HOM_Node>& c : children) {
        if (c.myPointer->name() == "stage") {
            auto schildren = c.myPointer->children();
            for (HOM_ElemPtr<HOM_Node>& sc : schildren) {
                bool hasBypassParam;
                bool bypass;
                GetBypassed(sc.myPointer, hasBypassParam, bypass);
                if (hasBypassParam) {
                    nodesToRestore.emplace(sc.myPointer->name(), bypass);
                    SetBypassed(sc.myPointer, true);
                }
            }
        }
    }

    HOM_ui& ui = hom.ui();
    HOM_SceneViewer* sceneViewer = dynamic_cast<HOM_SceneViewer*>(ui.paneTabOfType(HOM_paneTabType::SceneViewer));
    if (sceneViewer) {
        sceneViewer->restartRenderer();
    }
}

void ActivateScene(NodesToRestoreSet& nodesToRestore) {
    if (!IsHoudiniInstance())
    {
        return;
    }

    HOM_Module& hom = HOM();
    HOM_Node* root = hom.root();
    auto children = root->children();
    for (HOM_ElemPtr<HOM_Node>& c : children) {
        if (c.myPointer->name() == "stage") {
            auto schildren = c.myPointer->children();
            for (HOM_ElemPtr<HOM_Node>& sc : schildren) {
                auto it = nodesToRestore.find(sc.myPointer->name());
                if (it != nodesToRestore.end()) {
                    SetBypassed(sc.myPointer, (*it).second);
                }
            }
        }
    }
    nodesToRestore.clear();
}

enum class MessageType { Started, Finished, Live };

struct MessageData {
    hboost::interprocess::ipcdetail::OS_process_id_t pid;
    MessageType messageType;
};

struct InterprocessMessage
{
    InterprocessMessage() : messageIn(false) {}
    hboost::interprocess::interprocess_mutex      mutex;
    hboost::interprocess::interprocess_condition  condEmpty;
    hboost::interprocess::interprocess_condition  condFull;
    MessageData content;
    bool messageIn;
};

void Notify(InterprocessMessage* message, bool started);

class ResourceWatcher {
public:
    ResourceWatcher(): m_shm(hboost::interprocess::open_or_create, "RprResourceWatcher", hboost::interprocess::read_write), m_message(nullptr) {}

    bool Init() {
        try {
            ReadMemManagementFlag();
            m_shm.truncate(sizeof(InterprocessMessage));
            m_region = std::make_unique<hboost::interprocess::mapped_region>(m_shm, hboost::interprocess::read_write);
            void* addr = m_region->get_address();
            m_message = new (addr) InterprocessMessage;
        }
        catch (hboost::interprocess::interprocess_exception& ex) {
            std::cout << "Resource watcher failure: " << ex.what() << std::endl;
            return false;
        }
        return true;
    }

    InterprocessMessage* GetInterprocMessage() { return m_message; }
private:
    hboost::interprocess::shared_memory_object m_shm;
    std::unique_ptr<hboost::interprocess::mapped_region> m_region;
    InterprocessMessage* m_message;
};

static ResourceWatcher resourceWatcher;
static std::thread* listenerThread = nullptr;
static std::thread* checkAliveThread = nullptr;
std::mutex timePointsLock;
std::map<hboost::interprocess::ipcdetail::OS_process_id_t, std::chrono::steady_clock::time_point> timePoints;
NodesToRestoreSet nodesToRestore;

void Listen(InterprocessMessage* message)
{
    try {
        do {
            hboost::interprocess::scoped_lock<hboost::interprocess::interprocess_mutex> lock(message->mutex);
            if (!message->messageIn) {
                message->condEmpty.wait(lock);
            }
            else {
                if (message->content.pid != hboost::interprocess::ipcdetail::get_current_process_id()) {      // Ignore messages from the same process
                    if (message->content.messageType == MessageType::Started) {
                        fprintf(stdout, "RCV\n");
                        std::lock_guard<std::mutex> lock(timePointsLock);
                        timePoints[message->content.pid] = std::chrono::steady_clock::now();
                        DeActivateScene(nodesToRestore);
                    }
                    else if (message->content.messageType == MessageType::Finished) {
                        std::lock_guard<std::mutex> lock(timePointsLock);
                        timePoints.erase(message->content.pid);
                        ActivateScene(nodesToRestore);
                    }
                    else if (message->content.messageType == MessageType::Live) {
                        std::lock_guard<std::mutex> lock(timePointsLock);
                        timePoints[message->content.pid] = std::chrono::steady_clock::now();
                    }
                }

                message->messageIn = false;
                message->condFull.notify_all();
            }
        } while (true);
    }
    catch (hboost::interprocess::interprocess_exception& ex) {
        std::cout << "Resource watcher failure: " << ex.what() << std::endl;
    }
}

void NotifyLive(InterprocessMessage* message) {
    try {
        auto pid = hboost::interprocess::ipcdetail::get_current_process_id();
        while (true) {
            {   // code block where the mutex is locked
                hboost::interprocess::scoped_lock<hboost::interprocess::interprocess_mutex> lock(message->mutex);
                if (message->messageIn) {
                    message->condFull.wait(lock);
                }
                message->content.pid = pid;
                message->content.messageType = MessageType::Live;
                message->condEmpty.notify_all();
                message->messageIn = true; 
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    catch (hboost::interprocess::interprocess_exception& ex) {
        std::cout << ex.what() << std::endl;
    }
}

void CheckLive(InterprocessMessage* message) {
    while (true) {
        {   // code block where the mutex is locked
            std::lock_guard<std::mutex> lock(timePointsLock);
            bool anyAlive = false;
            for (auto it = timePoints.begin(); it != timePoints.end(); ++it) {
                auto interval = std::chrono::steady_clock::now().time_since_epoch() - (*it).second.time_since_epoch();
                double seconds = (double)interval.count() / 1000000000.0;
                int maxTimeoutSeconds = 3;
                if (seconds < maxTimeoutSeconds) {
                    anyAlive = true;
                    break;
                }
            }
            if (!anyAlive) {
                timePoints.clear();
                if (nodesToRestore.size() != 0) {
                    ActivateScene(nodesToRestore);
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void Notify(InterprocessMessage* message, bool started) {
    try {
        hboost::interprocess::scoped_lock<hboost::interprocess::interprocess_mutex> lock(message->mutex);
        if (message->messageIn) {
            message->condFull.wait(lock);
        }
        message->content.pid = hboost::interprocess::ipcdetail::get_current_process_id();
        message->content.messageType = started ? MessageType::Started : MessageType::Finished;
        message->condEmpty.notify_all();
        message->messageIn = true;
    }
    catch (hboost::interprocess::interprocess_exception& ex) {
        std::cout << ex.what() << std::endl;
    }
}

void InitWatcher() {
    if (!checkAliveThread) {
        resourceWatcher.Init();
        checkAliveThread = new std::thread(IsHoudiniInstance() ? CheckLive : NotifyLive, resourceWatcher.GetInterprocMessage());
    }
    if (!listenerThread) {
        listenerThread = new std::thread(Listen, resourceWatcher.GetInterprocMessage());
    }
}

void NotifyRenderStarted() {
    Notify(resourceWatcher.GetInterprocMessage(), true);
}

void NotifyRenderFinished() {
    WriteMemManagementFlag();   // calls on render delegate destructor, just as needed
    Notify(resourceWatcher.GetInterprocMessage(), false);
}

#else

PXR_NAMESPACE_OPEN_SCOPE

void InitWatcher() {}
void NotifyRenderStarted() {}
void NotifyRenderFinished() {}

#endif // BUILD_AS_HOUDINI_PLUGIN

PXR_NAMESPACE_CLOSE_SCOPE
