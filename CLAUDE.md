@AGENTS.md

## Claude Code 어댑터

제품 규칙과 읽기 정책의 정본은 `AGENTS.md`와
`keyboards/era/common/docs/era_active_index.md`다. 이 파일은 Claude의
환경·배선만 소유한다.

- `.claude/rules/*.md`는 경로가 맞을 때 `era_active_index.md`의 해당 작업
  행으로 보낸다. 계약 목록을 adapter에 다시 복사하거나 전체 색인을 상시
  import하지 않는다.
- 커밋 시점 검사는 `hooks/pre-commit` → `hooks/era_commit_check.py`다.
  편집 클론은 한 번 `git config core.hooksPath hooks`로 무장한다.
  `.claude/settings.json`은 프로젝트 훅이 아니라 권한 deny만 소유한다.
- 파괴적 복원 deny는 유지한다. push 권한은 adapter가 부여하지 않으며
  `AGENTS.md`의 원격 반영 규칙을 따른다.

## 빌드 환경

- 지원되는 증거 빌드는 Windows 편집 트리에서 WSL2 Ubuntu 빌드 트리로
  동기화한 뒤 `era-build keyboard:keymap`으로 실행한다.
- 동기화·빌드 메커니즘은 `.claude/tools/era-sync.sh`와
  `.claude/tools/era-build.sh`가 소유한다. 실제 빌드/플래시 절차는
  `keyboards/era/common/docs/manuals/era_build_and_flash.md`, 증거 판정은
  `keyboards/era/common/docs/manuals/era_performance_gates.md`를 따른다.
- WSL, wrapper, toolchain 또는 경로가 준비되지 않았으면 실패를 보고하고
  중단한다. 즉흥 설치, Windows 직접 빌드, 내부 launcher 직접 호출로 우회하지
  않는다.
- 플래시는 Windows 쪽 RPI-RP2 파일 복사 경로이며 자동 실행하지 않는다.
