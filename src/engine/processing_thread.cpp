#include "engine/processing_thread.h"

#include <spdlog/spdlog.h>
#include <thread>
#include <chrono>

namespace mde::engine {

ProcessingThread::ProcessingThread(feed::FeedQueue& queue,
                                   std::vector<SignalCondition> signal_conditions,
                                   output::DiskLogger* recorder)
    : queue_(queue)
    , recorder_(recorder)
    , signal_detector_([this](const Signal& sig) {
          signals_fired_.fetch_add(1, std::memory_order_relaxed);
          spdlog::info("SIGNAL [{}] {} value={:.6f} threshold={:.6f}",
              sig.symbol, signal_type_name(sig.type), sig.value, sig.threshold);
      })
    , signal_conditions_(std::move(signal_conditions))
{
    for (const auto& cond : signal_conditions_) {
        signal_detector_.add_condition(cond);
    }
}

ProcessingThread::~ProcessingThread() {
    stop();
}

void ProcessingThread::start(std::atomic<bool>& running) {
    thread_ = std::thread([this, &running]() {
        spdlog::info("Processing thread started");
        run(running);
        spdlog::info("Processing thread exited");
    });
}

void ProcessingThread::stop() {
    if (thread_.joinable()) {
        thread_.join();
    }
}

void ProcessingThread::run(std::atomic<bool>& running) {
    core::QueueMessage msg;

    while (running.load(std::memory_order_relaxed)) {
        if (queue_.try_pop(msg)) {
            msg.depth.ts_dequeued = core::Clock::now();
            process(msg);
        } else {
            std::this_thread::yield();
        }
    }

    // Drain remaining messages
    while (queue_.try_pop(msg)) {
        msg.depth.ts_dequeued = core::Clock::now();
        process(msg);
    }
}

void ProcessingThread::process(core::QueueMessage& msg) {
    auto count = processed_.fetch_add(1, std::memory_order_relaxed) + 1;

    // Record raw data before processing (if recording enabled)
    if (recorder_) {
        recorder_->write(msg.depth);
    }

    // Compute per-stage latencies
    auto parse_us = core::Clock::elapsed_us(msg.depth.ts_received, msg.depth.ts_parsed);
    auto queue_us = core::Clock::elapsed_us(msg.depth.ts_parsed, msg.depth.ts_dequeued);
    auto total_us = core::Clock::elapsed_us(msg.depth.ts_received, msg.depth.ts_dequeued);

    last_parse_us_.store(parse_us, std::memory_order_relaxed);
    last_queue_us_.store(queue_us, std::memory_order_relaxed);
    last_total_us_.store(total_us, std::memory_order_relaxed);

    // Update order book and aggregator
    auto& state = get_or_create_state(msg.depth.symbol);
    state.book.apply_update(msg.depth);
    state.aggregator.update(state.book);

    // Run signal detection
    signal_detector_.evaluate(msg.depth.symbol, state.book, state.aggregator.snapshot());

    // Publish snapshot for monitoring servers (every message)
    update_snapshot();

    // Periodic logging
    if (count <= 3 || count % 100 == 0) {
        const auto& snap = state.aggregator.snapshot();
        spdlog::debug("[processed #{}] {} bid={:.2f} ask={:.2f} spread={:.2f} "
                      "mid_sma={:.2f} imbalance={:.3f} | "
                      "parse={}us queue={}us total={}us",
            count, msg.depth.symbol,
            state.book.best_bid_price(), state.book.best_ask_price(),
            snap.spread, snap.mid_price_sma, snap.bid_ask_imbalance,
            parse_us, queue_us, total_us);
    }
}

ProcessingThread::SymbolState& ProcessingThread::get_or_create_state(const std::string& symbol) {
    auto it = states_.find(symbol);
    if (it != states_.end()) {
        return *it->second;
    }
    auto [inserted, _] = states_.emplace(symbol, std::make_unique<SymbolState>(symbol));
    spdlog::info("Created order book for symbol: {}", symbol);
    return *inserted->second;
}

const OrderBook* ProcessingThread::get_order_book(const std::string& symbol) const {
    auto it = states_.find(symbol);
    return it != states_.end() ? &it->second->book : nullptr;
}

const AggregatorSnapshot* ProcessingThread::get_aggregator(const std::string& symbol) const {
    auto it = states_.find(symbol);
    return it != states_.end() ? &it->second->aggregator.snapshot() : nullptr;
}

void ProcessingThread::update_snapshot() {
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    snapshot_.processed_count = processed_.load(std::memory_order_relaxed);
    snapshot_.signals_fired = signals_fired_.load(std::memory_order_relaxed);
    snapshot_.last_parse_us = last_parse_us_.load(std::memory_order_relaxed);
    snapshot_.last_queue_us = last_queue_us_.load(std::memory_order_relaxed);
    snapshot_.last_total_us = last_total_us_.load(std::memory_order_relaxed);

    snapshot_.symbols.clear();
    snapshot_.symbols.reserve(states_.size());
    for (const auto& [sym, state] : states_) {
        SymbolSnapshot ss;
        ss.symbol = sym;
        ss.bids = state->book.bids();
        ss.asks = state->book.asks();
        ss.aggregation = state->aggregator.snapshot();
        ss.best_bid = state->book.best_bid_price();
        ss.best_ask = state->book.best_ask_price();
        ss.spread = state->book.spread();
        ss.mid_price = state->book.mid_price();
        ss.update_count = state->book.update_count();
        snapshot_.symbols.push_back(std::move(ss));
    }
}

EngineSnapshot ProcessingThread::take_snapshot() const {
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    return snapshot_;
}

} // namespace mde::engine
