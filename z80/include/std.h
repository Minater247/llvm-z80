#pragma once

using int8_t = char;
using uint8_t = unsigned char;
using int16_t = int;
using uint16_t = unsigned int;
using int32_t = long;
using uint32_t = unsigned long;
using size_t = uint16_t;

extern "C" {
  void * memset(void *b, char c, size_t len);
  void * memcpy(void *dst, const void *src, size_t n);
};

namespace std {
    using int8_t = ::int8_t;

    template <typename T, size_t N>
    struct array {
        T data[N];  // Fixed-size array storage

        // Access operators
        constexpr T& operator[](size_t index) { return data[index]; }
        constexpr const T& operator[](size_t index) const { return data[index]; }

        // Get size of the array
        constexpr size_t size() const { return N; }

        // Get a pointer to the underlying data
        constexpr T* begin() { return data; }
        constexpr const T* begin() const { return data; }
        constexpr T* end() { return data + N; }
        constexpr const T* end() const { return data + N; }

        // Direct assignment
        constexpr array& operator=(const array& other) {
            for (size_t i = 0; i < N; i++) {
                data[i] = other.data[i];
            }
            return *this;
        }
    };
} // namespace std

