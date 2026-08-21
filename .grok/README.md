# Grok Build adapter

This directory is mechanism only. `hooks/era-pretooluse.json` is Grok's native
project adapter and calls the tool-neutral gate at `hooks/era_pretooluse.py`.
The policy it enforces is canonical in `AGENTS.md`.

Do not use Grok's Claude-hook compatibility scan as ERA wiring. On each
development machine, keep this user-level setting in `~/.grok/config.toml`:

```toml
[compat.claude]
hooks = false
```

This disables only the import of Claude hooks into Grok. It does not disable
Grok's global hooks or this repository's `.grok/hooks/` adapter. After cloning,
trust the project with `/hooks-trust` (or launch with `--trust`) and use
`/hooks` or `grok inspect --json` to verify that the `.grok/hooks` source is
enabled and the `.claude` hook source is disabled.
