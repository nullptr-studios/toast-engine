using System;
using System.Collections.Concurrent;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;
using Google.Protobuf;

namespace editor.Engine;

/// <summary>P/Invoke bridge to the engine event bus.</summary>
public static partial class Events {
	// concurrent because callbacks come from the engine's native thread, not the UI thread
	private static readonly ConcurrentDictionary<ulong, Subscription> Subscribers = new();

	private static ulong m_nextId;

	public static unsafe int Send<T>(T message) where T : IMessage<T> {
		var data = message.ToByteArray();
		fixed (byte* ptr = data) {
			return events_send(message.Descriptor.FullName, ptr, (uint)data.Length);
		}
	}

	internal static uint CreateListener() {
		return events_listener_create();
	}

	internal static void DestroyListener(uint handle) {
		events_listener_destroy(handle);
	}

	internal static void ForgetSubscription(ulong id) {
		Subscribers.TryRemove(id, out _);
	}

	internal static unsafe ulong ListenerSubscribe<T>(uint handle, Func<T, bool> handler, sbyte priority)
		where T : IMessage<T>, new() {
		var d = new T().Descriptor;
		// register in Subscribers before calling into the engine so there's no window
		// where a callback fires and finds nothing to dispatch to
		var id = Interlocked.Increment(ref m_nextId);
		Subscribers[id] = new Subscription(d.Parser, msg => handler((T)msg));
		if (events_listener_subscribe(handle, d.FullName, &OnNativeEvent, (void*)id, priority) != 0) {
			Subscribers.TryRemove(id, out _);
			return 0;
		}

		return id;
	}

	internal static void ListenerUnsubscribe(uint handle, string name, ulong id) {
		events_listener_unsubscribe(handle, name);
		Subscribers.TryRemove(id, out _);
	}

	// called from native code -> the id is passed back as void* userData
	// we cast it to ulong and look up the managed handler in Subscribers
	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static unsafe int OnNativeEvent(byte* name, byte* data, uint size, void* userData) {
		try {
			var id = (ulong)userData;
			if (!Subscribers.TryGetValue(id, out var subscription)) return 0;

			var span = new ReadOnlySpan<byte>(data, (int)size);
			var message = subscription.Parser.ParseFrom(span);
			return subscription.Handler(message) ? 1 : 0;
		} catch {
			return 0;
		}
	}

	[LibraryImport("toast_engine")]
	private static partial uint events_listener_create();

	[LibraryImport("toast_engine")]
	private static partial void events_listener_destroy(uint handle);

	[LibraryImport("toast_engine", StringMarshalling = StringMarshalling.Utf8)]
	private static unsafe partial int events_listener_subscribe(
		uint handle, string name, delegate* unmanaged[Cdecl]<byte*, byte*, uint, void*, int> callback, void* user_data,
		sbyte priority);

	[LibraryImport("toast_engine", StringMarshalling = StringMarshalling.Utf8)]
	private static partial void events_listener_unsubscribe(uint handle, string name);

	[LibraryImport("toast_engine", StringMarshalling = StringMarshalling.Utf8)]
	private static unsafe partial int events_send(string name, byte* data, uint size);

	private sealed record Subscription(MessageParser Parser, Func<IMessage, bool> Handler);
}
