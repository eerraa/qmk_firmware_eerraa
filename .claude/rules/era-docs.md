---
paths:
  - "keyboards/era/common/docs/**"
  - "AGENTS.md"
  - ".claude/rules/**"
  - "keyboards/era/common/tools/era_doc_refs.py"
  - "hooks/era_commit_check.py"
  - "hooks/test_era_commit_check.py"
---

Route through `era_active_index.md` row **documentation reorganization** and
`AGENTS.md` **문서와 도구 계층**. Run
`python keyboards/era/common/tools/era_doc_refs.py`; `--homeless` is an
advisory deletion review when identifiers disappear, not a requirement to
preserve source-owned or retired names. The pre-commit hook validates document
references against the staged index at the commit boundary.
