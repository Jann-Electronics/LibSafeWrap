#pragma once
#include <utility>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// [[nodiscard]] requires C++17 (Arduino-ESP32 3.x). Omitted on 2.x (gnu++11).
#if __cplusplus >= 201703L
#define SAFEWRAP_NODISCARD [[nodiscard]]
#else
#define SAFEWRAP_NODISCARD
#endif

template <typename T>
class SafeWrap {
  private:
  T value;
  SemaphoreHandle_t mutex;

  static bool take(SemaphoreHandle_t m, TickType_t timeout) {
    configASSERT(!xPortInIsrContext()); // FreeRTOS mutexes cannot be used from an ISR
    return xSemaphoreTake(m, timeout) == pdTRUE;
  }

  // Internal lock: releases the mutex on every exit path (return or exception).
  class ScopedTake {
    private:
    SemaphoreHandle_t mutex;

    public:
    ScopedTake(SemaphoreHandle_t m, TickType_t timeout)
        : mutex(take(m, timeout) ? m : nullptr) {}

    ~ScopedTake() {
      if (mutex) {
        xSemaphoreGive(mutex);
      }
    }

    explicit operator bool() const { return mutex != nullptr; }

    ScopedTake(const ScopedTake&)            = delete;
    ScopedTake& operator=(const ScopedTake&) = delete;
  };

  public:
  // Every operation requires an explicit timeout (like xSemaphoreTake).
  static constexpr TickType_t NO_WAIT      = 0;
  static constexpr TickType_t WAIT_FOREVER = portMAX_DELAY;

  class LockGuard {
    private:
    SafeWrap<T>* wrapper; // nullptr if the lock was not acquired

    LockGuard(SafeWrap<T>* w, TickType_t timeout)
        : wrapper(take(w->mutex, timeout) ? w : nullptr) {}

    friend class SafeWrap;

    public:
    // Movable so getLock() can return it by value, also in C++11.
    LockGuard(LockGuard&& other) noexcept
        : wrapper(other.wrapper) {
      other.wrapper = nullptr;
    }

    ~LockGuard() { unlock(); }

    bool isLocked() const { return wrapper != nullptr; }
    explicit operator bool() const { return isLocked(); }

    T& get() const {
      configASSERT(isLocked());
      return wrapper->value;
    }

    T& operator*() const { return get(); }
    T* operator->() const { return &get(); }

    // Releases the mutex before leaving the scope.
    void unlock() {
      if (wrapper) {
        xSemaphoreGive(wrapper->mutex);
        wrapper = nullptr;
      }
    }

    LockGuard(const LockGuard&)            = delete;
    LockGuard& operator=(const LockGuard&) = delete;
    LockGuard& operator=(LockGuard&&)      = delete;
  };

  SafeWrap()
      : value()
      , mutex(xSemaphoreCreateMutex()) {
    configASSERT(mutex);
  }

  explicit SafeWrap(const T& initialValue)
      : value(initialValue)
      , mutex(xSemaphoreCreateMutex()) {
    configASSERT(mutex);
  }

  explicit SafeWrap(T&& initialValue)
      : value(std::move(initialValue))
      , mutex(xSemaphoreCreateMutex()) {
    configASSERT(mutex);
  }

  ~SafeWrap() {
    if (mutex) {
      vSemaphoreDelete(mutex);
    }
  }

  SafeWrap(const SafeWrap&)            = delete;
  SafeWrap& operator=(const SafeWrap&) = delete;
  SafeWrap(SafeWrap&&)                 = delete;
  SafeWrap& operator=(SafeWrap&&)      = delete;

  template <typename Func>
  SAFEWRAP_NODISCARD bool withLock(Func&& func, TickType_t timeout) {
    ScopedTake lock(mutex, timeout);
    if (!lock) return false;
    std::forward<Func>(func)(value);
    return true;
  }

  template <typename Func>
  SAFEWRAP_NODISCARD bool withLock(Func&& func, TickType_t timeout) const {
    ScopedTake lock(mutex, timeout);
    if (!lock) return false;
    std::forward<Func>(func)(value);
    return true;
  }

  SAFEWRAP_NODISCARD bool get(T& out, TickType_t timeout) const {
    ScopedTake lock(mutex, timeout);
    if (!lock) return false;
    out = value;
    return true;
  }

  SAFEWRAP_NODISCARD bool set(const T& newValue, TickType_t timeout) {
    ScopedTake lock(mutex, timeout);
    if (!lock) return false;
    value = newValue;
    return true;
  }

  SAFEWRAP_NODISCARD bool set(T&& newValue, TickType_t timeout) {
    ScopedTake lock(mutex, timeout);
    if (!lock) return false;
    value = std::move(newValue);
    return true;
  }

  SAFEWRAP_NODISCARD LockGuard getLock(TickType_t timeout) { return LockGuard(this, timeout); }
};
