#include <catch2/catch_all.hpp>
#include "slic3r/GUI/Automation/JsonRpcDispatcher.hpp"
#include "MockUiBackend.hpp"

using namespace Slic3r::GUI::Automation;
using nlohmann::json;

TEST_CASE("dispatch automation.version", "[automation][rpc]") {
    MockUiBackend mock;
    JsonRpcDispatcher d(mock);
    const json req = {{"jsonrpc","2.0"},{"id",1},{"method","automation.version"}};
    const json resp = d.dispatch(req);
    CHECK(resp.at("jsonrpc") == "2.0");
    CHECK(resp.at("id") == 1);
    CHECK(resp.at("result").at("version") == kAutomationVersion);
    CHECK(resp.at("result").at("protocol") == "2.0");
    CHECK(resp.at("result").at("capabilities").is_array());
}

TEST_CASE("unknown method -> -32601", "[automation][rpc]") {
    MockUiBackend mock;
    JsonRpcDispatcher d(mock);
    const json req = {{"jsonrpc","2.0"},{"id",7},{"method","does.not.exist"}};
    const json resp = d.dispatch(req);
    CHECK(resp.at("id") == 7);
    CHECK(resp.at("error").at("code") == kMethodNotFound);
}

TEST_CASE("malformed JSON body -> parse error", "[automation][rpc]") {
    MockUiBackend mock;
    JsonRpcDispatcher d(mock);
    const std::string resp = d.handle_request("{not json");
    const json j = json::parse(resp);
    CHECK(j.at("error").at("code") == kParseError);
    CHECK(j.at("id").is_null());
}

TEST_CASE("missing method field -> invalid request", "[automation][rpc]") {
    MockUiBackend mock;
    JsonRpcDispatcher d(mock);
    const json req = {{"jsonrpc","2.0"},{"id",2}};
    const json resp = d.dispatch(req);
    CHECK(resp.at("error").at("code") == kInvalidRequest);
}

namespace {
UiNode dispatcher_tree() {
    UiNode root; root.klass = "MainFrame"; root.path = "MainFrame";
    UiNode b; b.id = "btn_slice"; b.klass = "Button"; b.label = "Slice plate";
    b.path = "MainFrame/Button[0]"; b.rect = {10,20,100,30};
    UiNode e; e.id = "btn_export"; e.klass = "Button"; e.label = "Export";
    e.path = "MainFrame/Button[1]"; e.enabled = false;
    root.children = {b, e};
    return root;
}
} // namespace

TEST_CASE("tree.dump returns the serialized tree", "[automation][rpc]") {
    MockUiBackend mock; mock.tree = dispatcher_tree();
    JsonRpcDispatcher d(mock);
    const json resp = d.dispatch({{"jsonrpc","2.0"},{"id",1},{"method","tree.dump"}});
    const json& result = resp.at("result");
    CHECK(result.at("class") == "MainFrame");
    CHECK(result.at("children").size() == 2);
    CHECK(mock.refresh_count == 1); // refreshed before reading
}

TEST_CASE("tree.find returns matching nodes", "[automation][rpc]") {
    MockUiBackend mock; mock.tree = dispatcher_tree();
    JsonRpcDispatcher d(mock);
    const json resp = d.dispatch({{"jsonrpc","2.0"},{"id",2},{"method","tree.find"},
                                  {"params",{{"class","Button"}}}});
    CHECK(resp.at("result").size() == 2);
}

TEST_CASE("widget.get returns a single node by id", "[automation][rpc]") {
    MockUiBackend mock; mock.tree = dispatcher_tree();
    JsonRpcDispatcher d(mock);
    const json resp = d.dispatch({{"jsonrpc","2.0"},{"id",3},{"method","widget.get"},
                                  {"params",{{"target",{{"id","btn_slice"}}}}}});
    CHECK(resp.at("result").at("id") == "btn_slice");
}

TEST_CASE("widget.get not found -> 1001", "[automation][rpc]") {
    MockUiBackend mock; mock.tree = dispatcher_tree();
    JsonRpcDispatcher d(mock);
    const json resp = d.dispatch({{"jsonrpc","2.0"},{"id",4},{"method","widget.get"},
                                  {"params",{{"target",{{"id","nope"}}}}}});
    CHECK(resp.at("error").at("code") == kErrNotFound);
}

TEST_CASE("input.click resolves target and clicks it", "[automation][rpc]") {
    MockUiBackend mock; mock.tree = dispatcher_tree();
    JsonRpcDispatcher d(mock);
    const json resp = d.dispatch({{"jsonrpc","2.0"},{"id",1},{"method","input.click"},
                                  {"params",{{"target",{{"id","btn_slice"}}}}}});
    CHECK(resp.at("result").at("ok") == true);
    REQUIRE(mock.clicked_ids.size() == 1);
    CHECK(mock.clicked_ids[0] == "btn_slice");
    CHECK(mock.click_buttons[0] == MouseButton::Left);
}

TEST_CASE("input.click on disabled widget -> 1002", "[automation][rpc]") {
    MockUiBackend mock; mock.tree = dispatcher_tree();
    JsonRpcDispatcher d(mock);
    const json resp = d.dispatch({{"jsonrpc","2.0"},{"id",2},{"method","input.click"},
                                  {"params",{{"target",{{"id","btn_export"}}}}}});
    CHECK(resp.at("error").at("code") == kErrNotActionable);
    CHECK(mock.clicked_ids.empty());
}

TEST_CASE("input.type with target clicks to focus then types", "[automation][rpc]") {
    MockUiBackend mock; mock.tree = dispatcher_tree();
    JsonRpcDispatcher d(mock);
    const json resp = d.dispatch({{"jsonrpc","2.0"},{"id",3},{"method","input.type"},
                                  {"params",{{"target",{{"id","btn_slice"}}},{"text","hello"}}}});
    CHECK(resp.at("result").at("ok") == true);
    CHECK(mock.clicked_ids.size() == 1);          // focused first
    REQUIRE(mock.typed_text.size() == 1);
    CHECK(mock.typed_text[0] == "hello");
}

TEST_CASE("input.key parses 'ctrl+s' string form", "[automation][rpc]") {
    MockUiBackend mock;
    JsonRpcDispatcher d(mock);
    const json resp = d.dispatch({{"jsonrpc","2.0"},{"id",4},{"method","input.key"},
                                  {"params",{{"keys","ctrl+s"}}}});
    CHECK(resp.at("result").at("ok") == true);
    REQUIRE(mock.sent_keys.size() == 1);
    REQUIRE(mock.sent_keys[0].size() == 1);
    CHECK(mock.sent_keys[0][0].key == "s");
    REQUIRE(mock.sent_keys[0][0].modifiers.size() == 1);
    CHECK(mock.sent_keys[0][0].modifiers[0] == KeyModifier::Ctrl);
}

TEST_CASE("input.key parses array form [\"ctrl\",\"s\"]", "[automation][rpc]") {
    MockUiBackend mock;
    JsonRpcDispatcher d(mock);
    const json resp = d.dispatch({{"jsonrpc","2.0"},{"id",5},{"method","input.key"},
                                  {"params",{{"keys", json::array({"ctrl","s"})}}}});
    CHECK(resp.at("result").at("ok") == true);
    REQUIRE(mock.sent_keys[0][0].modifiers.size() == 1);
    CHECK(mock.sent_keys[0][0].key == "s");
}
