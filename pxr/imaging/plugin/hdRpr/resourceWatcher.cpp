#include "resourceWatcher.h"

#ifdef BUILD_AS_HOUDINI_PLUGIN
#include <iostream>
#include <thread>
#include <memory>
#include <map>
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

void DeActivateScene(NodesToRestoreSet& nodesToRestore) {
    nodesToRestore.clear();
    HOM_Module& hom = HOM();
    if (hom.applicationName().rfind("houdini", 0) != 0) {
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
    HOM_Module& hom = HOM();
    if (hom.applicationName().rfind("houdini", 0) != 0) {
        return;
    }

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
}

using namespace hboost::interprocess;

struct MessageData {
    ipcdetail::OS_process_id_t pid;
    bool started;
};

struct InterprocessMessage
{
    InterprocessMessage() : message_in(false) {}
    hboost::interprocess::interprocess_mutex      mutex;
    hboost::interprocess::interprocess_condition  cond_empty;
    hboost::interprocess::interprocess_condition  cond_full;
    MessageData content;
    bool message_in;
};

void Notify(InterprocessMessage* message, bool started);

class ResourceWatcher {
public:
    ResourceWatcher(): m_shm(open_or_create, "RprResourceWatcher", read_write), m_message(nullptr) {}
    ~ResourceWatcher() {
        try {
            Notify(m_message, false);
        }
        catch (...) {}
        if (m_message) {
            m_message->~InterprocessMessage();
        }
    }

    bool Init() {
        try {
            m_shm.truncate(sizeof(InterprocessMessage));
            m_region = std::make_unique<mapped_region>(m_shm, read_write);
            void* addr = m_region->get_address();
            m_message = new (addr) InterprocessMessage;
        }
        catch (interprocess_exception& ex) {
            std::cout << "Resource watcher failure: " << ex.what() << std::endl;
            return false;
        }
        return true;
    }

    InterprocessMessage* GetInterprocMessage() { return m_message; }
private:
    shared_memory_object m_shm;
    std::unique_ptr<mapped_region> m_region;
    InterprocessMessage* m_message;
};

static ResourceWatcher resourceWatcher;
static std::thread* listenerThread = nullptr;

void Listen(InterprocessMessage* message)
{
    static NodesToRestoreSet nodesToRestore;
    try {
        do {
            scoped_lock<interprocess_mutex> lock(message->mutex);
            if (!message->message_in) {
                message->cond_empty.wait(lock);
            }
            else {
                if (message->content.pid != hboost::interprocess::ipcdetail::get_current_process_id()) {      // Ignore messages from the same process
                    if (message->content.started) {
                        fprintf(stdout, "RCV\n");
                        DeActivateScene(nodesToRestore);
                    }
                    else {
                        ActivateScene(nodesToRestore);
                    }
                }

                message->message_in = false;
                message->cond_full.notify_all();
            }
        } while (true);
    }
    catch (interprocess_exception& ex) {
        std::cout << "Resource watcher failure: " << ex.what() << std::endl;
    }
}

void InitWatcher() {
    if (!listenerThread) {
        resourceWatcher.Init();
        listenerThread = new std::thread(Listen, resourceWatcher.GetInterprocMessage());
    }
}

void Notify(InterprocessMessage* message, bool started) {
    try {
        scoped_lock<interprocess_mutex> lock(message->mutex);
        if (message->message_in) {
            message->cond_full.wait(lock);
        }
        message->content.pid = hboost::interprocess::ipcdetail::get_current_process_id();
        message->content.started = started;

        //Notify to the other process that there is a message
        message->cond_empty.notify_all();

        //Mark message buffer as full
        message->message_in = true;
    }
    catch (interprocess_exception& ex) {
        std::cout << ex.what() << std::endl;
    }
}

void NotifyRenderStarted() {
    Notify(resourceWatcher.GetInterprocMessage(), true);
}

void NotifyRenderFinished() {
    Notify(resourceWatcher.GetInterprocMessage(), false);
}

#else

void InitWatcher() {}
void NotifyRenderStarted() {}
void NotifyRenderFinished() {}

#endif // BUILD_AS_HOUDINI_PLUGIN
