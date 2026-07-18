# Pintos Phase 2 — Virtual Memory

> A C-based virtual-memory implementation lab covering supplemental page tables, lazy loading, frame allocation, swap, eviction, stack growth, and memory-mapped files.

KAIST Pintos에서 virtual address부터 physical frame과 swap disk까지 이어지는 page lifecycle을 직접 구현하고 디버깅한 운영체제 팀 프로젝트입니다.

## Project continuity

팀 변경과 함께 이 저장소는 별도로 초기화되었습니다. 첫 저장소와 Git ancestry를 공유하지는 않지만, 같은 KAIST Pintos 과정에서 Threads와 User Programs 다음 단계인 Virtual Memory 구현을 이어갔습니다.

- 이전 단계: [`NearthYou/pintos_lab`](https://github.com/NearthYou/pintos_lab)
- 전체 프로젝트 인덱스: [`NearthYou/pintos-os-lab`](https://github.com/NearthYou/pintos-os-lab)

## What we implemented

| 영역 | 구현 범위 | 대표 근거 |
| --- | --- | --- |
| SPT·frame·lazy loading | page 메타데이터, frame table, lazy segment 초기화와 claim 경로 | [PR #85](https://github.com/whiskend/pintos_302_G1/pull/85), [#88](https://github.com/whiskend/pintos_302_G1/pull/88), [#90](https://github.com/whiskend/pintos_302_G1/pull/90) |
| Page fault와 rollback | fault 처리, claim 실패 시 frame/page 관계 복구 | [PR #84](https://github.com/whiskend/pintos_302_G1/pull/84), [#87](https://github.com/whiskend/pintos_302_G1/pull/87) |
| Anonymous swap·eviction | bitmap 기반 swap slot, disk I/O, victim frame 교체 | [PR #92](https://github.com/whiskend/pintos_302_G1/pull/92), [#96](https://github.com/whiskend/pintos_302_G1/pull/96) |
| Stack growth | syscall user buffer를 포함한 stack 확장 | [PR #95](https://github.com/whiskend/pintos_302_G1/pull/95) |
| mmap·munmap | file-backed page의 lazy mapping과 해제 | [PR #98](https://github.com/whiskend/pintos_302_G1/pull/98), [#100](https://github.com/whiskend/pintos_302_G1/pull/100) |
| SPT copy·resource lifetime | fork 시 page 복사, swapped page와 aux 정리 | [PR #99](https://github.com/whiskend/pintos_302_G1/pull/99), [#102](https://github.com/whiskend/pintos_302_G1/pull/102) |

## Page lifecycle architecture

```mermaid
flowchart LR
    VA["virtual address"] --> SPT["supplemental page table"]
    SPT --> U["uninit / lazy page"]
    U --> C["claim page"]
    C --> F["frame table"]
    F --> E["eviction policy"]
    E --> A["anonymous swap disk"]
    E --> M["file-backed mmap"]
    A --> C
    M --> C
```

- SPT는 page-aligned virtual address를 기준으로 각 프로세스의 page 상태를 찾습니다.
- claim은 frame 확보, page/frame 연결, page table mapping, `swap_in`을 하나의 복구 가능한 흐름으로 묶습니다.
- 물리 frame이 부족하면 victim을 고르고 anonymous page는 swap disk로, file-backed page는 파일 정책에 따라 내보냅니다.
- 프로세스 종료와 `munmap`에서는 aux, file, swap slot, frame 관계를 중복 해제하지 않도록 수명을 정리합니다.

## Collaboration and contributions

이 프로젝트는 팀 구현입니다. 특히 **supplemental page table, frame table, lazy loading 기초와 디버깅은 `NearthYou`와 `cad8798-cmd`가 페어프로그래밍으로 진행**했습니다.

`NearthYou`는 anonymous swap과 eviction PR을 주도했지만, 리뷰와 다른 팀원의 page-fault·stack·mmap·SPT-copy 구현을 거쳐 전체 VM으로 통합됐습니다. VM 전체를 개인 단독 구현으로 표현하지 않습니다.

기능별 개인·페어·팀 경계와 커밋은 [기여 문서](docs/portfolio/CONTRIBUTIONS.md)에서 확인할 수 있습니다.

## Fresh verification

이번 문서 정리에서는 시간이 오래 걸리는 VM full suite를 다시 실행하지 않았습니다. 따라서 현재 `main`의 전체 통과를 주장하지 않습니다. 당시 PR에 기록된 테스트와 현재 재현 명령은 [검증 문서](docs/portfolio/VERIFICATION.md)에 분리해 두었습니다.

## Run locally

Docker/Dev Container 환경에서 다음 명령으로 재현할 수 있습니다.

```bash
cd pintos
source ./activate
make -C vm check
```

특정 anonymous swap 사례만 실행하려면 다음 명령을 사용합니다.

```bash
make -C vm tests/vm/swap-anon.result
```

## Known limitations

- 저장소는 Phase 1과 별도의 Git 이력으로 초기화되어 두 저장소 사이의 소스 계보를 증명하지 않습니다.
- PR #96 시점에는 `swap-anon`이 통과했지만 mmap과 SPT copy 의존 테스트는 후속 구현이 필요했습니다.
- 이후 PR #98~#102가 해당 경로를 보완했지만, 이번 문서 정리에서 VM full suite를 재실행하지 않았으므로 전체 통과로 확장하지 않습니다.
- KAIST Pintos 소스의 공개·재배포 조건은 반드시 원본 라이선스를 확인해야 합니다.

## Previous phase

Threads와 User Programs 단계는 [`NearthYou/pintos_lab`](https://github.com/NearthYou/pintos_lab)에서 확인할 수 있습니다. 두 단계를 한 번에 보는 문서는 [`NearthYou/pintos-os-lab`](https://github.com/NearthYou/pintos-os-lab)에 정리합니다.

## License and attribution

이 저장소는 KAIST Pintos 교육용 코드베이스와 Stanford Pintos를 기반으로 합니다. 자세한 조건은 [`pintos/LICENSE`](pintos/LICENSE)를 확인하세요.
