using System;
using System.Collections.Generic;
using Avalonia.Threading;
using Google.Protobuf;

namespace editor.Engine;

/// <summary>Holds a set of event subscriptions. Dispose to unsubscribe all of them.</summary>
public sealed class Listener : IDisposable {
	private readonly uint m_handle;
	private readonly Dictionary<string, ulong> m_subs = new();
	private bool m_disposed;

	public Listener() {
		m_handle = Events.CreateListener();
	}

	public void Dispose() {
		if (m_disposed) return;
		m_disposed = true;
		Events.DestroyListener(m_handle);
		foreach (var id in m_subs.Values) Events.ForgetSubscription(id);
		m_subs.Clear();
		GC.SuppressFinalize(this);
	}

	~Listener() {
		Dispose();
	}

	public void Subscribe<T>(Func<T, bool> handler, sbyte priority = 0) where T : IMessage<T>, new() {
		var name = new T().Descriptor.FullName;
		if (m_subs.ContainsKey(name)) Unsubscribe<T>(); // replace existing sub for same type
		var id = Events.ListenerSubscribe(m_handle, handler, priority);
		if (id != 0) m_subs[name] = id;
	}

	// non-consuming version -> handler cant stop propagation (returns false automatically)
	public void Subscribe<T>(Action<T> handler, sbyte priority = 0) where T : IMessage<T>, new() {
		Subscribe<T>(e => {
			handler(e);
			return false;
		}, priority);
	}

	// posts to the UI thread before invoking the handler (engine callbacks come on the native thread)
	public void SubscribeOnUiThread<T>(Action<T> handler, sbyte priority = 0) where T : IMessage<T>, new() {
		Subscribe<T>(e => {
			Dispatcher.UIThread.Post(() => handler(e));
			return false;
		}, priority);
	}

	public void Unsubscribe<T>() where T : IMessage<T>, new() {
		var name = new T().Descriptor.FullName;
		if (m_subs.Remove(name, out var id)) Events.ListenerUnsubscribe(m_handle, name, id);
	}
}
