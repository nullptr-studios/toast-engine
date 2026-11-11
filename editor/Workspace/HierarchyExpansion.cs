using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using Avalonia.Threading;

namespace editor.Workspace;

internal sealed class HierarchyExpansion {
	private DispatcherTimer? m_timer;
	private Action? m_finish;
	public void Complete() {
		m_timer?.Stop();
		m_timer = null;
		var finish = m_finish;
		m_finish = null;
		finish?.Invoke();
	}

	public void Toggle<TRow>(TRow row, IList<TRow> rows, bool collapsing, Action toggle, HierarchyConnectorLayer connector) where TRow : IHierarchyRow {
		Complete();
		if (!collapsing) toggle();
		var index = rows.IndexOf(row);
		if (index < 0) return;
		var descendants = rows.Skip(index + 1).TakeWhile(child => child.Depth > row.Depth).ToArray();
		if (descendants.Length == 0) { if (collapsing) toggle(); return; }
		foreach (var child in descendants) child.VisualHeight = collapsing ? HierarchyConnectorLayer.RowHeight : 0;
		m_finish = () => {
			foreach (var child in descendants) child.VisualHeight = HierarchyConnectorLayer.RowHeight;
			if (collapsing) toggle();
			connector.RefreshGeometry();
		};
		var clock = Stopwatch.StartNew();
		m_timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(8) };
		m_timer.Tick += (_, _) => {
			var t = Math.Min(clock.Elapsed.TotalMilliseconds / 180, 1);
			var progress = 1 - Math.Pow(1 - t, 3);
			foreach (var child in descendants) child.VisualHeight = HierarchyConnectorLayer.RowHeight * (collapsing ? 1 - progress : progress);
			connector.RefreshGeometry();
			if (t >= 1) Complete();
		};
		connector.RefreshGeometry();
		m_timer.Start();
	}
}
