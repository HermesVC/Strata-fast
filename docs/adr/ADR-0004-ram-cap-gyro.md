# ADR-0004 — Кап RAM (`STRATA_RAM_CAP_GIB`) на гілці `f-143-memlog` у `E:\Strata-gyro`

**Статус:** порт завершений, збірка `dist-cap` і `memlog_test` — ok (2026-10-11); виміряний прогон — очікує. **Гілка:** `f-143-memlog` у `E:\Strata-gyro`, база `9ff02c9` (`rc2`, Strata v0.1.40.3 + Gyro).
**Машина:** RX 7900 XT (gfx1100), 48 GB RAM, Windows, ROCm 10.2.0a20260930, Windows SDK 10.0.26100.
**Конфіг капу:** `strata-gyro-m-cap.json` (стабільний `strata-gyro-m.json`, змінені тільки `exe`, `log`, `lib_dirs`), двіжок `dist-cap\strata-windows-x64-hip\strata.exe` (білд-каталог `build-cap`), стеля `STRATA_RAM_CAP_GIB=24` у лаунчері `run-gyro-m-cap.bat`.
**Стабільний шлях не чіпається:** `rc2` + `run-gyro-m.bat` + `strata-gyro-m.json` + `dist\strata-windows-x64-hip` + `build-hip-win` лишаються як були.
Родина: `E:\Strata-latest\docs\adr\ADR-0003-memlog-reporting.md` (Стадія 7-10) — той самий кап, той самий semantics; цей ADR — порт на іншу лінію коду (`rc2`), зі своїми числами.

## Правило цього ADR

1. Кап = стеля на **цей процес**, а не «machine minus N». `STRATA_MEMORY_LIMIT_MIB` лишається окремим knob'ом («весь machine не більше X для всіх») і не зливається з капом.
2. Reporting (`STRATA_MEMLOG`) і cap (`STRATA_RAM_CAP_GIB`) — два окремі зміни, обидва opt-in; без змінних шлях byte-identical.
3. Commit-стелу кап не заряджує (рішення Стадія 10 в ADR-0003).
4. Кап діє на межі запиту, а не всередині запиту: prefill мапить сторінки, які читає.
5. Тести — чиста арифметика на fixture'ах, без GPU і без абсолютних OS-чисел; очікування — за специфікацією, не скопійовані з реалізації.
6. Hot set (prompt ring) — власна private пам'ять процесу, вже всередині `private_ws`, тому `hot` параметра немає; є тільки `reserve` для резерву викликача.

## Стадія 0 — порт функціоналу (2026-10-11)

Джерело: `f-143-memlog` у `E:\Strata-latest`, комміти `4730217f` (memlog reporting) + `9c40d6bb` (`STRATA_RAM_CAP_GIB`).
Лінії коду різні (`rc2` проти `rc1`-лінії latest), тому `git apply` цілим патчем не пройшов: 4 файли портовані вручну, решта — патчем.

| файл | як портовано | що |
|---|---|---|
| `include/strata/platform/memlog.hpp` | копія 1:1 | `level/on/parse_number/cap_bytes/ram_cap_bytes/probe_cap/cap_room/CeilingInput/ceiling_room/room_for/mark_over/over/add_trimmed/after_trim/Reading/read/phase/GateProbe/gate_probe/gate` |
| `src/platform/memlog.cpp` | копія 1:1 | читання Windows (`PROCESS_MEMORY_COUNTERS_EX2` + fallback) і Linux (`/proc/self/statm`, `/proc/self/stat`, `/proc/meminfo`); рядки фаз, гейтів, стеля-рішень, request-end, trim |
| `tests/platform/memlog_test.cpp` | копія 1:1 | 12 кейсів `gate_probe` + cross-check проти `platform::file_cache_keeps` + 8 кейсів `ceiling_room` + парсинг env + `cap_room` |
| `src/core/pinned.cu` | патчем | `experts_unbuffered`: `avail` через `memlog::room_for`, probe гейта |
| `src/program/generate.cpp` | патчем | `req0` на межі запиту, `phase("prompt")`, `phase("decode")`, request-end: `mark_over()` → `src.trim_mapping()` → `after_trim()` |
| `sycl/src/core/expert_source.cpp`, `sycl/CMakeLists.txt` | патчем | той самий funnel у SYCL-копії `host_available_memory` + `memlog.cpp` у `_core` (не компілюється тут: SYCL-тулчейну немає → не перевірено) |
| `include/strata/core/expert_source.hpp` | вручну | `HostMemory::by_ceiling`, `FileExpertSource::trim_mapping()`, документат до `release()` |
| `src/core/expert_source.cpp` | вручну | `detail::apply_ceiling` на всіх трьох гілках `host_available_memory`; `available_memory_bytes(..., by_ceiling)`; `resident_limit()` (commit / RAM / RAM під стелею); `log_complement_memory(stage, requested, headroom)` + probe; probe `file_cache_gate` (Linux-гілка); `release()` = `enabled \|\| memlog::over()` + `add_trimmed`; `trim_mapping()`; probe `resident_budget`; safety-check іменуючий стелю; три виклики `log_complement_memory(..., headroom_bytes)`; фінальний trim комплементу → `trim_mapping()` |
| `src/core/conversation_memory.cpp` | вручну | `conversation_available_memory()` — Windows і Linux гілки через `memlog::room_for` (у `rc2` цей файл не має `sample_host_memory`, як у latest; роль гейта паркування/restore тут саме в цій функції) |
| `CMakeLists.txt` | вручну | `memlog.cpp` у `strata_core`; `memlog.cpp` у `conversation_memory_test`; таргет `memlog_test` у `if(STRATA_BUILD_TESTS)` |

Адаптації під `rc2`, а не сліпі копії:
- `trim_mapping()` без `!unmapped_` — у `rc2` немає члена `unmapped_`; натомість guard `base_ != nullptr`.
- `file_cache_gate` probe — у Linux-гілці `file_tier_unbuffered` (Windows-гілка делегує в `pinned.cu::experts_unbuffered`, де probe вже є).
- `conversation_memory.cpp` — `room_for` на обох гілках `conversation_available_memory()`; `std::optional` зберігає «unknown ≠ виміряне 0».
- `memory_guard_test` і `host_limits_test` у `rc2` відсутні → відповідних правок CMake немає; `conversation_memory_test` потребує `-DSTRATA_BUILD_CONVERSATION_TESTS=ON` (у `rc2` default OFF).

## Прогрес (трекінг стадій)

| № | крок | статус | доказ |
|---|---|---|---|
| 0 | порт коду (11 файлів) | зроблено | `git diff --stat` = 166 вставок у 8 файлах + 3 нові файли (569 рядків); форма 1:1 з latest |
| 1 | арифметика капу на fixture'ах | зроблено (2026-10-11) | `build-cap\memlog_test.exe`: `memlog_test: ok` (ROCm clang, той самий білд) |
| 2 | чиста компіляція нових TU | зроблено | `memlog.cpp`, `conversation_memory.cpp`, `expert_source.cpp` — лише `getenv`-deprecation warnings, як у решті коду |
| 3 | збірка капу | зроблено | `build-cap.bat` (через `tools\hip\build_windows.bat tests`, gfx1100, `E:\agention-llama.cpp`) → `build-cap\`, `dist-cap\strata-windows-x64-hip\` + zip + `COMMIT.txt`, `EXITCODE=0`; стабільні `build-hip-win`/`dist` не чіпалися |
| 4 | ctest на білді капу | частково | `memlog_test` ok; `conversation_memory_test` не зібраний (`STRATA_BUILD_CONVERSATION_TESTS` у `build_windows.bat` не передається); GPU-тести не ганялися |
| 5 | config + bat | зроблено | `strata-gyro-m-cap.json` (від стабільного відрізняються лише `exe`, `log`, `lib_dirs`), `run-gyro-m-cap.bat` (`STRATA_RAM_CAP_GIB=24`, якщо не задано викликачем); ярлик на робочому столі не створювався; стабільні `run-gyro-m.bat`/`strata-gyro-m.json` unchanged |
| 6 | виміряний прогон стелі | очікує запуску | стабільний движок зупинений (порт 9797 вільний) — вікно є; запуск `run-gyro-m-cap.bat` з `STRATA_MEMLOG=1` |

## Що кап робить і чого не робить (семантика, з ADR-0003 Стадія 9-10)

Робить:
- кожен гейт, що питає «скільки ще можна», отримує `min(читання OS, стеля − private working set процесу)` (`memlog::room_for` / `ceiling_room`);
- на межі запиту, коли весь working set (private + маповані сторінки експертів) перевищує стелю, мапований view віддається назад (`VirtualUnlock` → standby, кешований, рахується available); наступний дотик = soft fault;
- `release()` під `over()` підстригує сторінки експертів, що вже потрапили у VRAM; лічильник `add_trimmed` — atomic і скидається на кожному кінці запиту.

Не робить:
- не відбирає resident комплемент, який вже в RAM (для того `STRATA_FILE_RELEASE=1` і план tier на старті);
- не змушує OS скидати сторінки, які цей процес не володіє;
- не заряджує commit проти стелі;
- Linux: request-end trim не існує (один рядок у stderr, що мапований view тут не віддається).

Якщо стеля нижча за resident комплемент — tier падає на file-read шлях, і це perf-режим, який trzeba сказати прямо, а не ховати за числом.

## Наступні кроки

1. Збірка капу чисто + ctest (`memlog_test`, `conversation_memory_test` з `STRATA_BUILD_CONVERSATION_TESTS=ON`).
2. Вікно вимірювання (після дозволу Кирилла): `run-gyro-m-cap.bat` з `STRATA_RAM_CAP_GIB=24` і `STRATA_MEMLOG=1`, 6-10 довгих запитів; порівняння з стабільним рухом: tier, ws peak/end, shared peak/end, avail min/end, trims, pp/tg.
3. Комміти: дві окремі зміни (reporting, потім cap) — так само як у latest.
