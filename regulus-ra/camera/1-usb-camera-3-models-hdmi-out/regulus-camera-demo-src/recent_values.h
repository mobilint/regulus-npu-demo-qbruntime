#ifndef RECENT_VALUES_H_
#define RECENT_VALUES_H_

#include <array>

template <typename T, int size>
class RecentValues {
public:
    void addValue(const T& val) {
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
        if (count < size) {
            sum += val;
            ++count;
        } else {
            sum = sum - container[curr_index] + val;
        }

        container[curr_index] = std::move(val);
        curr_index = (++curr_index) % size;
    }

    const T getAvg() { return count == 0 ? 0 : sum / count; }

private:
    std::array<T, size> container;
    int count = 0;
    T sum = 0;
    int curr_index = 0;
};

#endif
