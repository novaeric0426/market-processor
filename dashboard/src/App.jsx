import useEngineSocket from './hooks/useEngineSocket';
import OrderBookChart from './components/OrderBookChart';
import MetricsPanel from './components/MetricsPanel';
import ReplayControls from './components/ReplayControls';
import './App.css';

const WS_URL = `ws://${window.location.hostname || 'localhost'}:8081`;

function App() {
  const { data, status, sendCommand } = useEngineSocket(WS_URL);

  const firstSymbol = data?.symbols?.[0];

  return (
    <div className="app">
      <header className="app-header">
        <h1>Market Data Engine</h1>
        <span className={`connection-badge ${status}`}>{status}</span>
      </header>

      <main className="app-main">
        <div className="left-panel">
          {firstSymbol && (
            <>
              <h2>{firstSymbol.symbol}</h2>
              <OrderBookChart
                bids={firstSymbol.bids}
                asks={firstSymbol.asks}
              />
            </>
          )}

          {/* Additional symbols */}
          {data?.symbols?.slice(1).map((sym) => (
            <div key={sym.symbol} className="additional-symbol">
              <h2>{sym.symbol}</h2>
              <OrderBookChart bids={sym.bids} asks={sym.asks} />
            </div>
          ))}
        </div>

        <div className="right-panel">
          <MetricsPanel data={data} status={status} />
          <ReplayControls data={data} sendCommand={sendCommand} />
        </div>
      </main>
    </div>
  );
}

export default App;
