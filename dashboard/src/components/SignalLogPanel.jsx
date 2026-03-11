import { useState, useEffect, useRef } from 'react';

const MAX_SIGNALS = 50;

const SIGNAL_COLORS = {
  SPREAD_WIDE: '#d29922',
  SPREAD_NARROW: '#d29922',
  IMBALANCE_BID: '#3fb950',
  IMBALANCE_ASK: '#f85149',
  PRICE_DEVIATION: '#bc8cff',
  TRADE_IMBALANCE_BUY: '#3fb950',
  TRADE_IMBALANCE_SELL: '#f85149',
  VOLUME_SPIKE: '#f0883e',
  BOOK_PRESSURE_BID: '#3fb950',
  BOOK_PRESSURE_ASK: '#f85149',
};

function formatTime(timestampUs) {
  const ms = Math.floor(timestampUs / 1000);
  const d = new Date(ms);
  const h = String(d.getHours()).padStart(2, '0');
  const m = String(d.getMinutes()).padStart(2, '0');
  const s = String(d.getSeconds()).padStart(2, '0');
  const frac = String(d.getMilliseconds()).padStart(3, '0');
  return `${h}:${m}:${s}.${frac}`;
}

function formatValue(value) {
  if (Math.abs(value) >= 1) return value.toFixed(2);
  return value.toFixed(4);
}

export default function SignalLogPanel({ data }) {
  const [signals, setSignals] = useState([]);
  const seenRef = useRef(new Set());
  const listRef = useRef(null);

  useEffect(() => {
    if (!data?.recent_signals?.length) return;

    setSignals((prev) => {
      // Deduplicate by timestamp_us + type + symbol
      const newSignals = [];
      for (const sig of data.recent_signals) {
        const key = `${sig.timestamp_us}-${sig.type}-${sig.symbol}`;
        if (!seenRef.current.has(key)) {
          seenRef.current.add(key);
          newSignals.push(sig);
        }
      }
      if (newSignals.length === 0) return prev;

      const merged = [...newSignals, ...prev].slice(0, MAX_SIGNALS);

      // Trim seen set
      if (seenRef.current.size > MAX_SIGNALS * 2) {
        const keep = new Set(merged.map((s) => `${s.timestamp_us}-${s.type}-${s.symbol}`));
        seenRef.current = keep;
      }

      return merged;
    });
  }, [data]);

  return (
    <div className="signal-log-panel">
      <h3>Signal Log</h3>
      <div className="signal-log-list" ref={listRef}>
        {signals.length === 0 ? (
          <div className="signal-empty">No signals yet</div>
        ) : (
          signals.map((sig, i) => (
            <div key={`${sig.timestamp_us}-${sig.type}-${i}`} className="signal-row">
              <span className="signal-time">{formatTime(sig.timestamp_us)}</span>
              <span
                className="signal-type"
                style={{ color: SIGNAL_COLORS[sig.type] || '#8b949e' }}
              >
                {sig.type}
              </span>
              <span className="signal-symbol">{sig.symbol}</span>
              <span className="signal-value">{formatValue(sig.value)}</span>
            </div>
          ))
        )}
      </div>
    </div>
  );
}
