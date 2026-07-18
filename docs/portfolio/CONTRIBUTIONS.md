# Contributions — Phase 2

이 문서는 커밋 수를 기여량으로 환산하지 않고 `개인 주도`, `페어프로그래밍`, `팀 통합`을 구분합니다.

## 페어프로그래밍

`NearthYou`와 [`cad8798-cmd`](https://github.com/cad8798-cmd)는 다음 범위를 함께 설계·구현·디버깅했습니다.

- supplemental page table의 초기화·조회·삽입 흐름
- frame table 생성과 page/frame 연결
- uninit page와 lazy segment loading

저장소에 남은 대표 근거는 NearthYou의 [`817cf35`](https://github.com/whiskend/pintos_302_G1/commit/817cf35), [`2071e5a`](https://github.com/whiskend/pintos_302_G1/commit/2071e5a), [`a9047e1`](https://github.com/whiskend/pintos_302_G1/commit/a9047e1), [`70672d8`](https://github.com/whiskend/pintos_302_G1/commit/70672d8)과 cad8798-cmd의 [PR #85](https://github.com/whiskend/pintos_302_G1/pull/85), [PR #88](https://github.com/whiskend/pintos_302_G1/pull/88), [`45dd856`](https://github.com/whiskend/pintos_302_G1/commit/45dd856), [`a8ec32f`](https://github.com/whiskend/pintos_302_G1/commit/a8ec32f), [`915c6ef`](https://github.com/whiskend/pintos_302_G1/commit/915c6ef)입니다.

## NearthYou 주도 PR

| 범위 | PR | 대표 커밋 | 표기 |
| --- | --- | --- | --- |
| userprog regression과 lazy-load cleanup | [#90](https://github.com/whiskend/pintos_302_G1/pull/90) | `a9047e1`~`70672d8` | 페어 작업 위 후속 수정 |
| swap table과 anonymous swap | [#92](https://github.com/whiskend/pintos_302_G1/pull/92) | [`200d0e2`](https://github.com/whiskend/pintos_302_G1/commit/200d0e2), [`e282a00`](https://github.com/whiskend/pintos_302_G1/commit/e282a00) | PR 주도·팀 리뷰 |
| eviction과 owner page-table 처리 | [#96](https://github.com/whiskend/pintos_302_G1/pull/96) | [`10bafc4`](https://github.com/whiskend/pintos_302_G1/commit/10bafc4), [`9370518`](https://github.com/whiskend/pintos_302_G1/commit/9370518) | PR 주도·팀 리뷰 |
| aux/resource 수명과 swapped-page copy 보완 | [#102](https://github.com/whiskend/pintos_302_G1/pull/102) | [`0d7f8a3`](https://github.com/whiskend/pintos_302_G1/commit/0d7f8a3), [`e79f379`](https://github.com/whiskend/pintos_302_G1/commit/e79f379) | 팀 구현 후속 통합 |

## 팀 통합

- `whiskend`: page claim/fault rollback [#84](https://github.com/whiskend/pintos_302_G1/pull/84), [#87](https://github.com/whiskend/pintos_302_G1/pull/87), cleanup [#94](https://github.com/whiskend/pintos_302_G1/pull/94), mmap/munmap [#98](https://github.com/whiskend/pintos_302_G1/pull/98), [#100](https://github.com/whiskend/pintos_302_G1/pull/100)
- `SaeByeok22`: syscall user buffer를 포함한 stack growth [#95](https://github.com/whiskend/pintos_302_G1/pull/95)
- `cad8798-cmd`: supplemental page table copy [#99](https://github.com/whiskend/pintos_302_G1/pull/99)

SPT copy와 resource cleanup은 PR #99의 구현과 PR #102의 swapped-page/aux 후속 수정이 이어진 공동 통합 작업입니다. VM 전체는 특정 한 사람의 단독 구현이 아닙니다.

## 표기 원칙

- Git에 남은 PR·커밋은 근거 링크로 사용하되 커밋 개수로 기여량을 계산하지 않습니다.
- 페어프로그래밍 범위는 당사자가 확인한 SPT·frame·lazy loading으로 제한합니다.
- PR 작성자를 전체 기능의 단독 소유자로 확대하지 않습니다.
