# Architecture

## Overview

실시간 시장 데이터 수신 → 파싱 → 정규화 → 집계 → 시그널 판단까지의 저지연 파이프라인.

```
[Binance WebSocket]
    → Feed Handler (수신 스레드, CPU 0)
        → SSL WebSocket 연결 (Boost.Beast)
        → simdjson JSON 파싱
        → DepthUpdate 구조체 변환
        → SPSC Queue 투입
    → Processing Engine (처리 스레드, CPU 1)
        → Queue 소비
        → Order Book Manager (심볼별 오더북 유지)
        → Aggregator (VWAP, Spread, SMA, Imbalance)
        → Signal Detector (조건 충족 시 이벤트 발행)
    → Output Layer
        → Disk Logger (바이너리 기록, 리플레이용)
        → Metrics Server (HTTP GET /stats)
        → WebSocket Server (대시보드 실시간 피드)

[Web Dashboard]
    ← WebSocket ← Engine (100ms 주기)
    → 오더북 depth chart
    → 메트릭 차트 (throughput, latency, queue depth)
    → 리플레이 모드 (배속 조절)
```

## Thread Model

| Thread | Role | CPU Affinity (Linux) |
|--------|------|---------------------|
| Feed | WebSocket 수신 + JSON 파싱 + 큐 투입 | CPU 0 |
| Processing | 큐 소비 + 오더북 + 집계 + 시그널 | CPU 1 |
| Metrics HTTP | `/stats`, `/health` 응답 | - |
| WS Broadcaster | 대시보드 WebSocket push (100ms) | - |
| WS Acceptor | 대시보드 WebSocket 연결 수락 | - |

Feed → Processing 간 통신은 **lock-free SPSC ring buffer**를 사용하며, 두 스레드 간 공유 상태는 큐 외에 없습니다.

## Data Flow

### 1. Feed Handler
- Binance `wss://stream.binance.com:9443` 연결
- 심볼별 depth stream 구독 (`btcusdt@depth@100ms`)
- 수신 즉시 `ts_received` 타임스탬프 기록
- simdjson ondemand parser로 JSON → `DepthUpdate` 변환
- `ts_parsed` 기록 후 SPSC queue에 push

### 2. SPSC Queue
- 용량: 8192 슬롯 (power of 2, bitwise AND 모듈로)
- 캐시라인 정렬: head/tail을 64바이트 경계에 배치해 false sharing 방지
- 메모리 오더링: acquire/release (seq_cst 대비 낮은 오버헤드)
- 큐 full 시 메시지 드롭 + 카운터 증가 (back-pressure 없음)

### 3. Processing Engine
- 큐에서 pop 시 `ts_dequeued` 기록
- 심볼별 `OrderBook` + `Aggregator` 인스턴스 관리
- `std::map` 기반 오더북: bid(내림차순), ask(오름차순)
- 델타 업데이트: quantity=0이면 해당 가격 레벨 삭제
- 집계: VWAP (top N levels), spread, mid price SMA, bid-ask imbalance
- 시그널: 5가지 조건 (SPREAD_WIDE, SPREAD_NARROW, IMBALANCE_BID, IMBALANCE_ASK, PRICE_DEVIATION)

### 4. Output Layer
- **DiskLogger**: 바이너리 포맷 (MDE1 매직, 레코드 헤더 + DepthRecord)
- **MetricsServer**: Boost.Beast HTTP, JSON 응답
- **WsServer**: Boost.Beast WebSocket, 100ms 주기 브로드캐스트

## Key Design Decisions

### simdjson vs nlohmann/json
simdjson은 SIMD 명령어를 활용해 **3.4x 빠른 파싱 성능**을 제공.
nlohmann/json은 fallback으로 유지하되, 벤치마크로 차이를 문서화.

### SPSC vs MPMC
현재 아키텍처는 1:1 파이프라인이므로 SPSC가 최적.
SPSC는 원자적 카운터 2개만 사용하여 MPMC 대비 훨씬 낮은 오버헤드.
다중 처리 스레드가 필요해지면 심볼별 샤딩 + SPSC 다중화로 확장.

### Binary Recording Format
리플레이 시 JSON 재파싱 오버헤드를 제거하기 위해 내부 구조체를 직접 직렬화.
24바이트 파일 헤더에 매직 넘버 + 스키마 버전을 포함하여 포맷 변경에 대응.

### CPU Affinity
Linux 환경에서 feed thread와 processing thread를 고정 코어에 배치.
캐시 오염(cache pollution)을 줄이고, 컨텍스트 스위칭으로 인한 레이턴시 스파이크 방지.
macOS에서는 no-op (macOS는 thread affinity API를 지원하지 않음).
