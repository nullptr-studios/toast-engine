//
// LogFilterViewModels.cs by Xein
// 28 Jul 2026
//

using System;
using System.Collections.ObjectModel;
using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using Lucide.Avalonia;

namespace editor.Logger;

public partial class SeverityFilterViewModel : ObservableObject {
	[ObservableProperty] private string m_badge = "0";

	[ObservableProperty] private bool m_isEnabled = true;
	public required string Label { get; init; }
	public required uint Severity { get; init; }
	public required LucideIconKind Icon { get; init; }

	public bool IsLocked { get; init; }
	public int MaxDisplayCount { get; init; } = 999;
	public bool PadBadge { get; init; }

	public Action<SeverityFilterViewModel>? OnToggled { get; set; }

	partial void OnIsEnabledChanged(bool value) {
		if (IsLocked && !value) {
			IsEnabled = true; // reject
			return;
		}

		OnToggled?.Invoke(this);
	}

	public void BumpCount(long count) {
		var text = count > MaxDisplayCount ? $"{MaxDisplayCount}+" : count.ToString(CultureInfo.InvariantCulture);
		Badge = PadBadge ? PadCenter(text) : text;
	}

	// " 9 ", "99 ", "99+"
	private string PadCenter(string text) {
		var width = MaxDisplayCount.ToString(CultureInfo.InvariantCulture).Length + 1;
		var pad = width - text.Length;
		if (pad <= 0) return text;
		var left = pad / 2;
		return new string(' ', left) + text + new string(' ', pad - left);
	}
}

public partial class SinkFilterViewModel : ObservableObject {
	private readonly long[] m_counts = new long[4];

	[ObservableProperty] private bool? m_quickToggleState = true;

	public required string Name { get; init; }
	public required SeverityFilterViewModel[] SeverityByBucket { get; init; }
	public required ObservableCollection<SeverityFilterViewModel> Severities { get; init; }

	public bool IsSeverityEnabled(int bucket) {
		return SeverityByBucket[bucket].IsEnabled;
	}

	public void BumpCount(int bucket) {
		SeverityByBucket[bucket].BumpCount(++m_counts[bucket]);
	}

	public void RecomputeQuickToggleState() {
		var warning = SeverityByBucket[2].IsEnabled;
		var info = SeverityByBucket[1].IsEnabled;
		var trace = SeverityByBucket[0].IsEnabled;

		QuickToggleState = warning && info && trace ? true : !warning && !info && !trace ? false : null;
	}
}
