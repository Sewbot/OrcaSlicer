#pragma once
#include "slic3r/GUI/Automation/IUiBackend.hpp"
#include "slic3r/GUI/Automation/JsonRpcDispatcher.hpp" // kErrLoadFailed
#include <functional>
#include <vector>

namespace Slic3r { namespace GUI { namespace Automation {

// Deterministic fake backend for dispatcher tests. Records every primitive call
// and returns canned data. `tree_provider` lets a test return different trees on
// successive dump_tree() calls (used for sync.wait_for tests).
class MockUiBackend : public IUiBackend {
public:
    // Recorded calls (inspected by tests).
    int               refresh_count = 0;
    int               dump_count    = 0;
    std::vector<std::string> clicked_ids;       // node.id of each click()
    std::vector<MouseButton> click_buttons;
    std::vector<std::string> typed_text;
    std::vector<std::vector<KeyChord>> sent_keys;
    int               screenshot_window_count   = 0;
    std::vector<std::vector<std::string>> opened_paths; // paths of each open_files()

    // Canned outputs (set by tests).
    UiNode   tree;                                  // default tree for dump_tree
    AppState state;
    PngImage canned_png{ {0x89,0x50,0x4E,0x47}, 4, 4 }; // fake "PNG" bytes
    bool     click_result = true;
    int      open_return_count = 0;     // value open_files() returns
    bool     open_should_fail = false;  // when true, open_files() throws kErrLoadFailed

    // Optional: per-call tree provider (overrides `tree` when set).
    std::function<UiNode(int /*call_index*/)> tree_provider;

    void     refresh_ui() override { ++refresh_count; }
    UiNode   dump_tree(const DumpOptions&) override {
        const int idx = dump_count++;
        return tree_provider ? tree_provider(idx) : tree;
    }
    AppState app_state() override { return state; }
    bool     click(const UiNode& node, MouseButton button, bool /*dbl*/,
                   const std::vector<KeyModifier>&) override {
        clicked_ids.push_back(node.id);
        click_buttons.push_back(button);
        return click_result;
    }
    bool     type_text(const std::string& text) override {
        typed_text.push_back(text); return true;
    }
    bool     send_keys(const std::vector<KeyChord>& chords) override {
        sent_keys.push_back(chords); return true;
    }
    PngImage screenshot_window(const UiNode*) override {
        ++screenshot_window_count; return canned_png;
    }
    int      open_files(const std::vector<std::string>& paths) override {
        opened_paths.push_back(paths);
        if (open_should_fail)
            throw AutomationError(kErrLoadFailed, "mock load failed");
        return open_return_count;
    }
};

}}} // namespace
