---
paths:
  - "keyboards/era/common/docs/**"
  - "AGENTS.md"
---

Agent-document territory. Canonical rules: AGENTS.md Document Management
Rules, its Evidence And Retirement section, and the
`documentation reorganization` Task Read Matrix row. Before committing doc
deletions, run the safety net:
`python keyboards/era/common/tools/era_doc_refs.py --homeless` — a homeless
token is a promotion candidate, not a deletion candidate. The pre-commit hook
(`hooks/pre-commit`) runs the same script with no argument automatically,
which is the locatability check. Because that checker derives names and targets
from the whole non-ignored working tree, an armed document check refuses any
unstaged or untracked non-ignored path rather than validating content different
from the staged commit.
