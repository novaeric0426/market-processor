import { useState, useEffect, useRef } from 'react';

const MAX_HISTORY = 120; // 120 samples * 100ms push interval = 12 seconds of recent data

export default function MetricsPanel({ data, status }) {
  const [latencyHistory, setLatencyHistory] = useState([]);
  const [throughputHistory, setThroughputHistory] = useState([]);
  const prevProcessed = useRef(0);
  const prevTime = useRef(Date.now());

  useEffect(() => {
    if (!data) return;

    // Latency history
    setLatencyHistory((prev) => {
      const next = [
        ...prev,
        {
          time: Date.now(),
          parse: data.latency?.parse_us || 0,
          queue: data.latency?.queue_us || 0,
          total: data.latency?.total_us || 0,
        },
      ];
      return next.slice(-MAX_HISTORY);
    });

    // Throughput (messages per second)
    const now = Date.now();
    const dt = (now - prevTime.current) / 1000;
    const processed = data.processed || 0;
    if (dt > 0 && prevProcessed.current > 0) {
      const mps = (processed - prevProcessed.current) / dt;
      setThroughputHistory((prev) => {
        const next = [...prev, { time: now, mps }];
        return next.slice(-MAX_HISTORY);
      });
    }
    prevProcessed.current = processed;
    prevTime.current = now;
  }, [data]);

  const uptime = data?.uptime_s || 0;
  const uptimeStr = `${Math.floor(uptime / 60)}m ${uptime % 60}s`;

  const latestThroughput =
    throughputHistory.length > 0
      ? throughputHistory[throughputHistory.length - 1].mps
      : 0;

  return (
    <div className="metrics-panel">
      <div className="metrics-grid">
        <MetricCard
          label="Status"
          value={status}
          className={`status-${status}`}
        />
        <MetricCard label="Uptime" value={uptimeStr} />
        <MetricCard label="Processed" value={data?.processed?.toLocaleString() || '0'} />
        <MetricCard label="Signals" value={data?.signals_fired?.toLocaleString() || '0'} />
        <MetricCard label="Queue Depth" value={data?.queue_depth?.toLocaleString() || '0'} />
        <MetricCard
          label="Throughput"
          value={`${latestThroughput.toFixed(1)} msg/s`}
        />
      </div>

      <div className="latency-section">
        <h3>Latency (us)</h3>
        <div className="latency-bars">
          <LatencyBar label="Parse" value={data?.latency?.parse_us || 0} max={200} color="#4caf50" />
          <LatencyBar label="Queue" value={data?.latency?.queue_us || 0} max={200} color="#2196f3" />
          <LatencyBar label="Total" value={data?.latency?.total_us || 0} max={200} color="#ff9800" />
        </div>
      </div>

      {/* Latency sparkline */}
      <div className="sparkline-section">
        <h3>Latency History (total us)</h3>
        <Sparkline
          data={latencyHistory.map((h) => h.total)}
          color="#ff9800"
          height={60}
        />
      </div>

      {/* Feed or Replay stats */}
      {data?.feed && (
        <div className="feed-stats">
          <h3>Feed</h3>
          <div className="metrics-grid small">
            <MetricCard label="Received" value={data.feed.received?.toLocaleString()} />
            <MetricCard label="Parse Errors" value={data.feed.parse_errors} />
            <MetricCard label="Queue Full" value={data.feed.queue_full} />
          </div>
        </div>
      )}

      {data?.replay && (
        <div className="replay-stats">
          <h3>Replay</h3>
          <div className="metrics-grid small">
            <MetricCard
              label="Progress"
              value={`${data.replay.replayed} / ${data.replay.total}`}
            />
            <MetricCard label="Speed" value={data.replay.speed} />
            <MetricCard
              label="State"
              value={data.replay.finished ? 'Finished' : data.replay.paused ? 'Paused' : 'Playing'}
            />
          </div>
        </div>
      )}
    </div>
  );
}

function MetricCard({ label, value, className = '' }) {
  return (
    <div className={`metric-card ${className}`}>
      <div className="metric-label">{label}</div>
      <div className="metric-value">{value}</div>
    </div>
  );
}

function LatencyBar({ label, value, max, color }) {
  const pct = Math.min((value / max) * 100, 100);
  return (
    <div className="latency-bar-row">
      <span className="latency-label">{label}</span>
      <div className="latency-bar-track">
        <div
          className="latency-bar-fill"
          style={{ width: `${pct}%`, backgroundColor: color }}
        />
      </div>
      <span className="latency-value">{value}</span>
    </div>
  );
}

function Sparkline({ data, color, height = 40 }) {
  if (data.length < 2) return <div style={{ height }} />;

  const max = Math.max(...data, 1);
  const w = 300;
  const points = data
    .map((v, i) => {
      const x = (i / (data.length - 1)) * w;
      const y = height - (v / max) * height;
      return `${x},${y}`;
    })
    .join(' ');

  return (
    <svg width={w} height={height} className="sparkline">
      <polyline
        fill="none"
        stroke={color}
        strokeWidth="1.5"
        points={points}
      />
    </svg>
  );
}
