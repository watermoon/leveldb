// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef STORAGE_LEVELDB_UTIL_RANDOM_H_
#define STORAGE_LEVELDB_UTIL_RANDOM_H_

#include <cstdint>

namespace leveldb {

// A very simple random number generator.  Not especially good at
// generating truly random bits, but good enough for our needs in this
// package.
class Random {
 private:
  uint32_t seed_;

 public:
  explicit Random(uint32_t s) : seed_(s & 0x7fffffffu) {
    // Avoid bad seeds.
    if (seed_ == 0 || seed_ == 2147483647L) {
      seed_ = 1;
    }
  }
  uint32_t Next() {
    static const uint32_t M = 2147483647L;  // 2^31-1
    static const uint64_t A = 16807;        // bits 14, 8, 7, 5, 2, 1, 0
    // We are computing
    //       seed_ = (seed_ * A) % M,    where M = 2^31-1
    //
    // seed_ must not be zero or M, or else all subsequent computed values
    // will be zero or M respectively.  For all other values, seed_ will end
    // up cycling through every number in [1,M-1]
    // 计算 seed_ = (seed_ * A) % M, 其中 M = 2^31 - 1
    // seed_ 必须不为 0 或者 M, 否则所有接下来计算的值都会是 0 或者 M. 对于其他值,
    // seed_ 会在 [1, M-1] 中的每一个值中停下来
    // 神一样的魔数 A
    uint64_t product = seed_ * A;

    // Compute (product % M) using the fact that ((x << 31) % M) == x.
    // 用 ((x << 31) % M) == x 公式来计算 (product % M)
    // 事实却是如此, 但是为什么呢？
    // 尝试"解释"一下:
    // 由于 M = 2^31 - 1, 所以 x % M = x
    // x << 31 = x * 2^31,
    // 假设 x = a + b * M
    // 那么 x * 2^31 % M = (a + b * M) * (M + 1) % M
    //                  = {a * (M + 1) + b * M * (M + 1)} % M
    //                  = a * (M + 1) % M + 0
    //                  = (a * M + a) % M
    //                  = 0 + a % M
    //                  = a
    // 又由于 x < M, 所以有
    // x = a + b * M = a + 0 * M = a
    // 因此: ((x << 31) % M) == x
    // 由此, 推广开来, 假设 M = 2^n - 1
    // 可得 ((x << n) % M) == x
    // 当然, 需要注意避免移位后溢出的情况

    // 下面的计算将 product 拆成了两部分: product = x << 31 + x & M
    seed_ = static_cast<uint32_t>((product >> 31) + (product & M));
    // The first reduction may overflow by 1 bit, so we may need to
    // repeat.  mod == M is not possible; using > allows the faster
    // sign-bit-based test.
    if (seed_ > M) {
      seed_ -= M;
    }
    return seed_;
  }
  // Returns a uniformly distributed value in the range [0..n-1]
  // REQUIRES: n > 0
  uint32_t Uniform(int n) { return Next() % n; }

  // Randomly returns true ~"1/n" of the time, and false otherwise.
  // REQUIRES: n > 0
  bool OneIn(int n) { return (Next() % n) == 0; }

  // Skewed: pick "base" uniformly from range [0,max_log] and then
  // return "base" random bits.  The effect is to pick a number in the
  // range [0,2^max_log-1] with exponential bias towards smaller numbers.
  // 偏态: 从 [0, max_log] 选择一个一个基 ("base"), 然后返回基随机位
  // 效果是: 在范围 [0, 2^max_log-1] 中选一个数字, 这个数字是指数性偏向于(exponential bias)
  // 较小的数字
  // HOW: 这个函数的数学原理有必要学习一下
  uint32_t Skewed(int max_log) { return Uniform(1 << Uniform(max_log + 1)); }
};

}  // namespace leveldb

#endif  // STORAGE_LEVELDB_UTIL_RANDOM_H_
