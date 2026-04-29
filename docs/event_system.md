# Event System

> Authors: Dante Harper
>
> Created on: 29/April/2026
>
> Updated on: 29/April/2026

## Core Concepts

### Event Definition

To create a new event type, define a `struct` that inherits from
`event::Event<T>`, where `T` is your event type. Event data must be defined in
a constructor

```cpp
// Example: Defining an event with data
struct MyCustomEvent : event::Event<MyCustomEvent> {
    int value;
    std::string message;

    MyCustomEvent(int v, std::string m) : value(v), message(std::move(m)) {}
};

// Example: A simple event without data
struct BasicEvent : event::Event<BasicEvent> {};
```

### Event Sending

Events are enqueued for dispatch using `event::send<T>(...)`. This function is thread-safe. Arguments provided are forwarded to your event type's constructor.

Events sent during an `event::pollEvents()` call are queued and dispatched during the *next* `event::pollEvents()` invocation.

```cpp
// Example: Sending events
event::send<BasicEvent>(); // Send an event without arguments
event::send<MyCustomEvent>(42, "Hello from event!"); // Send an event with arguments
event::send<MyCustomEvent>(MyCustomEvent{42, "Hello from event!"}); // Send an event with arguments
```

### Event Callbacks

Callbacks are function objects invoked when an event is dispatched. The `EventCallback` concept supports various signatures:

*   `bool(TEvent&)`: Takes event reference, returns `true` to consume.
*   `void(TEvent&)`: Takes event reference, does not consume.
*   `bool()`: No arguments, returns `true` to consume.
*   `void()`: No arguments, does not consume.

Returning `true` from a callback consumes the event, stopping further propagation to other callbacks.

Callbacks can also be assigned a priority. Callbacks with a higher `char priority` value will be executed before those with lower priority. The default priority is `0`.

### Subscribing / Unsubscribing Callbacks

The `event::Listener` class manages event subscriptions. When an
`event::Listener` instance is destroyed, it automatically unsubscribes all
registered callbacks. As well as enabling / disabling callbacks with `void enabled(bool state)`

You can subscribe callbacks using `Listener::subscribe` with the following signatures:

1.  **Unnamed Subscription:**
    `void Listener::subscribe<TEvent, F>(F&& callback, char priority = 0)`
    ```cpp
    event::Listener myListener;
    myListener.subscribe<BasicEvent>([]() { /* ... */ });
    ```
2.  **Named Subscription (label):**
    `void Listener::subscribe<TEvent, F>(std::string name, F&& callback, char priority = 0)`
    ```cpp
    myListener.subscribe<MyCustomEvent>("my_handler", [](MyCustomEvent& e) { /* ... */ });
    ```
    Callbacks can also be subscribed with a priority (see "Event Callbacks" section for details on priority).

Explicitly unsubscribe named callbacks using `Listener::unsubscribe<TEvent>(label)` with the following signature:

`void Listener::unsubscribe<TEvent>(std::string name)`
```cpp
myListener.unsubscribe<BasicEvent>("my_one_time_event");
```

*Note: Unnamed callbacks are internally assigned the label "unnamed".*


### Dispatching Events

`event::pollEvents()` dispatches all currently queued events to their
callbacks. This function is *not* thread-safe and must be called from a single,
dedicated thread (e.g., the main thread).

```cpp
// Example: Dispatching events in the main loop
void gameLoop() {
    // ... game logic ...
    event::pollEvents(); // Process all queued events
    // ... more game logic ...
}
```

## Motivation

## Performance

## Expansion

-   [ ] FFI
-   [ ] Event serialization/deserialization

## Changelog

-   Basic Features Complete
