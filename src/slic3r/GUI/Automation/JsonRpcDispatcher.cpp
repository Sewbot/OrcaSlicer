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

namespace {
MouseButton parse_button(const nlohmann::json& p) {
    auto b = opt_str(p, "button");
    if (b && *b == "right")  return MouseButton::Right;
    if (b && *b == "middle") return MouseButton::Middle;
    return MouseButton::Left;
}

std::vector<KeyModifier> parse_modifiers(const nlohmann::json& p) {
    std::vector<KeyModifier> mods;
    if (p.is_object() && p.contains("modifiers") && p.at("modifiers").is_array()) {
        for (const auto& m : p.at("modifiers")) {
            if (!m.is_string()) continue;
            const std::string s = m.get<std::string>();
            if (s == "ctrl")  mods.push_back(KeyModifier::Ctrl);
            else if (s == "shift") mods.push_back(KeyModifier::Shift);
            else if (s == "alt")   mods.push_back(KeyModifier::Alt);
            else if (s == "cmd" || s == "meta") mods.push_back(KeyModifier::Cmd);
        }
    }
    return mods;
}

// Parse one chord token list (already split): the last token is the key, the
// earlier ones are modifiers.
KeyChord chord_from_tokens(const std::vector<std::string>& tokens) {
    KeyChord c;
    for (size_t i = 0; i < tokens.size(); ++i) {
        const std::string& t = tokens[i];
        const bool is_mod = (t == "ctrl" || t == "shift" || t == "alt" ||
                             t == "cmd" || t == "meta");
        if (is_mod && i + 1 < tokens.size()) {
            if (t == "ctrl")  c.modifiers.push_back(KeyModifier::Ctrl);
            else if (t == "shift") c.modifiers.push_back(KeyModifier::Shift);
            else if (t == "alt")   c.modifiers.push_back(KeyModifier::Alt);
            else                   c.modifiers.push_back(KeyModifier::Cmd);
        } else {
            c.key = t; // last token (or a lone token) is the key
        }
    }
    return c;
}

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> out;
    std::string cur;
    for (char ch : s) {
        if (ch == delim) { if (!cur.empty()) out.push_back(cur); cur.clear(); }
        else cur.push_back(ch);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// "keys" may be a string ("ctrl+s") or an array (["ctrl","s"]). Returns one chord.
std::vector<KeyChord> parse_keys(const nlohmann::json& params) {
    if (!params.is_object() || !params.contains("keys"))
        throw AutomationError(kInvalidParams, "input.key requires 'keys'");
    const auto& k = params.at("keys");
    std::vector<std::string> tokens;
    if (k.is_string()) {
        tokens = split(k.get<std::string>(), '+');
    } else if (k.is_array()) {
        for (const auto& e : k)
            if (e.is_string()) tokens.push_back(e.get<std::string>());
    } else {
        throw AutomationError(kInvalidParams, "'keys' must be string or array");
    }
    if (tokens.empty())
        throw AutomationError(kInvalidParams, "'keys' is empty");
    return { chord_from_tokens(tokens) };
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

const UiNode JsonRpcDispatcher::resolve_actionable(const nlohmann::json& params,
                                                   UiNode& tree_out) {
    if (!params.is_object() || !params.contains("target"))
        throw AutomationError(kInvalidParams, "missing 'target'");
    m_backend.refresh_ui();
    tree_out = m_backend.dump_tree(DumpOptions{});
    int count = 0;
    const UiNode* node = resolve_unique(tree_out, parse_target(params.at("target")), count);
    if (count == 0) throw AutomationError(kErrNotFound, "target not found");
    if (count > 1)  throw AutomationError(kErrNotFound, "target is ambiguous");
    if (!node->enabled || !node->visible)
        throw AutomationError(kErrNotActionable, "target is disabled or hidden");
    return *node; // copy: stable even though tree_out outlives this call
}

nlohmann::json JsonRpcDispatcher::m_input_click(const nlohmann::json& params) {
    UiNode tree;
    const UiNode node = resolve_actionable(params, tree);
    const bool dbl = params.contains("double") && params.at("double").is_boolean()
                         && params.at("double").get<bool>();
    const bool ok = m_backend.click(node, parse_button(params), dbl, parse_modifiers(params));
    return { {"ok", ok} };
}

nlohmann::json JsonRpcDispatcher::m_input_type(const nlohmann::json& params) {
    if (!params.is_object() || !params.contains("text") || !params.at("text").is_string())
        throw AutomationError(kInvalidParams, "input.type requires string 'text'");
    const std::string text = params.at("text").get<std::string>();
    // Optional target: click to focus first.
    if (params.contains("target")) {
        UiNode tree;
        const UiNode node = resolve_actionable(params, tree);
        m_backend.click(node, MouseButton::Left, false, {});
    }
    const bool ok = m_backend.type_text(text);
    return { {"ok", ok} };
}

nlohmann::json JsonRpcDispatcher::m_input_key(const nlohmann::json& params) {
    const bool ok = m_backend.send_keys(parse_keys(params));
    return { {"ok", ok} };
}

nlohmann::json JsonRpcDispatcher::m_sync_wait_for(const nlohmann::json&)        { throw AutomationError(kMethodNotFound, "not implemented"); }
nlohmann::json JsonRpcDispatcher::m_app_state(const nlohmann::json&)            { throw AutomationError(kMethodNotFound, "not implemented"); }
nlohmann::json JsonRpcDispatcher::m_screenshot_window(const nlohmann::json&)    { throw AutomationError(kMethodNotFound, "not implemented"); }
nlohmann::json JsonRpcDispatcher::m_screenshot_viewport3d(const nlohmann::json&){ throw AutomationError(kMethodNotFound, "not implemented"); }

}}} // namespace
