#include "AutomationRegistry.hpp"
#include <wx/window.h>
#include <wx/event.h>
#include <mutex>
#include <unordered_map>

namespace Slic3r { namespace GUI { namespace Automation {

namespace {
std::mutex& mtx() { static std::mutex m; return m; }
std::unordered_map<const wxWindow*, std::string>& fwd() {
    static std::unordered_map<const wxWindow*, std::string> m; return m;
}
std::unordered_map<std::string, wxWindow*>& rev() {
    static std::unordered_map<std::string, wxWindow*> m; return m;
}
void erase_window(const wxWindow* w) {
    std::lock_guard<std::mutex> lk(mtx());
    auto it = fwd().find(w);
    if (it != fwd().end()) { rev().erase(it->second); fwd().erase(it); }
}
} // namespace

void set_automation_id(wxWindow* window, const std::string& id) {
    if (window == nullptr || id.empty()) return;
    {
        std::lock_guard<std::mutex> lk(mtx());
        fwd()[window] = id;
        rev()[id]     = window;
    }
    // Prune on destruction.
    window->Bind(wxEVT_DESTROY, [window](wxWindowDestroyEvent& e) {
        erase_window(window);
        e.Skip();
    });
}

std::string automation_id_of(const wxWindow* window) {
    std::lock_guard<std::mutex> lk(mtx());
    auto it = fwd().find(window);
    return it == fwd().end() ? std::string() : it->second;
}

wxWindow* window_for_automation_id(const std::string& id) {
    std::lock_guard<std::mutex> lk(mtx());
    auto it = rev().find(id);
    return it == rev().end() ? nullptr : it->second;
}

}}} // namespace
