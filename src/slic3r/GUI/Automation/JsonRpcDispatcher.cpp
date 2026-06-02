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

namespace {
std::optional<std::string> opt_str(const nlohmann::json& p, const char* key) {
    if (p.is_object() && p.contains(key) && p.at(key).is_string())
        return p.at(key).get<std::string>();
    return std::nullopt;
}

Target parse_target(const nlohmann::json& tj) {
    Target t;
    if (!tj.is_object()) return t;
    t.id    = opt_str(tj, "id");
    t.path  = opt_str(tj, "path");
    t.name  = opt_str(tj, "name");
    t.klass = opt_str(tj, "class");
    t.label = opt_str(tj, "label");
    t.value = opt_str(tj, "value");
    if (auto b = opt_str(tj, "backend"))
        t.backend = (*b == "imgui") ? BackendKind::ImGui : BackendKind::Wx;
    return t;
}

DumpOptions parse_dump_options(const nlohmann::json& p) {
    DumpOptions o;
    if (p.is_object()) {
        if (p.contains("root"))         o.root = opt_str(p, "root");
        if (p.contains("max_depth") && p.at("max_depth").is_number_integer())
            o.max_depth = p.at("max_depth").get<int>();
        if (p.contains("visible_only") && p.at("visible_only").is_boolean())
            o.visible_only = p.at("visible_only").get<bool>();
        if (p.contains("include_imgui") && p.at("include_imgui").is_boolean())
            o.include_imgui = p.at("include_imgui").get<bool>();
    }
    return o;
}
} // namespace

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

// --- method handlers implemented in Tasks 7-10 (remaining stubs throw for now) ---
nlohmann::json JsonRpcDispatcher::m_tree_dump(const nlohmann::json& params) {
    m_backend.refresh_ui();
    const UiNode root = m_backend.dump_tree(parse_dump_options(params));
    return node_to_json(root, /*include_children*/ true);
}

nlohmann::json JsonRpcDispatcher::m_tree_find(const nlohmann::json& params) {
    m_backend.refresh_ui();
    const UiNode root = m_backend.dump_tree(DumpOptions{});
    const Target target =
        parse_target(params.is_object() ? params : nlohmann::json::object());
    nlohmann::json arr = nlohmann::json::array();
    for (const UiNode* n : find_matches(root, target))
        arr.push_back(node_to_json(*n, /*include_children*/ false));
    return arr;
}

nlohmann::json JsonRpcDispatcher::m_widget_get(const nlohmann::json& params) {
    if (!params.is_object() || !params.contains("target"))
        throw AutomationError(kInvalidParams, "widget.get requires 'target'");
    m_backend.refresh_ui();
    const UiNode root = m_backend.dump_tree(DumpOptions{});
    int count = 0;
    const UiNode* node = resolve_unique(root, parse_target(params.at("target")), count);
    if (count == 0) throw AutomationError(kErrNotFound, "target not found");
    if (count > 1)  throw AutomationError(kErrNotFound, "target is ambiguous");
    return node_to_json(*node, /*include_children*/ true);
}

nlohmann::json JsonRpcDispatcher::m_input_click(const nlohmann::json&)          { throw AutomationError(kMethodNotFound, "not implemented"); }
nlohmann::json JsonRpcDispatcher::m_input_type(const nlohmann::json&)           { throw AutomationError(kMethodNotFound, "not implemented"); }
nlohmann::json JsonRpcDispatcher::m_input_key(const nlohmann::json&)            { throw AutomationError(kMethodNotFound, "not implemented"); }
nlohmann::json JsonRpcDispatcher::m_sync_wait_for(const nlohmann::json&)        { throw AutomationError(kMethodNotFound, "not implemented"); }
nlohmann::json JsonRpcDispatcher::m_app_state(const nlohmann::json&)            { throw AutomationError(kMethodNotFound, "not implemented"); }
nlohmann::json JsonRpcDispatcher::m_screenshot_window(const nlohmann::json&)    { throw AutomationError(kMethodNotFound, "not implemented"); }
nlohmann::json JsonRpcDispatcher::m_screenshot_viewport3d(const nlohmann::json&){ throw AutomationError(kMethodNotFound, "not implemented"); }

}}} // namespace
