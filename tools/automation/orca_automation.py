"""Reference client for the OrcaSlicer UI automation JSON-RPC server.

Usage:
    from orca_automation import OrcaClient
    orca = OrcaClient(port=13619)
    print(orca.version())
    orca.click({"id": "btn_slice"})
    orca.wait_for({"id": "btn_export"}, state="enabled", timeout_ms=120000)
    png = orca.screenshot()
    open("window.png", "wb").write(png)
"""
from __future__ import annotations
import base64
import json
import urllib.request
from typing import Any, Optional


class OrcaError(RuntimeError):
    def __init__(self, code: int, message: str):
        super().__init__(f"[{code}] {message}")
        self.code = code
        self.message = message


class OrcaClient:
    def __init__(self, host: str = "127.0.0.1", port: int = 13619, timeout: float = 30.0):
        self._url = f"http://{host}:{port}/jsonrpc"
        self._timeout = timeout
        self._id = 0

    def _call(self, method: str, params: Optional[dict] = None) -> Any:
        self._id += 1
        payload = {"jsonrpc": "2.0", "id": self._id, "method": method}
        if params is not None:
            payload["params"] = params
        data = json.dumps(payload).encode("utf-8")
        req = urllib.request.Request(
            self._url, data=data, headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=self._timeout) as resp:
            body = json.loads(resp.read().decode("utf-8"))
        if "error" in body:
            err = body["error"]
            raise OrcaError(err.get("code", -1), err.get("message", "unknown error"))
        return body.get("result")

    # --- protocol methods ---
    def version(self) -> dict:
        return self._call("automation.version")

    def dump_tree(self, root: Optional[str] = None, max_depth: Optional[int] = None,
                  visible_only: bool = False, include_imgui: bool = True) -> dict:
        params: dict = {"visible_only": visible_only, "include_imgui": include_imgui}
        if root is not None:
            params["root"] = root
        if max_depth is not None:
            params["max_depth"] = max_depth
        return self._call("tree.dump", params)

    def find(self, **predicate) -> list:
        # predicate keys: name, class, label, value, backend
        return self._call("tree.find", predicate)

    def get(self, target: dict) -> dict:
        return self._call("widget.get", {"target": target})

    def click(self, target: dict, button: str = "left",
              double: bool = False, modifiers: Optional[list] = None) -> dict:
        params = {"target": target, "button": button, "double": double}
        if modifiers:
            params["modifiers"] = modifiers
        return self._call("input.click", params)

    def type(self, text: str, target: Optional[dict] = None) -> dict:
        params: dict = {"text": text}
        if target is not None:
            params["target"] = target
        return self._call("input.type", params)

    def key(self, keys) -> dict:
        # keys: "ctrl+s" or ["ctrl", "s"]
        return self._call("input.key", {"keys": keys})

    def open(self, paths) -> dict:
        """Load one or more files into the running instance at runtime.

        `paths` is a single absolute path string or a list of them. Paths are read
        from the host filesystem by the server (localhost-only). Returns
        {"ok": True, "loaded": <count>}. Raises OrcaError 1007 on load failure."""
        if isinstance(paths, str):
            paths = [paths]
        return self._call("file.open", {"paths": list(paths)})

    def select_view(self, view) -> dict:
        """Switch the main window to a top-level view/tab by name.

        `view` is one of: "home", "prepare", "preview", "device",
        "multi_device", "project", "calibration". Returns
        {"ok": True, "view": <name>, "index": <int>}. Raises OrcaError 1001 if the
        view name is unknown or not available in the current layout."""
        return self._call("view.select", {"view": view})

    def wait_for(self, target: dict, state: str = "visible",
                 value: Optional[str] = None, timeout_ms: int = 5000,
                 poll_ms: int = 100) -> dict:
        params = {"target": target, "state": state,
                  "timeout_ms": timeout_ms, "poll_ms": poll_ms}
        if value is not None:
            params["value"] = value
        return self._call("sync.wait_for", params)

    def app_state(self) -> dict:
        return self._call("app.state")

    def screenshot(self, target: Optional[dict] = None) -> bytes:
        """Capture a window as a PNG, exactly as composited on screen (includes the
        GL 3D viewport and ImGui overlays). Defaults to the main frame."""
        params = {"target": target} if target is not None else None
        result = self._call("screenshot.window", params)
        return base64.b64decode(result["png_base64"])
