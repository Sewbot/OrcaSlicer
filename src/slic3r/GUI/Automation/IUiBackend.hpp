#pragma once
// PURE header: no wx / ImGui / GL includes. Safe to compile in the display-free
// unit-test target. Shared by the dispatcher, serializer, locator, and backends.
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace Slic3r { namespace GUI { namespace Automation {

enum class BackendKind { Wx, ImGui };

struct Rect { int x = 0, y = 0, w = 0, h = 0; };

// One node of the unified UI tree. `handle` is opaque (wxWindow* cast to uintptr_t
// for wx, item index for ImGui); it is used by WxUiBackend to recover concrete
// objects and is NEVER serialized.
struct UiNode {
    BackendKind         backend = BackendKind::Wx;
    std::string         id;       // automation id if set, else derived path id
    std::string         path;     // positional path, e.g. "MainFrame/Panel[2]/Button[0]"
    std::string         klass;    // wx class name or imgui item type
    std::string         label;
    Rect                rect;     // screen coordinates
    bool                enabled = true;
    bool                visible = true;
    bool                has_value = false;
    std::string         value;    // when applicable (text/choice/check/slider)
    std::uint64_t       handle = 0;
    std::vector<UiNode> children; // wx only; imgui items are flat under their window
};

struct DumpOptions {
    std::optional<std::string> root;          // id/path to root the dump at
    int                        max_depth = -1; // -1 = unlimited
    bool                       visible_only = false;
    bool                       include_imgui = true;
};

enum class MouseButton { Left, Right, Middle };
enum class KeyModifier { Ctrl, Shift, Alt, Cmd };

struct KeyChord {
    std::vector<KeyModifier> modifiers;
    std::string              key; // normalized lowercase: "s", "enter", "f5", "tab", ...
};

struct AppState {
    std::string                active_tab;
    bool                       project_loaded = false;
    bool                       slicing = false;
    int                        slice_progress = -1; // -1 = unknown
    std::optional<std::string> modal_dialog;
    bool                       foreground = false;
};

struct PngImage {
    std::vector<unsigned char> png; // encoded PNG bytes
    int                        width = 0;
    int                        height = 0;
};

// Thrown by backends/dispatcher; carries a JSON-RPC application error code.
struct AutomationError : std::runtime_error {
    int code;
    AutomationError(int code, std::string msg)
        : std::runtime_error(std::move(msg)), code(code) {}
};

// Backend abstraction. The dispatcher orchestrates; the backend only snapshots
// and executes primitives on already-resolved nodes.
class IUiBackend {
public:
    virtual ~IUiBackend() = default;

    // Force a fresh frame so transient ImGui items are recorded before a read or
    // action. No-op for non-GUI backends.
    virtual void refresh_ui() = 0;

    // Snapshot the UI tree (wx hierarchy + flat imgui items under their windows).
    virtual UiNode dump_tree(const DumpOptions& opts) = 0;

    // Application-level state snapshot.
    virtual AppState app_state() = 0;

    // Click a resolved node (uses its rect/handle). Raises/focuses first.
    virtual bool click(const UiNode& node, MouseButton button, bool dbl,
                       const std::vector<KeyModifier>& modifiers) = 0;
    // Type into the currently-focused control.
    virtual bool type_text(const std::string& text) = 0;
    // Send key chords (e.g. ctrl+s) to the focused window.
    virtual bool send_keys(const std::vector<KeyChord>& chords) = 0;

    // Screenshot. target == nullptr => main frame. Captured from the on-screen
    // composited framebuffer, so it includes the GL viewport and ImGui overlays.
    virtual PngImage screenshot_window(const UiNode* target) = 0;

    // Load one or more files (absolute paths) into the running instance on the GUI
    // thread. Returns the number of objects added to the scene (load_files(...).size()).
    // Throws AutomationError(kErrLoadFailed) when nothing loads. Header stays wx-free:
    // the concrete LoadStrategy is chosen inside WxUiBackend, not exposed here.
    virtual int open_files(const std::vector<std::string>& paths) = 0;
};

}}} // namespace Slic3r::GUI::Automation
