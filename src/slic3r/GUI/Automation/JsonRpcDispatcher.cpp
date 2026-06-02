#include "JsonRpcDispatcher.hpp"
#include "WidgetSerializer.hpp"
#include "Locator.hpp"
#include <chrono>
#include <thread>

namespace Slic3r { namespace GUI { namespace Automation {

JsonRpcDispatcher::JsonRpcDispatcher(IUiBackend& backend) : m_backend(backend) {}

nlohmann::json JsonRpcDispatcher::make_result(const nlohmann::json& id, nlohmann::json result) {
    return { {"jsonrpc","2.0"}, {"id", id}, {"result", std::move(result)} };
}

nlohmann::json JsonRpcDispatcher::make_error(const nlohmann::json& id, int code,
                                             const std::string& msg) {
    return { {"jsonrpc","2.0"}, {"id", id},
             {"error", { {"code", code}, {"message", msg} }} };
}

nlohmann::json JsonRpcDispatcher::m_version(const nlohmann::json&) {
    return { {"version", kAutomationVersion},
             {"protocol", "2.0"},
             {"capabilities", nlohmann::json::array({
                 "tree.dump","tree.find","widget.get","input.click","input.type",
                 "input.key","sync.wait_for","app.state","screenshot.window",
                 "screenshot.viewport3d" })} };
}

nlohmann::json JsonRpcDispatcher::dispatch(const nlohmann::json& request) {
    nlohmann::json id = request.contains("id") ? request.at("id") : nlohmann::json(nullptr);

    if (!request.is_object() || !request.contains("method") ||
        !request.at("method").is_string()) {
        return make_error(id, kInvalidRequest, "missing or invalid 'method'");
    }
    const std::string method = request.at("method").get<std::string>();
    const nlohmann::json params =
        request.contains("params") ? request.at("params") : nlohmann::json::object();

    try {
        if (method == "automation.version")        return make_result(id, m_version(params));
        if (method == "tree.dump")                 return make_result(id, m_tree_dump(params));
        if (method == "tree.find")                 return make_result(id, m_tree_find(params));
        if (method == "widget.get")                return make_result(id, m_widget_get(params));
        if (method == "input.click")               return make_result(id, m_input_click(params));
        if (method == "input.type")                return make_result(id, m_input_type(params));
        if (method == "input.key")                 return make_result(id, m_input_key(params));
        if (method == "sync.wait_for")             return make_result(id, m_sync_wait_for(params));
        if (method == "app.state")                 return make_result(id, m_app_state(params));
        if (method == "screenshot.window")         return make_result(id, m_screenshot_window(params));
        if (method == "screenshot.viewport3d")     return make_result(id, m_screenshot_viewport3d(params));
        return make_error(id, kMethodNotFound, "unknown method: " + method);
    } catch (const AutomationError& e) {
        return make_error(id, e.code, e.what());
    } catch (const std::exception& e) {
        return make_error(id, kInvalidParams, e.what());
    }
}

std::string JsonRpcDispatcher::handle_request(const std::string& body) {
    nlohmann::json req;
    try {
        req = nlohmann::json::parse(body);
    } catch (const std::exception& e) {
        return make_error(nullptr, kParseError, std::string("parse error: ") + e.what()).dump();
    }
    return dispatch(req).dump();
}

// --- method handlers implemented in Tasks 7-10 (stubs throw for now) ---
nlohmann::json JsonRpcDispatcher::m_tree_dump(const nlohmann::json&)            { throw AutomationError(kMethodNotFound, "not implemented"); }
nlohmann::json JsonRpcDispatcher::m_tree_find(const nlohmann::json&)            { throw AutomationError(kMethodNotFound, "not implemented"); }
nlohmann::json JsonRpcDispatcher::m_widget_get(const nlohmann::json&)           { throw AutomationError(kMethodNotFound, "not implemented"); }
nlohmann::json JsonRpcDispatcher::m_input_click(const nlohmann::json&)          { throw AutomationError(kMethodNotFound, "not implemented"); }
nlohmann::json JsonRpcDispatcher::m_input_type(const nlohmann::json&)           { throw AutomationError(kMethodNotFound, "not implemented"); }
nlohmann::json JsonRpcDispatcher::m_input_key(const nlohmann::json&)            { throw AutomationError(kMethodNotFound, "not implemented"); }
nlohmann::json JsonRpcDispatcher::m_sync_wait_for(const nlohmann::json&)        { throw AutomationError(kMethodNotFound, "not implemented"); }
nlohmann::json JsonRpcDispatcher::m_app_state(const nlohmann::json&)            { throw AutomationError(kMethodNotFound, "not implemented"); }
nlohmann::json JsonRpcDispatcher::m_screenshot_window(const nlohmann::json&)    { throw AutomationError(kMethodNotFound, "not implemented"); }
nlohmann::json JsonRpcDispatcher::m_screenshot_viewport3d(const nlohmann::json&){ throw AutomationError(kMethodNotFound, "not implemented"); }

}}} // namespace
