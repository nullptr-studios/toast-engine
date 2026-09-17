//
// LogsViewModel.cs by Xein
// 14 May 2026
//

using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Threading;
using System.Threading.Tasks;
using Avalonia.Input.Platform;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Dock.Model.Mvvm.Controls;
using editor.Components.Modals;
using Lucide.Avalonia;

namespace editor.Logger;

public partial class LogsViewModel : Tool {
	private const string SeverityKeyTrace = "trace";
	private const string SeverityKeyInfo = "info";
	private const string SeverityKeyWarning = "warning";
	private const string SeverityKeyError = "error";

	private const int SearchDebounceMs = 250;

	private const int MaxSeverityLabelLen = 5;
	private const int TimestampLabelLen = 15;
	private const double SeverityBarAndGapPx = 8; // 4px color bar + 4px spacing before the label
	private const double ColumnPaddingPx = 20;

	private readonly List<LogEntry> m_all = [];

	private readonly LogClient m_client;
	private readonly LogFilterState m_filterState = LogFilterState.Load();
	private readonly List<LogEntry> m_pendingBuffer = [];

	private readonly object m_pendingLock = new();

	private readonly SeverityFilterViewModel[] m_severityByBucket;
	private readonly long[] m_severityCounts = new long[4];
	private readonly bool[] m_severityEnabled = new bool[4];
	private readonly Dictionary<string, SinkFilterViewModel> m_sinksByName = new(StringComparer.Ordinal);

	[ObservableProperty] private bool m_autoScroll = true;
	[ObservableProperty] private double m_fileColumnWidth;
	private bool m_flushQueued;

	private double m_glyphAdvance = 8;
	private int m_maxFileLen = 8;
	private int m_maxSinkLen = 4;
	[ObservableProperty] private int m_rowCount;

	[ObservableProperty] private IReadOnlyList<ScrollMarker> m_scrollMarkers = [];
	private CancellationTokenSource? m_searchDebounceCts;
	[ObservableProperty] private string m_searchText = "";

	private string[] m_searchTokens = [];

	[ObservableProperty] private double m_severityColumnWidth;
	[ObservableProperty] private bool m_showTimestamps; // off by default
	[ObservableProperty] private double m_sinkColumnWidth;
	private bool m_started;
	private bool m_suppressSinkRebuild;
	[ObservableProperty] private double m_timestampColumnWidth;

	public LogsViewModel() {
		m_client = new LogClient();
		m_client.OnLogReceived += HandleNewLogs;

		var error = new SeverityFilterViewModel {
			Label = "ERROR", Severity = 3, Icon = LucideIconKind.OctagonX,
			IsEnabled = m_filterState.GetSeverity(SeverityKeyError, true)
		};
		var warning = new SeverityFilterViewModel {
			Label = "WARN", Severity = 2, Icon = LucideIconKind.TriangleAlert,
			IsEnabled = m_filterState.GetSeverity(SeverityKeyWarning, true)
		};
		var info = new SeverityFilterViewModel {
			Label = "INFO", Severity = 1, Icon = LucideIconKind.Info,
			IsEnabled = m_filterState.GetSeverity(SeverityKeyInfo, true)
		};
		var trace = new SeverityFilterViewModel {
			Label = "TRACE", Severity = 0, Icon = LucideIconKind.MessageSquare,
			IsEnabled = m_filterState.GetSeverity(SeverityKeyTrace, false) // default to off for trace
		};

		m_severityByBucket = [trace, info, warning, error];
		foreach (var s in m_severityByBucket) {
			m_severityEnabled[s.Severity] = s.IsEnabled;
			s.OnToggled = OnSeverityToggled;
			s.BumpCount(0);
		}

		Severities = [error, warning, info, trace];

		RecomputeColumnWidths();
	}

	public ReverseLogList Rows { get; } = new();
	public ObservableCollection<SeverityFilterViewModel> Severities { get; }
	public ObservableCollection<SinkFilterViewModel> Sinks { get; } = [];

	public event Action? ScrollToNewestRequested;

	public void Start() {
		if (m_started) return;
		m_started = true;
		m_client.start();
	}

	public void Stop() {
		if (!m_started) return;
		m_started = false;
		m_client.stop();
	}

	public void OnUserScrolled(bool isAtTop) {
		if (isAtTop) {
			if (!AutoScroll) AutoScroll = true;
		} else {
			if (AutoScroll) AutoScroll = false;
		}
	}

	public void SetGlyphAdvance(double advance) {
		if (advance <= 0) return;
		m_glyphAdvance = advance;
		RecomputeColumnWidths();
	}

	partial void OnAutoScrollChanged(bool value) {
		if (value) ScrollToNewestRequested?.Invoke();
	}

	partial void OnSearchTextChanged(string value) {
		m_searchTokens = value.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);

		m_searchDebounceCts?.Cancel();
		var cts = new CancellationTokenSource();
		m_searchDebounceCts = cts;
		_ = DebouncedRebuildAsync(cts.Token);
	}

	[RelayCommand]
	private void ClearLogs() {
		m_all.Clear();
		Array.Clear(m_severityCounts);
		foreach (var s in m_severityByBucket) s.BumpCount(0);
		Rows.ResetTo([]);
	}

	[RelayCommand]
	private void OpenEntry(LogEntry? entry) {
		if (entry is null) return;
		var window = new LogDetailWindow(entry);
		var owner = ModalService.FindActiveWindow();
		if (owner is not null) window.Show(owner);
		else window.Show();
	}

	[RelayCommand]
	private async Task CopyEntry(LogEntry? entry) {
		if (entry is null) return;
		await CopyToClipboardAsync(LogCsv.ToCsvLine(entry));
	}

	[RelayCommand]
	private async Task CopyEntryMessage(LogEntry? entry) {
		if (entry is null) return;
		await CopyToClipboardAsync(entry.Message);
	}

	[RelayCommand]
	private void ResetSinkFilters() {
		m_suppressSinkRebuild = true;
		try {
			foreach (var sink in m_sinksByName.Values)
			foreach (var s in sink.SeverityByBucket)
				if (!s.IsLocked)
					s.IsEnabled = true;
		} finally {
			m_suppressSinkRebuild = false;
		}

		m_filterState.ResetAllSinkSeverities();
		RebuildRows();
	}

	[RelayCommand]
	private void ToggleSinkQuick(SinkFilterViewModel? sink) {
		if (sink is null) return;
		var next = sink.QuickToggleState != true;

		m_suppressSinkRebuild = true;
		try {
			sink.SeverityByBucket[0].IsEnabled = next; // Trace
			sink.SeverityByBucket[1].IsEnabled = next; // Info
			sink.SeverityByBucket[2].IsEnabled = next; // Warning
		} finally {
			m_suppressSinkRebuild = false;
		}

		m_filterState.SetSinkSeverities(sink.Name, [
			(SeverityKeyFor(0), next),
			(SeverityKeyFor(1), next),
			(SeverityKeyFor(2), next)
		]);
		RebuildRows();
	}

	private static async Task CopyToClipboardAsync(string text) {
		if (ModalService.FindActiveWindow()?.Clipboard is { } clipboard)
			await clipboard.SetTextAsync(text);
	}

	private async Task DebouncedRebuildAsync(CancellationToken token) {
		try {
			await Task.Delay(SearchDebounceMs, token);
		} catch (TaskCanceledException) {
			return;
		}

		if (token.IsCancellationRequested) return;
		RebuildRows();
	}

	private void HandleNewLogs(List<LogEntry> newLogs) {
		bool needsPost;
		lock (m_pendingLock) {
			m_pendingBuffer.AddRange(newLogs);
			needsPost = !m_flushQueued;
			m_flushQueued = true;
		}

		if (needsPost) Dispatcher.UIThread.Post(Flush, DispatcherPriority.Background);
	}

	private void Flush() {
		List<LogEntry> drained;
		lock (m_pendingLock) {
			drained = [.. m_pendingBuffer];
			m_pendingBuffer.Clear();
		}

		if (drained.Count > 0) {
			m_all.AddRange(drained);
			UpdateColumnWidths(drained);

			foreach (var entry in drained) {
				var bucket = SeverityBucket(entry.Severity);
				m_severityByBucket[bucket].BumpCount(++m_severityCounts[bucket]);

				if (!m_sinksByName.TryGetValue(entry.Sink, out var sinkVm))
					sinkVm = AddSink(entry.Sink);
				sinkVm.BumpCount(bucket);
			}

			RebuildRows();
		}

		lock (m_pendingLock) {
			if (m_pendingBuffer.Count > 0) Dispatcher.UIThread.Post(Flush, DispatcherPriority.Background);
			else m_flushQueued = false;
		}
	}

	private void UpdateColumnWidths(IReadOnlyList<LogEntry> batch) {
		var changed = false;
		foreach (var e in batch) {
			if (e.Sink.Length > m_maxSinkLen) {
				m_maxSinkLen = e.Sink.Length;
				changed = true;
			}

			if (e.DisplayFileLine.Length > m_maxFileLen) {
				m_maxFileLen = e.DisplayFileLine.Length;
				changed = true;
			}
		}

		if (changed) RecomputeColumnWidths();
	}

	private void RecomputeColumnWidths() {
		SeverityColumnWidth = SeverityBarAndGapPx + MaxSeverityLabelLen * m_glyphAdvance + ColumnPaddingPx;
		SinkColumnWidth = m_maxSinkLen * m_glyphAdvance + ColumnPaddingPx;
		FileColumnWidth = m_maxFileLen * m_glyphAdvance + ColumnPaddingPx;
		TimestampColumnWidth = TimestampLabelLen * m_glyphAdvance + ColumnPaddingPx;
	}

	private SinkFilterViewModel AddSink(string sinkName) {
		SeverityFilterViewModel MakeSeverity(string label, uint severity, LucideIconKind icon, bool locked) {
			var key = SeverityKeyFor((int)severity);
			var enabled = locked || m_filterState.IsSinkSeverityEnabled(sinkName, key);
			var vm = new SeverityFilterViewModel {
				Label = label, Severity = severity, Icon = icon, IsLocked = locked, IsEnabled = enabled,
				MaxDisplayCount = 99,
				PadBadge = true
			};
			vm.BumpCount(0);
			return vm;
		}

		var error = MakeSeverity("ERROR", 3, LucideIconKind.OctagonX, true);
		var warning = MakeSeverity("WARN", 2, LucideIconKind.TriangleAlert, false);
		var info = MakeSeverity("INFO", 1, LucideIconKind.Info, false);
		var trace = MakeSeverity("TRACE", 0, LucideIconKind.MessageSquare, false);

		var sinkVm = new SinkFilterViewModel {
			Name = sinkName,
			SeverityByBucket = [trace, info, warning, error],
			Severities = [error, warning, info, trace]
		};

		foreach (var s in sinkVm.SeverityByBucket) s.OnToggled = severity => OnSinkSeverityToggled(sinkVm, severity);
		sinkVm.RecomputeQuickToggleState();

		m_sinksByName[sinkName] = sinkVm;

		var insertAt = 0;
		while (insertAt < Sinks.Count &&
		       string.Compare(Sinks[insertAt].Name, sinkName, StringComparison.OrdinalIgnoreCase) < 0)
			insertAt++;
		Sinks.Insert(insertAt, sinkVm);

		return sinkVm;
	}

	private void OnSeverityToggled(SeverityFilterViewModel severity) {
		m_severityEnabled[severity.Severity] = severity.IsEnabled;
		m_filterState.SetSeverity(SeverityKeyFor((int)severity.Severity), severity.IsEnabled);
		RebuildRows();
	}

	private void OnSinkSeverityToggled(SinkFilterViewModel sink, SeverityFilterViewModel severity) {
		sink.RecomputeQuickToggleState();
		if (m_suppressSinkRebuild) return;
		m_filterState.SetSinkSeverity(sink.Name, SeverityKeyFor((int)severity.Severity), severity.IsEnabled);
		RebuildRows();
	}

	private static string SeverityKeyFor(int bucket) {
		return bucket switch {
			3 => SeverityKeyError,
			2 => SeverityKeyWarning,
			1 => SeverityKeyInfo,
			_ => SeverityKeyTrace
		};
	}

	private void RebuildRows() {
		var filtered = new List<LogEntry>(m_all.Count);
		var warningIndices = new List<int>();
		var errorIndices = new List<int>();

		foreach (var e in m_all) {
			if (!Matches(e)) continue;

			var bucket = SeverityBucket(e.Severity);
			if (bucket == 3) errorIndices.Add(filtered.Count);
			else if (bucket == 2) warningIndices.Add(filtered.Count);
			filtered.Add(e);
		}

		Rows.ResetTo(filtered);
		RowCount = filtered.Count;
		ScrollMarkers = BuildScrollMarkers(filtered.Count, warningIndices, errorIndices);
		if (AutoScroll) ScrollToNewestRequested?.Invoke();
	}

	private static IReadOnlyList<ScrollMarker> BuildScrollMarkers(
		int total, List<int> warningIndices, List<int> errorIndices) {
		if (total == 0 || (warningIndices.Count == 0 && errorIndices.Count == 0)) return [];

		var markers = new List<ScrollMarker>(warningIndices.Count + errorIndices.Count);
		foreach (var i in warningIndices) markers.Add(new ScrollMarker(total - 1 - i, "Orange"));
		foreach (var i in errorIndices) markers.Add(new ScrollMarker(total - 1 - i, "Red"));
		return markers;
	}

	private bool Matches(LogEntry e) {
		var bucket = SeverityBucket(e.Severity);
		if (!m_severityEnabled[bucket]) return false;
		if (m_sinksByName.TryGetValue(e.Sink, out var sinkVm) && !sinkVm.IsSeverityEnabled(bucket)) return false;

		foreach (var token in m_searchTokens)
			if (!e.SearchBlob.Contains(token, StringComparison.OrdinalIgnoreCase))
				return false;

		return true;
	}

	private static int SeverityBucket(uint severity) {
		return severity <= 3 ? (int)severity : 3;
	}
}
