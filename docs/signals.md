# Signal Reference

Market Data Engine이 실시간으로 감지하는 시그널 목록.
각 시그널은 매 depth 메시지 처리 시 평가되며, 타입+심볼별 쿨다운으로 반복 발화를 억제한다.

---

## Order Book Signals

오더북 호가 데이터에서 계산되는 시그널.

### SPREAD_WIDE

최우선 매수/매도 호가 사이의 가격 간격이 임계값을 초과할 때 발동.

```
spread = best_ask_price - best_bid_price

발동 조건: spread > threshold
기본 임계값: 50.0
기본 쿨다운: 1초
```

유동성이 일시적으로 빠진 상태를 감지한다. 매매 진입보다는 **회피 필터**로 유용하다.

### IMBALANCE_BID / IMBALANCE_ASK

호가창 전체 매수잔량과 매도잔량의 비율 쏠림을 감지.

```
imbalance = (total_bid_qty - total_ask_qty) / (total_bid_qty + total_ask_qty)

범위: -1.0 (매도 100%) ~ +1.0 (매수 100%)

IMBALANCE_BID 발동: imbalance > threshold
IMBALANCE_ASK 발동: imbalance < -threshold
기본 임계값: 0.7
기본 쿨다운: 1초
```

호가창 수급의 기본 지표. 복합 시그널의 핵심 재료.

### BOOK_PRESSURE_BID / BOOK_PRESSURE_ASK

imbalance의 변화량(이전 대비 delta)으로 수급 방향 전환을 감지.

```
imbalance_delta = 현재_imbalance - 이전_imbalance

BOOK_PRESSURE_BID 발동: imbalance_delta > threshold
BOOK_PRESSURE_ASK 발동: imbalance_delta < -threshold
기본 임계값: 0.15
기본 쿨다운: 1초
```

imbalance 자체보다 노이즈가 많을 수 있다. 호가창 스푸핑(허수 주문)에 취약하므로 단독 사용보다 보조 지표로 적합.

### PRICE_DEVIATION

현재 중간가가 이동평균(SMA)에서 벗어난 정도를 감지.

```
mid_price = (best_bid + best_ask) / 2
mid_price_sma = 최근 20개 mid_price의 단순이동평균

deviation = |mid_price - mid_price_sma| / mid_price_sma

발동 조건: deviation > threshold
기본 임계값: 0.001 (0.1%)
기본 쿨다운: 1초
```

급등/급락 초기 신호. SMA 윈도우(20샘플) 대비 가격 이탈을 측정한다.

---

## Trade Signals

실시간 체결 스트림에서 계산되는 시그널. 1초 슬라이딩 윈도우 기반.

### TRADE_IMBALANCE_BUY / TRADE_IMBALANCE_SELL

시장가 매수/매도 체결량의 비율 쏠림을 감지.

```
최근 1초 윈도우 내 체결 기준:

buy_volume  = 매수 공격 체결량 합 (is_buyer_maker = false)
sell_volume = 매도 공격 체결량 합 (is_buyer_maker = true)

trade_imbalance = (buy_volume - sell_volume) / (buy_volume + sell_volume)

범위: -1.0 (매도 100%) ~ +1.0 (매수 100%)

TRADE_IMBALANCE_BUY 발동:  trade_imbalance > threshold
TRADE_IMBALANCE_SELL 발동: trade_imbalance < -threshold
기본 임계값: 0.8
기본 쿨다운: 3초
```

호가 잔량(의향)이 아닌 실제 체결(실행)을 보기 때문에 IMBALANCE_BID/ASK보다 신뢰도가 높다.

### VOLUME_SPIKE

현재 윈도우 거래량이 평소 대비 급증했는지 감지.

```
total_volume = 현재 1초 윈도우 거래량
volume_sma   = 최근 20개 완료된 윈도우 거래량의 단순이동평균

spike_ratio = total_volume / volume_sma

발동 조건: spike_ratio > threshold
기본 임계값: 3.0 (평소의 3배)
기본 쿨다운: 1초
```

큰 가격 변동의 전조. 거의 모든 전략에서 확인 필터로 사용된다.

---

## Aggregation Values (Non-Signal)

시그널 조건으로 사용되지 않지만 대시보드에 표시되는 집계값.

### VWAP (Volume-Weighted Average Price)

```
bid_vwap = Σ(price × qty) / Σ(qty)  (상위 5개 매수호가)
ask_vwap = Σ(price × qty) / Σ(qty)  (상위 5개 매도호가)
```

### Spread SMA

```
spread_sma = 최근 20개 spread 값의 단순이동평균
```

---

## Cooldown

동일 시그널 타입 + 심볼 조합이 발화된 후 쿨다운 기간 내에는 재발화하지 않는다.

```cpp
struct SignalCondition {
    SignalType type;
    double threshold;
    uint64_t cooldown_us = 1'000'000;  // 기본 1초
};
```

쿨다운을 0으로 설정하면 매 평가마다 발동 가능.

---

## Signal Reliability Summary

| 시그널 | 신뢰도 | 용도 |
|--------|--------|------|
| IMBALANCE_BID/ASK | 높음 | 수급 판단 핵심 재료 |
| TRADE_IMBALANCE_BUY/SELL | 높음 | 실제 체결 기반, 가장 신뢰할 수 있는 방향 지표 |
| VOLUME_SPIKE | 높음 | 변동성 확대 확인 필터 |
| SPREAD_WIDE | 중간 | 유동성 회피 필터 |
| PRICE_DEVIATION | 중간 | 평균 회귀 전략 재료 |
| BOOK_PRESSURE_BID/ASK | 낮음 | 스푸핑에 취약, 보조 지표 |

핵심 조합: **IMBALANCE + TRADE_IMBALANCE + VOLUME_SPIKE** 세 축이 동시에 충족될 때 유의미한 매매 시그널이 된다.

---

## Configuration

시그널 조건은 `src/main.cpp`에서 하드코딩되어 있다.

```cpp
std::vector<mde::engine::SignalCondition> signals = {
    {SignalType::SPREAD_WIDE,          50.0},
    {SignalType::IMBALANCE_BID,         0.7},
    {SignalType::IMBALANCE_ASK,         0.7},
    {SignalType::PRICE_DEVIATION,       0.001},
    {SignalType::TRADE_IMBALANCE_BUY,   0.8, 3'000'000},
    {SignalType::TRADE_IMBALANCE_SELL,  0.8, 3'000'000},
    {SignalType::VOLUME_SPIKE,          3.0},
    {SignalType::BOOK_PRESSURE_BID,     0.15},
    {SignalType::BOOK_PRESSURE_ASK,     0.15},
};
```

향후 YAML config로 이동하여 런타임 설정 가능하도록 확장 예정.
