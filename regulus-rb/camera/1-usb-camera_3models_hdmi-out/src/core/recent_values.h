#ifndef RECENT_VALUES_H_
#define RECENT_VALUES_H_

#include <algorithm>
#include <array>
#include <mutex>

template <typename T, int size>
class RecentValues {
 public:
  void addValue(const T& val) {
    std::lock_guard<std::mutex> lock(mutex);
    if (count < size) {
      sum += val;
      ++count;
    } else {
      sum = sum - container[curr_index] + val;
    }

    container[curr_index] = val;
    curr_index = (++curr_index) % size;
  }

  void addValue(T&& val) {
    std::lock_guard<std::mutex> lock(mutex);
    if (count < size) {
      sum += val;
      ++count;
    } else {
      sum = sum - container[curr_index] + val;
    }

    container[curr_index] = std::move(val);
    curr_index = (++curr_index) % size;
  }

  T getAvg() const {
    std::lock_guard<std::mutex> lock(mutex);
    return count == 0 ? 0 : sum / count;
  }
  int getCount() const {
    std::lock_guard<std::mutex> lock(mutex);
    return count;
  }
  T getMin() const {
    std::lock_guard<std::mutex> lock(mutex);
    return count == 0 ? T{} : *std::min_element(container.begin(), container.begin() + count);
  }
  T getMax() const {
    std::lock_guard<std::mutex> lock(mutex);
    return count == 0 ? T{} : *std::max_element(container.begin(), container.begin() + count);
  }

 private:
  std::array<T, size> container;
  int count = 0;
  T sum = 0;
  int curr_index = 0;
  mutable std::mutex mutex;
};

#endif
