# Security Policy

GolfForge is desktop software plus a local data pipeline, maintained by a small team. We take
security seriously within that scope.

## Reporting a vulnerability

**Please do not report security issues in public GitHub issues.**

Instead, email **security@golfforge.org** with:

- a description of the issue and its impact,
- steps to reproduce (a proof of concept if you have one),
- the affected version / commit.

We'll acknowledge your report as soon as we reasonably can (this is a small project, so please
allow some time), keep you posted on the fix, and credit you once it's resolved if you'd like.
Once GitHub private security advisories are enabled on the repo, you may use those instead.

## Scope

Most relevant areas:

- the launch-monitor driver socket connection (e.g. OpenFlight over a local WebSocket);
- any networked / multiplayer code as it lands;
- the Python pipeline's handling of downloaded data (OpenStreetMap / elevation).

Third-party components (the Unreal Engine, bundled assets) are governed by their own vendors'
security processes.

## Supported versions

GolfForge is pre-1.0 and moves fast. Security fixes land on the `main` branch; there are no
long-term-support branches yet.
