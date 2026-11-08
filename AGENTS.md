# Bearamin VCV collection

- This is the independent source for the Bearamin Rack plugin. Root Makefile and plugin.json must build with the Rack SDK without sibling repositories or KiCad.
- Preserve plugin slug Bearamin, visible module slugs SlowLFO/Virtual, the hidden BearaminVirtual compatibility model, output order, parameters, saved-state keys, Virtual protocol v1 and immediate fault/stale zeroing.
- Run ./test.sh, then the Rack-SDK test mode and make dist with the configured SDK. Keep generated binaries and build products ignored.
- README and other maintained publication documents must remain consistent with the implementation and preserve upstream and agent-assisted provenance.
- Do not install into a running Rack session or touch devices during source checks. Library submissions, comments and emails need explicit authorization.

## Agent-assisted collaboration

These instructions make AI-assisted development and review visible. Preserve accurate human, agent and upstream attribution; do not imply sole authorship or manufacture contributor identities. Keep secrets, personal details, device identities and private operational records out of the published source.
