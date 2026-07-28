# Phase 2 검증

## Fresh verification

- **문서 기준 커밋:** [`e573b78`](https://github.com/whiskend/pintos_302_G1/commit/e573b782a590a9e6180458060e2c392e708de161)
- **현재 상태:** 이번 문서 정리에서는 VM full suite를 재실행하지 않았습니다.
- **재현 명령:** `make -C vm check`

따라서 이 문서는 현재 `main`의 전체 테스트 통과를 주장하지 않습니다.

## Historical evidence

- [PR #96](https://github.com/whiskend/pintos_302_G1/pull/96)에는 `make -C pintos/vm/build tests/vm/swap-anon.result` 통과가 기록되어 있습니다.
- 같은 시점의 `swap-file`, `swap-iter`는 `do_mmap`/`do_munmap` 미구현, `swap-fork`는 `supplemental_page_table_copy` 의존성이 남아 있었습니다.
- 이후 mmap/munmap [PR #98](https://github.com/whiskend/pintos_302_G1/pull/98), [#100](https://github.com/whiskend/pintos_302_G1/pull/100), SPT copy [#99](https://github.com/whiskend/pintos_302_G1/pull/99), cleanup [#102](https://github.com/whiskend/pintos_302_G1/pull/102)가 `main`에 병합됐습니다.

후속 PR의 병합은 구현 보완 근거이며, fresh full-suite 통과와 동일한 뜻은 아닙니다.

## Limitations

- 현재 커밋의 VM full suite는 이 문서 갱신 시점에 미실행입니다.
- PR 본문의 과거 테스트 결과와 현재 코드의 상태를 구분합니다.
- 전체 통과 수치나 badge는 fresh 실행 결과가 확보되기 전까지 추가하지 않습니다.
