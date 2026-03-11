# Market Data Engine

C++17로 구현한 저지연 시장 데이터 처리 파이프라인.

바이낸스 WebSocket에서 실시간 오더북 데이터를 수신하고, simdjson으로 파싱하여 lock-free SPSC 큐를 통해 전달합니다. 심볼별 오더북을 유지하면서 집계 지표(VWAP, 스프레드 SMA, 매수/매도 불균형)를 계산하고, 설정 기반 시그널을 감지합니다 — 수신부터 시그널 발화까지 **E2E 레이턴시 ~3μs**.

## 아키텍처

```
[Binance WebSocket]
    → 수신 스레드 (CPU 0)
        → simdjson 파싱 → DepthUpdate / Trade 구조체
        → SPSC Lock-Free Queue
    → 처리 스레드 (CPU 1)
        → Order Book (std::map, 델타 업데이트)
        → Aggregator (VWAP, Spread SMA, Imbalance)
        → Trade Aggregator (체결 불균형, 거래량 SMA)
        → Signal Detector (9개 시그널 타입, 쿨다운)
    → 출력
        → Binary Disk Logger (리플레이 지원)
        → HTTP /stats 엔드포인트 (JSON 메트릭)
        → WebSocket Server → React 대시보드
```

## 성능

> Apple M4 Air, Release 빌드 (-O2), Google Benchmark

| 컴포넌트 | 레이턴시 | 처리량 |
|----------|---------|--------|
| JSON 파싱 (simdjson) | 2.6 μs | 434 MiB/s |
| JSON 파싱 (nlohmann, 비교 대상) | 8.9 μs | 126 MiB/s |
| SPSC Queue push+pop | 1.9 ns (uint64) / 66 ns (전체 메시지) | 527M / 15M ops/s |
| 오더북 업데이트 (20 레벨) | 5.5 μs | 181K ops/s |
| **E2E 파이프라인 (P50)** | **3.3 μs** | **333K msgs/s** |
| E2E 파이프라인 (P99.9) | 9.3 μs | - |

simdjson은 nlohmann/json 대비 **3.4배 빠름** (direct 포맷 기준).

상세 벤치마크: [docs/benchmarks.md](docs/benchmarks.md)

## 빠른 시작

### Docker (권장)

```bash
docker compose -f docker/docker-compose.yml up
```

엔진: 8080 (HTTP) / 8081 (WebSocket), 대시보드: 3000번 포트.

### 소스 빌드

```bash
# 의존성 (macOS)
brew install boost spdlog yaml-cpp simdjson nlohmann-json openssl

# 빌드
cmake --preset release
cmake --build build/release

# 실행
./build/release/market-engine config/dev.yaml
```

### 벤치마크 실행

```bash
cmake --preset bench
cmake --build build/bench
./scripts/run_bench.sh
```

### 리플레이 모드

```bash
# 녹화 (config에서 recording.enabled 활성화 또는 prod.yaml 사용)
./build/release/market-engine config/prod.yaml

# 5배속 리플레이
./build/release/market-engine --replay recordings/<file>.bin --speed 5x config/dev.yaml
```

## 프로젝트 구조

```
src/
├── core/          # 타입, SPSC 큐, 클럭, 스레드 유틸리티
├── feed/          # WebSocket 클라이언트, 피드 핸들러, 파서 (depth, trade)
├── engine/        # 오더북, 집계기, 시그널 감지기, 처리 스레드
├── output/        # 디스크 로거, HTTP 메트릭, WebSocket 서버
└── replay/        # 리플레이 엔진 (배속 조절)

bench/             # Google Benchmark: JSON 파싱, 큐, 오더북, E2E
test/              # GTest: 9개 테스트 스위트, 녹화 데이터 기반
dashboard/         # React + Vite: 오더북 차트, 메트릭, 시그널 로그, 리플레이
docs/              # 아키텍처, 벤치마크 결과, 시그널 레퍼런스
```

## 기술 스택

| 영역 | 선택 | 이유 |
|------|------|------|
| 언어 | C++17 | `std::optional`, structured bindings, `string_view` |
| WebSocket | Boost.Beast | 크로스플랫폼 (macOS kqueue / Linux epoll) |
| JSON | simdjson (주) | SIMD 가속, nlohmann 대비 3.4배 빠름 |
| 큐 | 자체 구현 SPSC ring buffer | Lock-free, cache line 정렬, acquire/release ordering |
| 로깅 | spdlog | 비동기 모드, 파일 로테이션, 레벨별 로깅 |
| 설정 | yaml-cpp | dev.yaml / prod.yaml 환경 분리 |
| 대시보드 | React + Vite | 경량, WebSocket 기반 실시간 UI |
| 벤치마크 | Google Benchmark | 컴포넌트별 마이크로벤치마크 |
| CI | GitHub Actions | Linux 빌드 + 테스트 + 벤치마크 자동화 |

## 설계 결정

- **SPSC > MPMC**: 생산자 1 → 소비자 1 파이프라인. SPSC가 최소한의 원자 연산으로 가장 낮은 레이턴시 보장
- **simdjson > nlohmann/json**: 벤치마크로 3.4배 차이 확인. nlohmann은 비교 대상으로 유지
- **바이너리 녹화 포맷**: JSON 재파싱 없는 제로 오버헤드 리플레이. 스키마 버전 관리 헤더 포함
- **CPU affinity (Linux)**: 수신 → CPU 0, 처리 → CPU 1. 캐시 오염과 스케줄링 지터 감소
- **Hot path에서 예외 금지**: `std::optional` 반환, 에러 코드 사용. 예외는 초기화 단계에서만
- **쿨다운 메커니즘**: 시그널 type + symbol별 재발화 억제. 노이즈 방지

## 시그널 시스템

9개 시그널 타입을 지원하며, 복합 시그널로 신뢰도를 높입니다.

| 시그널 | 기반 | 신뢰도 |
|--------|------|--------|
| SPREAD_WIDE | 오더북 | 중간 |
| IMBALANCE_BID/ASK | 오더북 | 높음 |
| BOOK_PRESSURE_BID/ASK | 오더북 변화율 | 낮음 |
| PRICE_DEVIATION | 중간가 SMA 이탈 | 중간 |
| TRADE_IMBALANCE_BUY/SELL | 체결 불균형 | 높음 |
| VOLUME_SPIKE | 거래량 급증 | 높음 |

**핵심 복합 시그널**: `IMBALANCE + TRADE_IMBALANCE + VOLUME_SPIKE` 동시 발화 = 강한 방향성 신호

상세 레퍼런스: [docs/signals.md](docs/signals.md)

## 문서

- [아키텍처](docs/architecture.md) — 스레드 모델, 데이터 흐름, 설계 근거
- [벤치마크](docs/benchmarks.md) — 구간별 성능 분석 및 최적화 히스토리
- [시그널 레퍼런스](docs/signals.md) — 시그널별 공식, 임계값, 신뢰도
