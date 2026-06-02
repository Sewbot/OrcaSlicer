#pragma once
#include <cstdint>
#include <string>

class wxWindow;

namespace Slic3r { namespace GUI { namespace Automation {

// Process-wide wxWindow* <-> automation_id side map. Header is dependency-light so
// widget-construction code can call set_automation_id() unconditionally — it is a
// cheap, safe registration that no-ops when the window is null.
//
// Registration is pruned automatically when the window is destroyed (bound to
// wxEVT_DESTROY inside set_automation_id).
void        set_automation_id(wxWindow* window, const std::string& id);
std::string automation_id_of(const wxWindow* window);   // "" if none
wxWindow*   window_for_automation_id(const std::string& id); // nullptr if none

}}} // namespace
