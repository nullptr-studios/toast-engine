//
// AssetBrowserSettingsViewModel.cs
// 24 Sep 2026
//

using System;
using System.Collections.ObjectModel;
using System.Linq;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Media;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using editor.Assets;
using editor.Components.Elements;
using editor.Components.Modals;
using Lucide.Avalonia;

namespace editor.Workspace;

public sealed partial class AssetBrowserSettingsViewModel : ObservableObject {
	public AssetBrowserSettingsViewModel() {
		AssetBrowserSettings.Changed += () => {
			OnPropertyChanged(string.Empty);
			foreach (var command in new IRelayCommand[] { MoveUpCommand, MoveDownCommand })
				command.NotifyCanExecuteChanged();
		};
	}

	public ObservableCollection<AssetTag> Tags => AssetBrowserSettings.Tags;
	public bool HasTags => Tags.Count > 0;

	public string[] CardSizes { get; } = Enum.GetNames<AssetCardSize>();
	public string[] SortOptions { get; } = Enum.GetNames<AssetSortBy>();
	public string[] SearchScopes { get; } = ["Everywhere", "Current folder"];

	public bool ShowTags {
		get => AssetBrowserSettings.ShowTags;
		set => AssetBrowserSettings.SetShowTags(value);
	}

	public bool ShowTypeBadge {
		get => AssetBrowserSettings.ShowTypeBadge;
		set => AssetBrowserSettings.SetShowTypeBadge(value);
	}

	public int CardSizeIndex {
		get => (int)AssetBrowserSettings.CardSize;
		set => AssetBrowserSettings.SetCardSize((AssetCardSize)Math.Max(0, value));
	}

	public bool ShowCore {
		get => AssetBrowserSettings.ShowCore;
		set => AssetBrowserSettings.SetShowCore(value);
	}

	public bool ShowCache {
		get => AssetBrowserSettings.ShowCache;
		set => AssetBrowserSettings.SetShowCache(value);
	}

	public int SortByIndex {
		get => (int)AssetBrowserSettings.SortBy;
		set => AssetBrowserSettings.SetSortBy((AssetSortBy)Math.Max(0, value));
	}

	public bool FoldersFirst {
		get => AssetBrowserSettings.FoldersFirst;
		set => AssetBrowserSettings.SetFoldersFirst(value);
	}

	public int SearchScopeIndex {
		get => (int)AssetBrowserSettings.SearchScope;
		set => AssetBrowserSettings.SetSearchScope((AssetSearchScope)Math.Max(0, value));
	}

	public bool ConfirmDelete {
		get => AssetBrowserSettings.ConfirmDelete;
		set => AssetBrowserSettings.SetConfirmDelete(value);
	}

	public string HiddenPatterns {
		get => string.Join(", ", AssetBrowserSettings.HiddenPatterns);
		set => AssetBrowserSettings.SetHiddenPatterns(
			(value ?? "").Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries));
	}

	[RelayCommand]
	private void AddTag() {
		var hue = EditorColorPalette.Hues[Tags.Count % EditorColorPalette.Hues.Length];
		var name = "New Tag";
		for (var i = 2; AssetBrowserSettings.ByName(name) is not null; i++) name = $"New Tag {i}";
		AssetBrowserSettings.AddTag(name, EditorColorPalette.Resolve(hue));
	}

	[RelayCommand(CanExecute = nameof(CanMoveUp))]
	private void MoveUp(AssetTag? tag) {
		if (tag is not null) AssetBrowserSettings.MoveTag(tag, -1);
	}

	private bool CanMoveUp(AssetTag? tag) {
		return tag is not null && Tags.IndexOf(tag) > 0;
	}

	[RelayCommand(CanExecute = nameof(CanMoveDown))]
	private void MoveDown(AssetTag? tag) {
		if (tag is not null) AssetBrowserSettings.MoveTag(tag, 1);
	}

	private bool CanMoveDown(AssetTag? tag) {
		return tag is not null && Tags.IndexOf(tag) is var i && i >= 0 && i < Tags.Count - 1;
	}

	[RelayCommand]
	private async Task RemoveTag(AssetTag? tag) {
		if (tag is null) return;

		var count = await Task.Run(() => AssetBrowserSettings.FindTaggedMetas(tag.Id).Count);
		var usage = count switch {
			0 => "No assets use it.",
			1 => "It will be removed from 1 asset.",
			_ => $"It will be removed from {count} assets."
		};

		if (ActiveWindow() is not { } window) return;
		var confirmed = await new MessageModal(new ModalConfig(
			"Remove Tag", $"Remove the tag \"{tag.Name}\"? {usage}",
			ModalButtons.OkCancel,
			LucideIconKind.Tag,
			new SolidColorBrush(Color.Parse("#d04040")),
			"Remove",
			OkIcon: LucideIconKind.X
		)).ShowDialog<bool?>(window) == true;
		if (!confirmed) return;

		AssetBrowserSettings.DeleteTag(tag);
	}

	private static Window? ActiveWindow() {
		if (Application.Current?.ApplicationLifetime is IClassicDesktopStyleApplicationLifetime d)
			return d.Windows.FirstOrDefault(w => w.IsActive) ?? d.Windows.FirstOrDefault();
		return null;
	}
}
