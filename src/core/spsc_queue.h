#pragma once

#include <atomic>
#include <cstddef>
#include <new>
#include <type_traits>
#include <optional>

namespace mde::core {

// Lock-free Single-Producer Single-Consumer ring buffer.
//
// Design:
//   - Power-of-two capacity for fast modulo via bitwise AND
//   - Separate cache lines for head/tail to avoid false sharing
//   - Producer writes to tail, consumer reads from head
//   - Uses acquire/release memory ordering (no seq_cst overhead)
//
// Capacity must be a power of two.
template <typename T, size_t Capacity>
class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
    static_assert(Capacity > 0, "Capacity must be greater than zero");

public:
    SPSCQueue() : head_(0), tail_(0) {}

    // Non-copyable, non-movable
    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    // Producer: try to push an element. Returns false if full.
    bool try_push(const T& item) {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t next_tail = (tail + 1) & MASK;

        if (next_tail == head_.load(std::memory_order_acquire)) {
            return false; // Queue is full
        }

        buffer_[tail] = item;
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    // Producer: try to push by move. Returns false if full.
    bool try_push(T&& item) {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t next_tail = (tail + 1) & MASK;

        if (next_tail == head_.load(std::memory_order_acquire)) {
            return false; // Queue is full
        }

        buffer_[tail] = std::move(item);
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    // Consumer: try to pop an element. Returns std::nullopt if empty.
    std::optional<T> try_pop() {
        const size_t head = head_.load(std::memory_order_relaxed);

        if (head == tail_.load(std::memory_order_acquire)) {
            return std::nullopt; // Queue is empty
        }

        T item = std::move(buffer_[head]);
        head_.store((head + 1) & MASK, std::memory_order_release);
        return item;
    }

    // Consumer: try to pop into a reference. Returns false if empty.
    bool try_pop(T& item) {
        const size_t head = head_.load(std::memory_order_relaxed);

        if (head == tail_.load(std::memory_order_acquire)) {
            return false;
        }

        item = std::move(buffer_[head]);
        head_.store((head + 1) & MASK, std::memory_order_release);
        return true;
    }

    // Approximate size (may be slightly stale in concurrent context).
    size_t size_approx() const {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t head = head_.load(std::memory_order_relaxed);
        return (tail - head) & MASK;
    }

    bool empty() const {
        return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_relaxed);
    }

    static constexpr size_t capacity() { return Capacity - 1; } // Usable slots

private:
    static constexpr size_t MASK = Capacity - 1;

    // Pad head and tail to separate cache lines to prevent false sharing.
    alignas(64) std::atomic<size_t> head_;
    alignas(64) std::atomic<size_t> tail_;
    alignas(64) T buffer_[Capacity];
};

} // namespace mde::core
