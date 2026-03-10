import { useMemo } from 'react';

export default function OrderBookChart({ bids = [], asks = [] }) {
  // Compute cumulative depth for visualization
  const { bidDepth, askDepth, maxQty, midPrice } = useMemo(() => {
    let cumBid = 0;
    const bidDepth = bids.map(({ price, qty }) => {
      cumBid += qty;
      return { price, qty, cumQty: cumBid };
    });

    let cumAsk = 0;
    const askDepth = asks.map(({ price, qty }) => {
      cumAsk += qty;
      return { price, qty, cumQty: cumAsk };
    });

    const maxQty = Math.max(
      bidDepth.length > 0 ? bidDepth[bidDepth.length - 1].cumQty : 0,
      askDepth.length > 0 ? askDepth[askDepth.length - 1].cumQty : 0,
      1
    );

    const midPrice =
      bids.length > 0 && asks.length > 0
        ? (bids[0].price + asks[0].price) / 2
        : 0;

    return { bidDepth, askDepth, maxQty, midPrice };
  }, [bids, asks]);

  if (bids.length === 0 && asks.length === 0) {
    return <div className="chart-empty">Waiting for order book data...</div>;
  }

  const barHeight = 22;
  const chartHeight = Math.max(bidDepth.length, askDepth.length) * barHeight;

  return (
    <div className="orderbook-chart">
      <div className="orderbook-header">
        <span className="label-bid">Bids</span>
        <span className="mid-price">
          Mid: {midPrice.toFixed(2)}
        </span>
        <span className="label-ask">Asks</span>
      </div>
      <div className="orderbook-body" style={{ height: chartHeight }}>
        {/* Bids (left side, green) */}
        <div className="orderbook-side bids-side">
          {bidDepth.map((level, i) => (
            <div key={i} className="depth-row">
              <span className="price bid-price">{level.price.toFixed(2)}</span>
              <div className="bar-container">
                <div
                  className="bar bid-bar"
                  style={{ width: `${(level.cumQty / maxQty) * 100}%` }}
                />
              </div>
              <span className="qty">{level.qty.toFixed(4)}</span>
            </div>
          ))}
        </div>

        {/* Asks (right side, red) */}
        <div className="orderbook-side asks-side">
          {askDepth.map((level, i) => (
            <div key={i} className="depth-row">
              <span className="qty">{level.qty.toFixed(4)}</span>
              <div className="bar-container">
                <div
                  className="bar ask-bar"
                  style={{ width: `${(level.cumQty / maxQty) * 100}%` }}
                />
              </div>
              <span className="price ask-price">{level.price.toFixed(2)}</span>
            </div>
          ))}
        </div>
      </div>
    </div>
  );
}
