# Contributing to GolfForge

Thanks for your interest in GolfForge. Here's how to help right now.

## Current status: issues yes, pull requests not yet

GolfForge is in early, fast-moving development. To keep things coherent while the foundations
settle, **we are not accepting external pull requests yet.** PRs opened against this repo will be
politely closed for now.

**What you _can_ do today:**

- **Report bugs** and **request features** via [Issues](../../issues) — these are open and very welcome.
- Try the sim, build a course with the pipeline, and tell us what breaks or what's missing.
- Discuss ideas in an issue before they become code.

## Why PRs are closed for now

1. **Stability.** The architecture is still moving fast; merging external code now would create more churn than value.
2. **Licensing.** GolfForge is dual-licensed — AGPL-3.0 plus a paid commercial license (see [`COMMERCIAL.md`](COMMERCIAL.md)). To keep offering both, we need a **Contributor License Agreement (CLA)** in place before we can accept contributed code. That's coming; until then, code contributions can't be merged.

When PRs open, contributions will be under the project's AGPL-3.0 license and the CLA. We'll update this file when that happens.

## Reporting a good bug

Use the bug-report issue template and include:

- what you did, what you expected, and what actually happened;
- your OS + GPU, and whether you're running the editor (PIE) or a packaged build;
- for course/pipeline issues: the course id / bbox and any console output.

## Project orientation

- [`README.md`](README.md) — what GolfForge is and how to run it.
- [`docs/windows-setup.md`](docs/windows-setup.md) — Windows / UE5 setup.
- [`pipeline/README.md`](pipeline/README.md) — the course-building pipeline.
- [`docs/`](docs/) — architecture, event protocol, data contract.

## Code of conduct

This project follows the [Contributor Covenant](CODE_OF_CONDUCT.md). By participating, you agree to uphold it.

## Security

Please don't file security issues publicly — see [`SECURITY.md`](SECURITY.md).
