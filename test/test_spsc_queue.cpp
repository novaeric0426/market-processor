#include "core/spsc_queue.h"

#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <numeric>

using namespace mde::core;

TEST(SPSCQueue, PushPopBasic) {
    SPSCQueue<int, 16> q;
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.size_approx(), 0u);

    EXPECT_TRUE(q.try_push(42));
    EXPECT_FALSE(q.empty());
    EXPECT_EQ(q.size_approx(), 1u);

    auto val = q.try_pop();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 42);
    EXPECT_TRUE(q.empty());
}

TEST(SPSCQueue, PopEmpty) {
    SPSCQueue<int, 8> q;
    auto val = q.try_pop();
    EXPECT_FALSE(val.has_value());

    int out = -1;
    EXPECT_FALSE(q.try_pop(out));
    EXPECT_EQ(out, -1);
}

TEST(SPSCQueue, FullQueue) {
    SPSCQueue<int, 4> q; // capacity = 3 usable slots
    EXPECT_TRUE(q.try_push(1));
    EXPECT_TRUE(q.try_push(2));
    EXPECT_TRUE(q.try_push(3));
    EXPECT_FALSE(q.try_push(4)); // Full

    EXPECT_EQ(q.size_approx(), 3u);

    auto val = q.try_pop();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 1);

    EXPECT_TRUE(q.try_push(4)); // Space freed
}

TEST(SPSCQueue, FIFO) {
    SPSCQueue<int, 32> q;
    for (int i = 0; i < 20; ++i) {
        EXPECT_TRUE(q.try_push(i));
    }
    for (int i = 0; i < 20; ++i) {
        auto val = q.try_pop();
        ASSERT_TRUE(val.has_value());
        EXPECT_EQ(*val, i);
    }
}

TEST(SPSCQueue, WrapAround) {
    SPSCQueue<int, 4> q; // 3 usable slots
    // Fill and drain multiple times to wrap around the ring buffer
    for (int round = 0; round < 10; ++round) {
        EXPECT_TRUE(q.try_push(round * 10 + 1));
        EXPECT_TRUE(q.try_push(round * 10 + 2));
        EXPECT_TRUE(q.try_push(round * 10 + 3));

        EXPECT_EQ(*q.try_pop(), round * 10 + 1);
        EXPECT_EQ(*q.try_pop(), round * 10 + 2);
        EXPECT_EQ(*q.try_pop(), round * 10 + 3);
        EXPECT_TRUE(q.empty());
    }
}

TEST(SPSCQueue, MoveSemantics) {
    SPSCQueue<std::string, 8> q;
    std::string s = "hello world";
    EXPECT_TRUE(q.try_push(std::move(s)));

    auto val = q.try_pop();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, "hello world");
}

TEST(SPSCQueue, ConcurrentProducerConsumer) {
    constexpr int N = 100000;
    SPSCQueue<int, 1024> q;
    std::vector<int> received;
    received.reserve(N);

    std::thread producer([&]() {
        for (int i = 0; i < N; ++i) {
            while (!q.try_push(i)) {
                std::this_thread::yield();
            }
        }
    });

    std::thread consumer([&]() {
        for (int i = 0; i < N; ++i) {
            int val;
            while (!q.try_pop(val)) {
                std::this_thread::yield();
            }
            received.push_back(val);
        }
    });

    producer.join();
    consumer.join();

    // Verify all values received in order
    ASSERT_EQ(received.size(), static_cast<size_t>(N));
    for (int i = 0; i < N; ++i) {
        EXPECT_EQ(received[i], i) << "Mismatch at index " << i;
    }
}

TEST(SPSCQueue, Capacity) {
    SPSCQueue<int, 64> q;
    EXPECT_EQ(q.capacity(), 63u); // One slot reserved for full/empty distinction
}
