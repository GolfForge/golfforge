"""Golfsim MCP toolset for Unreal Engine 5.8's official Model Context Protocol.

Authored as a Python `ToolsetDefinition` (GOL-215) instead of porting our custom
UnrealClaudeMCP C++ plugin: Epic owns the MCP server/transport, the Toolset Registry
auto-discovers this file (it lives in a *plugin's* Content/Python -- the registry does
NOT scan the project's Content/Python), and Python tool bodies hot-reload via
`ModelContextProtocol.RefreshTools`. Type hints + Google-style docstrings drive the
JSON schema.

`execute_python` is the workhorse -- our engine-side automation is already stdlib-only
Python run in the embedded interpreter, so this single tool restores ~all of it.
"""

import contextlib
import io
import traceback

import unreal

try:
    import toolset_registry
except ImportError:
    toolset_registry = None


if toolset_registry is not None:

    @unreal.uclass()
    class GolfsimTools(unreal.ToolsetDefinition):
        """Golfsim project automation: run Python in the editor + inspect the level."""

        @toolset_registry.tool_call
        @staticmethod
        def execute_python(code: str) -> str:
            """Execute Python in the Unreal editor interpreter and return its stdout.

            The `unreal` module is available; use print() for anything you want back.
            Each call runs in a fresh namespace (no state persists between calls).

            Args:
                code: Python source to exec() in the editor.

            Returns:
                Captured stdout, or stdout + a traceback string if the code raised.
            """
            buf = io.StringIO()
            try:
                with contextlib.redirect_stdout(buf):
                    exec(code, {"unreal": unreal})
            except Exception:
                return buf.getvalue() + "\n--- TRACEBACK ---\n" + traceback.format_exc()
            return buf.getvalue()

        @toolset_registry.tool_call
        @staticmethod
        def list_actors(class_filter: str = "") -> list[str]:
            """List actor labels in the current editor level, optionally filtered by class.

            Args:
                class_filter: Case-insensitive substring matched against each actor's
                    class name (e.g. 'GolfPinActor'). Empty returns all actors.

            Returns:
                A list of "<label> (<ClassName>)" strings.
            """
            eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
            actors = eas.get_all_level_actors() if eas else []
            out = []
            needle = class_filter.lower()
            for a in actors:
                if not a:
                    continue
                cls = a.get_class().get_name()
                if needle and needle not in cls.lower():
                    continue
                out.append("{0} ({1})".format(a.get_actor_label(), cls))
            return out

    # Subclassing ToolsetDefinition does NOT auto-register with the registry in this
    # 5.8 build -- it must be registered explicitly (the shipped toolsets do the same
    # via toolset_registry.registration). Keep a module-level ref so it isn't GC'd and
    # so the framework's reload_module() can unregister/re-register on hot-reload.
    from toolset_registry.registration import Registration as _Registration
    _GOLFSIM_REGISTRATION = _Registration([GolfsimTools])
    _GOLFSIM_REGISTRATION.register()
