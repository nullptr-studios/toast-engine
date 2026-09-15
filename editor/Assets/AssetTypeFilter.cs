using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Linq;
using System.Runtime.CompilerServices;
using Avalonia;
using Avalonia.Media;
using Avalonia.Styling;
using editor.Assets.Types;

namespace editor.Assets;

public class AssetTypeFilter : INotifyPropertyChanged {
	private bool m_isEnabled = true;
	private bool m_isExpanded;

	public AssetTypeFilter(BaseAsset? definition) {
		Definition = definition;
	}

	public BaseAsset? Definition { get; }
	public string Label => Definition?.DisplayName ?? "Unknown";
	public string ChipText => Definition?.ChipText ?? "?";

	public IBrush ChipColor {
		get {
			var key = Definition?.ChipColor;
			if (key is not null &&
			    Application.Current?.TryGetResource(key, ThemeVariant.Default, out var resource) == true &&
			    resource is IBrush brush)
				return brush;
			return Brushes.Gray;
		}
	}

	public bool IsExpanded {
		get => m_isExpanded;
		set {
			if (m_isExpanded == value) return;
			m_isExpanded = value;
			Notify();
		}
	}

	public bool IsEnabled {
		get => m_isEnabled;
		set {
			if (m_isEnabled == value) return;
			m_isEnabled = value;
			Notify();
		}
	}

	public event PropertyChangedEventHandler? PropertyChanged;

	private void Notify([CallerMemberName] string? name = null) {
		PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
	}
}

public class AssetTypeFilterGroup : INotifyPropertyChanged {
	private bool m_isExpanded;
	private bool m_isUpdating;

	public AssetTypeFilterGroup(string label, IEnumerable<AssetTypeFilter> filters) {
		Label = label;
		Filters = new ObservableCollection<AssetTypeFilter>(filters);
		foreach (var filter in Filters)
			filter.PropertyChanged += OnFilterPropertyChanged;
	}

	public string Label { get; }
	public ObservableCollection<AssetTypeFilter> Filters { get; }

	public bool IsExpanded {
		get => m_isExpanded;
		set {
			if (m_isExpanded == value) return;
			m_isExpanded = value;
			Notify();
		}
	}

	public bool? IsEnabled {
		get {
			var all = Filters.All(f => f.IsEnabled);
			var none = Filters.All(f => !f.IsEnabled);
			return all ? true : none ? false : null;
		}
		set {
			m_isUpdating = true;
			var enabled = value ?? IsEnabled != true;
			foreach (var filter in Filters)
				filter.IsEnabled = enabled;
			m_isUpdating = false;
			Notify();
		}
	}

	public event PropertyChangedEventHandler? PropertyChanged;

	private void OnFilterPropertyChanged(object? sender, PropertyChangedEventArgs e) {
		if (!m_isUpdating && e.PropertyName == nameof(AssetTypeFilter.IsEnabled))
			Notify(nameof(IsEnabled));
	}

	private void Notify([CallerMemberName] string? name = null) {
		PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
	}
}
