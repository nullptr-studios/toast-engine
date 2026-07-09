using System.ComponentModel;
using System.Runtime.CompilerServices;
using editor.Assets.Types;

namespace editor.Assets;

public class AssetTypeFilter : INotifyPropertyChanged {
	private bool m_isEnabled = true;

	public AssetTypeFilter(BaseAsset? definition) {
		Definition = definition;
	}

	public BaseAsset? Definition { get; }
	public string Label => Definition?.DisplayName ?? "Unknown";

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
