"""Tests for pipeline/build_heightmap.py.

Scope today is narrow: freeze the OpenTopography API-key resolution contract
(GOL-50). The DEM-fetching + raster-conversion code paths hit the network and
big rasterio dependencies and are intentionally left for end-to-end testing.

Run from `pipeline/` with:  python3 -m pytest tests/
"""

import sys
from pathlib import Path

import pytest


# Allow `import build_heightmap` when pytest is invoked from the pipeline dir.
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from build_heightmap import (  # noqa: E402
    OPENTOPO_ENV_VAR,
    resolve_opentopo_key,
)


class TestResolveOpentopoKey:
    """Priority: CLI flag > env var > friendly SystemExit."""

    def test_cli_flag_wins(self):
        # CLI value takes precedence even when the env var is set.
        env = {OPENTOPO_ENV_VAR: "from-env"}
        assert resolve_opentopo_key("from-cli", env=env) == "from-cli"

    def test_env_var_used_when_no_cli(self):
        env = {OPENTOPO_ENV_VAR: "from-env"}
        assert resolve_opentopo_key(None, env=env) == "from-env"

    def test_empty_cli_falls_through_to_env(self):
        # argparse may give us "" rather than None if a user passes --opentopo-key ""
        env = {OPENTOPO_ENV_VAR: "from-env"}
        assert resolve_opentopo_key("", env=env) == "from-env"

    def test_whitespace_env_treated_as_missing(self):
        env = {OPENTOPO_ENV_VAR: "   "}
        with pytest.raises(SystemExit) as exc:
            resolve_opentopo_key(None, env=env)
        assert exc.value.code == 2

    def test_missing_everywhere_exits_with_help(self, capsys):
        with pytest.raises(SystemExit) as exc:
            resolve_opentopo_key(None, env={})
        assert exc.value.code == 2
        # Help text on stderr must point contributors at the signup URL + env var.
        err = capsys.readouterr().err
        assert "portal.opentopography.org" in err
        assert OPENTOPO_ENV_VAR in err
        assert "--opentopo-key" in err

    def test_no_demo_key_leakage(self):
        """Regression: the old hardcoded demo key 'demoapikeyot2022' must not
        sneak back in as a fallback. GOL-50 removed it explicitly so contributors
        don't share the demo's rate limit."""
        with pytest.raises(SystemExit):
            resolve_opentopo_key(None, env={})
        # Walk the module's globals for the old constant name.
        import build_heightmap

        assert not hasattr(build_heightmap, "OPENTOPO_DEMO_KEY")
        # And no module-level string literal that happens to match.
        for name, value in vars(build_heightmap).items():
            if isinstance(value, str):
                assert "demoapikeyot2022" not in value, (
                    f"Demo key leaked back into module attribute {name!r}"
                )
