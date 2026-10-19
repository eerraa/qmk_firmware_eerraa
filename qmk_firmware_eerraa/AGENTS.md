# ERA 에이전트 진입

ERA 전용 QMK fork에서 모든 에이전트가 따르는 로컬 작업 규칙과 문서 라우팅
진입점이다. 구현 요약, 세션 기록, 과거 조사 결과는 두지 않는다.

## 시작과 작업 라우팅

- 공통 Git·승인·로컬 폴더 정책은 루트 AGENTS.md를 따른다. 이 문서의 경로와
  빌드·검사 명령은 현재 펌웨어 컴포넌트 기준이다.
- ERA 작업은
  `keyboards/era/common/docs/era_active_index.md`에서 해당 작업 행을 고른다.
  편집 전에는 **Change** 열만 읽고, **Locate**는 소유 위치를 찾을 때,
  **Verify**는 실제 검증·판정을 할 때만 읽는다.
- `contracts/era_overview.md`와 `contracts/era_invariants.md`는 상시 선독이
  아니다. 색인의 해당 작업 행이 요구할 때만 읽는다.
- 일반 개발에는 중앙 `eerraa-agent-docs` 접근이 필요하지 않다. 중앙 규약은
  문서체계 변경·채택 검토 때만 조회한다.
- 현재 소스·설정·스키마가 구현 사실을 소유하고, 제품 요구·안전·호환 경계와
  미구현 목표는 해당 계약이 소유한다. 둘이 어긋나면 테스트를 통과시키려고 계약을
  완화하지 말고, 구현 결함·의도 변경·낡은 문서 중 무엇인지 판단한다. 이 세션의
  권한 밖이면 불일치를 보고한다.

## 로컬 작업 규칙

- 공유 스플릿 동작은 `keyboards/era/common/split/`을 우선한다.
- 스캔 바운드 핫 패스의 결정은 실용적인 한 O(1)로 둔다. 활성 계약이 비용을
  명시적으로 수락하지 않는 한 할당, 넓은 소스 스캔, CRC 구성,
  EEPROM/RGB/INPUT 신선도 계산, 진단 스냅샷 구성을 그 경로에 넣지 않는다.
- 진단 전용 상태는 릴리스 핫 패스 밖에 둔다.
- 실제 기기 증거와 소스 추론이 충돌하면 관찰된 증거를 우선한다. 실기기 미측정,
  skip, unknown을 PASS로 바꾸지 않는다.
- 생 기기 로그와 생 EEPROM 바이트를 활성 문서나 커밋 메시지에 복사하지 않는다.
  증거 형식과 최소 기록은 해당 매뉴얼이 소유한다.
- `docs/`는 upstream QMK 문서다. `keyboards/era/**/readme.md`와
  `keyboards/era/common/docs/user/`도 독자가 다르므로 해당 작업이 직접
  요구할 때만 수정한다.

## 문서와 도구 계층

- 도구 중립 문서 진입은 이 파일과 `era_active_index.md`다. 활성 ERA 에이전트
  문서는 `keyboards/era/common/docs/contracts/`, `maps/`, `manuals/`에
  두며, 실제 열린 상태가 필요할 때만 state 역할을 둔다.
- 같은 구현 사실을 문서에 다시 설명하지 않는다. 단순 검색으로 복원되는 값은
  첫 소유 경로·심볼을 가리킨다. wire/storage/safety 수치, 호환 의무, 미구현
  요구처럼 문서가 요구 자체를 소유하는 내용은 해당 계약에 보존한다.
- 완료 이력, 측정 일지, 세션 인계, 닫힌 계획, 평행 archive를 활성 문서로
  누적하지 않는다.
- 빌드는 `keyboards/era/common/tools/era_build.py`를 현재 편집 worktree에서
  실행한다. 저장소 밖 machine profile과 검증된 Linux backend가 환경을
  소유한다. 설정·동기화·반환 절차는 `manuals/era_build_and_flash.md`를 따른다.
- 커밋 시점 검사는 `hooks/pre-commit` → `hooks/era_commit_check.py`다.
  문서 포인터 검사는
  `keyboards/era/common/tools/era_doc_refs.py`가 소유한다.
- 문서 구조나 라우팅을 바꾸면 이 파일, `era_active_index.md`와
  검사 포인터를 같은 관심사 변경에서 정렬한다. 문서 줄 삭제 시
  `python keyboards/era/common/tools/era_doc_refs.py --homeless`로 사라진 식별자를
  검토할 수 있다. 출력 자체는 보존 의무가 아니며, 고유 제품 요구·안전·호환
  제약만 실제 소유 위치에 남기고 소스 소유 식별자나 폐기명은 새 집을 만들지 않는다.

이 집합은 `eerraa-agent-docs` v2의 commit
`4bd84f45dd3e970bf25505059431058efbe7680a` 고정본을 따른다. 문서 규약은
[AGENT_DOCS_CONVENTION.md](https://github.com/eerraa/eerraa-agent-docs/blob/4bd84f45dd3e970bf25505059431058efbe7680a/AGENT_DOCS_CONVENTION.md),
채택 판단 기준은 같은 고정본의
[ADOPTION.md](https://github.com/eerraa/eerraa-agent-docs/blob/4bd84f45dd3e970bf25505059431058efbe7680a/ADOPTION.md)다.
장르별 디렉터리, 별도 진입 라우터(`era_active_index.md`), Genre 헤더와
진입 한국어/본문 영어는 로컬 표현 선택이며 중앙 규약의 의미상 예외가 아니다.

## 검증과 종료

- 문서만 바꾼 작업은 관련 문서 검사, 필요한 삭제 안전망, `git diff --check`와
  최종 diff 검토가 기본이다. 제품 전체 빌드나 HIL을 요구하지 않는다.
- 문서 검사기나 관련 검사를 바꿨다면 정상 입력과 의도적으로 깨진 음성 fixture를
  함께 실행한다.
- 제품 소스를 바꾼 작업의 검증 범위는
  `keyboards/era/common/docs/manuals/era_performance_gates.md`가 소유한다.
- 문서 삭제 커밋은 제거한 것이 중복·이력인지, 고유 제약이 어디에 남는지를 설명한다.
- 최종 보고에는 바뀐 범위, 실행한 검증, 미검증·기존 실패, commit을 구분해
  적는다.
