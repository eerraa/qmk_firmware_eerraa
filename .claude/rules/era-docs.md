---
paths:
  - "keyboards/era/common/docs/**"
  - "AGENTS.md"
  - "CLAUDE.md"
  - ".claude/rules/**"
  - "keyboards/era/common/tools/era_doc_refs.py"
  - "hooks/era_commit_check.py"
  - "hooks/test_era_commit_check.py"
---

Route through `era_active_index.md` row **documentation reorganization** and
`AGENTS.md` **문서와 도구 계층**. Run
`python keyboards/era/common/tools/era_doc_refs.py`; when document lines are
deleted, also run it with `--homeless`. The pre-commit hook remains the commit
boundary check.
