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
