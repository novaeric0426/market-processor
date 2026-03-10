export default function ReplayControls({ data, sendCommand }) {
  if (!data?.replay) return null;

  const { replayed, total, finished, paused, speed } = data.replay;
  const progress = total > 0 ? (replayed / total) * 100 : 0;

  return (
    <div className="replay-controls">
      <h3>Replay Controls</h3>

      {/* Progress bar */}
      <div className="progress-container">
        <div className="progress-bar">
          <div
            className="progress-fill"
            style={{ width: `${progress}%` }}
          />
        </div>
        <span className="progress-text">
          {replayed} / {total} ({progress.toFixed(1)}%)
        </span>
      </div>

      {/* Control buttons */}
      <div className="control-buttons">
        <button
          className={`btn ${paused ? 'btn-primary' : ''}`}
          onClick={() => sendCommand({ cmd: paused ? 'resume' : 'pause' })}
          disabled={finished}
        >
          {paused ? 'Resume' : 'Pause'}
        </button>

        {/* Speed buttons */}
        {['1x', '2x', '5x', 'max'].map((s) => (
          <button
            key={s}
            className={`btn ${speed === s ? 'btn-active' : ''}`}
            onClick={() => sendCommand({ cmd: 'speed', value: s })}
            disabled={finished}
          >
            {s}
          </button>
        ))}
      </div>

      {finished && <div className="replay-finished">Replay Complete</div>}
    </div>
  );
}
