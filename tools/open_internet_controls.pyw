"""Open controls for an already-running, password-protected Internet host."""
import json
import os
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from cofcoop import core, network
from cofcoop.ui import Launcher


def main():
    output = ROOT / "state" / "internet-control-status.json"
    mapping = None
    app = None
    ready = False
    try:
        settings = core.SettingsStore().load()
        endpoint = network.Endpoint("127.0.0.1", int(settings.get("port", 27015)))
        info = network.query_server(endpoint)
        if settings.get("lan", True) or not info.get("password"):
            raise RuntimeError("Configure the active host for Internet play with a server password first.")
        app = Launcher()
        app.withdraw()
        mapping = network.upnp_map(endpoint.port, timeout=8)
        remote = network._read_mapping(mapping, endpoint.port, time.monotonic() + 4)
        if remote is None or not network._same_mapping(remote, mapping):
            raise RuntimeError("The router did not confirm the requested UDP mapping.")
        app._attached(endpoint, info, core.DEFAULT_GAME_DIR)
        app.active_lan = False
        app.mapping = mapping
        app._public(mapping["external_ip"], mapping["port"])
        app.log("Internet test ready. Network & invite can remove this session's temporary UDP mapping.")
        output.write_text(json.dumps({"ready": True, "pid": os.getpid(), "mapping": mapping,
                                      "router_verified": True, "remote_join_verified": False}, indent=2))
        ready = True
        app.deiconify()
        app.mainloop()
    except Exception as exc:
        if mapping is not None and not ready:
            try:
                network.upnp_unmap(mapping, timeout=4)
            except Exception:
                pass
        output.write_text(json.dumps({"ready": False, "error": str(exc)}, indent=2))
    finally:
        if app is not None:
            try:
                app.destroy()
            except Exception:
                pass


if __name__ == "__main__":
    main()
