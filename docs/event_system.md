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
event::send<MyCustomEvent>(MyCustomEvent{42, "Hello from event!"}); // Send a pre-constructed event
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

## Implementation


### Event Definition

As Explained above you must define an Event Type like this `struct MyEvent : Event<MyEvent> {}`

#### IEvent

IEvent struct was made so that Event<T> would inherit and get a vtable for the
`notify` function

*Note: In Debug this struct also holds a std::stackstrace for debugging support

#### Event<T>

```cpp
	using callback_t = std::move_only_function<bool(T&)>;
	using iterator_t = std::multimap<char, callback_t*>::iterator;
```

##### Storing Callbacks

In each event type created with Event<T> it creates a multimap `callbacks` list
that maps every callback added to a priority (char)

```cpp
    std::multimap<char, callback_t*, std::greater<char>> callbacks;
```

#### Subscribing / Unsubscribing Callbacks

When subscribing an callback we return a `iterator_t` of the inserted callback.
This is because with a multimap we can assure that the iterator is not going to
be invalidated

We can use the returned `iterator_t` to then unsubscribe the callback when we are ready

```cpp
	static auto subscribe(char priority, callback_t&& callback) noexcept -> iterator_t;
	static void unsubscribe(iterator_t it) noexcept;
```

### Listener

```cpp
	struct Handle {
		std::type_index type;
		std::string name;
		std::any iterator;
	};

    std::vector<Handle> callbacks;
    bool enabled;

	template<typename TEvent, EventCallback<TEvent&> F>
	void subscribe(std::string name, F&& callback, char priority = 0) noexcept;

	template<typename TEvent>
	void unsubscribe(std::string name) noexcept;

	template<typename TEvent, EventCallback<TEvent&> F>
	void subscribe(F&& callback, char priority = 0) noexcept;
```

when we subscribe a function i use constexpr programming to convert the
callback `F&&` to a `(TEvent&)->bool` callback and we add a check inside the
callback to make sure the listener is enabled so that we can disable the
callbacks if needed and then we store the iterator_t alongside a name and the a
type_id of the event type

when we unsubscribe a function we can now search for every callback with the
same name and type that was given and find and remove it properly from the list
and in order to remove it properly from the callback list in Event<T> i made a
sudo v-table to abstract the removal of callbacks

```cpp
    /// @brief vtable for unsubscribing callbacks
    inline std::unordered_map<std::type_index, std::function<void(std::any)>> unsubscribe_map;
```

### Sending Events

because of the fact that all events will be added and removed if FIFO order we
can actually store all the events in 2 circular buffers (a type of memory pool
search it up) and swap them every time we need to dispatch them

one circular buffer for adding new events and another for polling events in
case we need to push new events on another thread while polling

### Dispatching Events

when we call `void pollEvents()` we first delete the memory of all the
callbacks added to the `_detail::deletion_queue` and swap the 2 circular buffers

then we iterator through each event and call its `notify()` function

then we clear the pollEvents memory pool

#### `notify()`

the `notify()` function first checks if any callbacks were added / removed from
the callback list and if so we clear the cached list of callbacks and
repopulate them with the new list. This allows us to modify the `Master`
callback list during polling as well as increase cache locality a little bit.

then we iterator through the list of callbacks and if one returns true we
return and dont propogate further

## Performance

## Expansion

-   [ ] FFI
-   [ ] Event serialization/deserialization

## Changelog

-   Basic Features Complete
