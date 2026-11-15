using System.Collections;
using System.Collections.Generic;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Media;
using CommunityToolkit.Mvvm.ComponentModel;
using editor.Assets.Importers;
using editor.Assets.Types;
using Lucide.Avalonia;

namespace editor.Components.Modals;

internal sealed record ReimportReviewViewModel(string Source, int AddedCount, int ModifiedCount,
	IReadOnlyList<ReimportMissingRow> Missing) {
	public int MissingCount => Missing.Count;
}

internal sealed partial class ReimportMissingRow(ReimportAsset asset) : ObservableObject {
	[ObservableProperty]
	[NotifyPropertyChangedFor(nameof(ShowKeep), nameof(ShowReplace), nameof(ShowDelete))]
	[NotifyPropertyChangedFor(nameof(KeepTip), nameof(ReplaceTip), nameof(DeleteTip))]
	[NotifyPropertyChangedFor(nameof(HasResolution), nameof(HasReplacement), nameof(ReplacementLabel))]
	private MissingAssetResolution? m_resolution;

	[ObservableProperty]
	[NotifyPropertyChangedFor(nameof(ReplaceTip))]
	private bool m_canReplace;

	public ReimportAsset Asset { get; } = asset;
	public string Label => Asset.Label;
	public bool HasResolution => Resolution is not null;
	public bool ShowKeep => Resolution is null or { Action: MissingAssetAction.Keep };
	public bool ShowReplace => Resolution is null or { Action: MissingAssetAction.Replace };
	public bool ShowDelete => Resolution is null or { Action: MissingAssetAction.Delete };
	public string KeepTip => Resolution is null ? "Keep asset" : "Undo keep";
	public string ReplaceTip => Resolution is not null ? "Undo replacement"
		: CanReplace ? "Choose replacement" : "No new assets of the same type are available";
	public string DeleteTip => Resolution is null ? "Delete asset" : "Undo delete";
	public bool HasReplacement => Resolution?.Replacement is not null;
	public string? ReplacementLabel => Resolution?.Replacement is { } replacement ? $"Replace with {replacement.Label}" : null;
}

public partial class ReimportReviewWindow : Window {
	private readonly IReadOnlyList<ReimportAsset> m_added = [];
	private readonly IReadOnlyList<ReimportMissingRow> m_rows = [];

	public ReimportReviewWindow() {
		InitializeComponent();
	}

	internal ReimportReviewWindow(string source, int modifiedCount,
		IReadOnlyList<ReimportAsset> added, IReadOnlyList<ReimportAsset> missing) : this() {
		m_added = added;
		m_rows = missing.Select(asset => new ReimportMissingRow(asset)).ToList();
		DataContext = new ReimportReviewViewModel(source, added.Count, modifiedCount, m_rows);
		UpdateActions();
	}

	private void OnKeep(object? sender, RoutedEventArgs e) => ToggleAction(sender, MissingAssetAction.Keep);
	private void OnDelete(object? sender, RoutedEventArgs e) => ToggleAction(sender, MissingAssetAction.Delete);

	private void ToggleAction(object? sender, MissingAssetAction action) {
		if (sender is not Button { DataContext: ReimportMissingRow row }) return;
		row.Resolution = row.Resolution is null ? new MissingAssetResolution(action) : null;
		UpdateActions();
	}

	private async void OnReplace(object? sender, RoutedEventArgs e) {
		if (sender is not Button { DataContext: ReimportMissingRow row }) return;
		if (row.Resolution is not null) {
			row.Resolution = null;
		} else {
			var available = AvailableReplacements(row).ToList();
			var picker = new ReplacementPicker(new ReplacementPickerViewModel(row.Asset, available));
			var uid = await picker.ShowDialog<string?>(this);
			if (available.FirstOrDefault(asset => asset.Uid == uid) is not { } replacement) return;
			row.Resolution = new MissingAssetResolution(MissingAssetAction.Replace, replacement);
		}
		UpdateActions();
	}

	private IEnumerable<ReimportAsset> AvailableReplacements(ReimportMissingRow row) =>
		m_added.Where(asset => asset.Type == row.Asset.Type &&
			m_rows.All(other => other.Resolution?.Replacement != asset));

	private void UpdateActions() {
		foreach (var row in m_rows)
			row.CanReplace = row.HasReplacement || AvailableReplacements(row).Any();
		ApplyButton.IsEnabled = m_rows.All(row => row.Resolution is not null);
	}

	private void OnCancel(object? sender, RoutedEventArgs e) => Close();

	private void OnApply(object? sender, RoutedEventArgs e) {
		if (ApplyButton.IsEnabled)
			Close(m_rows.ToDictionary(row => row.Asset.Uid, row => row.Resolution!));
	}

	private sealed class ReplacementPicker(ReplacementPickerViewModel vm) : PickerWindow(vm);

	private sealed class ReplacementPickerViewModel : PickerViewModel {
		private readonly List<AssetPickerItem> m_all;
		private IReadOnlyList<AssetPickerItem> m_filtered;

		public ReplacementPickerViewModel(ReimportAsset old, IReadOnlyList<ReimportAsset> replacements) {
			WindowTitle = $"Replace {old.Name}";
			var definition = AssetTypeRegistry.ByType(old.Type);
			var color = Application.Current!.TryGetResource(definition?.ChipColor ?? "Blue", null, out var value)
				? value as IBrush ?? Brushes.DodgerBlue : Brushes.DodgerBlue;
			m_all = replacements.Select(asset => new AssetPickerItem(asset.Uid, asset.Label, asset.Label, color,
				definition?.ChipText ?? old.Type, definition?.HasThumbnail == true, showPath: false)).ToList();
			m_filtered = m_all;
		}

		public override string WindowTitle { get; }
		public override string AcceptLabel => "Replace";
		public override LucideIconKind AcceptIconKind => LucideIconKind.Replace;
		public override IEnumerable Items => m_filtered;

		public override void UpdateFilter(string query, bool caseSensitive) =>
			m_filtered = m_all.Where(item => item.UpdateSegments(query, caseSensitive)).ToList();

		public override string? GetResult(object? selected) => (selected as AssetPickerItem)?.Uid;
	}
}
