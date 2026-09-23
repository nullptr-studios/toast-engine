//
// PaletteViewModel.cs
// 12 Sep 2026
//

using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Media;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Dock.Model.Mvvm.Controls;
using editor.Assets;
using editor.Assets.Types;
using editor.Components.Modals;
using editor.Engine;
using editor.Workspace;

namespace editor.Editors;

/// <summary>One swatch of the 255-cell grid</summary>
public partial class PaletteSwatchVM : ObservableObject {
	private readonly Action<int> m_select;
	[ObservableProperty] private IBrush m_brush;
	[ObservableProperty] private bool m_isSelected;

	/// <summary>False for an index the palette does not author so it draws as a hole not black</summary>
	[ObservableProperty] private bool m_isUsed;

	public PaletteSwatchVM(Action<int> select, int index, IBrush brush, bool used) {
		m_select = select;
		Index = index;
		m_brush = brush;
		m_isUsed = used;
	}

	public int Index { get; }

	[RelayCommand]
	private void Select() {
		m_select(Index);
	}
}

public partial class PaletteSlotVM : ObservableObject {
	private readonly Action m_touch;
	private readonly VoxelMaterialSlot m_slot;
	private bool m_loading;

	[ObservableProperty] private string? m_destructionUid;
	[ObservableProperty] private string m_name;
	[ObservableProperty] private string? m_physicsUid;

	public PaletteSlotVM(int index, VoxelMaterialSlot slot, Action touch) {
		Index = index;
		m_slot = slot;
		m_touch = touch;

		m_loading = true;
		m_name = slot.Name;
		m_physicsUid = slot.PhysicsUid.Length > 0 ? slot.PhysicsUid : null;
		m_destructionUid = slot.DestructionUid.Length > 0 ? slot.DestructionUid : null;
		m_loading = false;
	}

	public int Index { get; }

	public string Label => string.IsNullOrWhiteSpace(Name) ? $"{Index}:" : $"{Index}: {Name}";

	partial void OnNameChanged(string value) {
		OnPropertyChanged(nameof(Label));
		if (m_loading) return;
		m_slot.Name = value ?? "";
		m_touch();
	}

	partial void OnPhysicsUidChanged(string? value) {
		if (m_loading) return;
		m_slot.PhysicsUid = value ?? "";
		m_touch();
	}

	partial void OnDestructionUidChanged(string? value) {
		if (m_loading) return;
		m_slot.DestructionUid = value ?? "";
		m_touch();
	}
}

/// <summary>Assigns entry materials which a reimport of the source .vox cannot re-derive</summary>
public partial class PaletteViewModel : Tool, IToastZoneEditor, IAutosavable {
	private const string BaseTitle = "Palette Editor";
	private static readonly IBrush s_emptyBrush = new SolidColorBrush(Color.FromArgb(40, 255, 255, 255));

	[ObservableProperty] private float m_albedoB;
	[ObservableProperty] private float m_albedoG;

	// 0-1 for Color3Box while the file stores bytes
	[ObservableProperty] private float m_albedoR;
	[ObservableProperty] private string m_currentPath = "";
	[ObservableProperty] private string m_currentUid = "";
	[ObservableProperty] private string m_displayTitle = BaseTitle;
	[ObservableProperty] private float m_emissive;
	[ObservableProperty] private bool m_entryInUse;
	[ObservableProperty] private string m_fileName = "";
	[ObservableProperty] private bool m_isDirty;
	private bool m_loading;
	[ObservableProperty] private float m_maxEmissive = 1f;
	[ObservableProperty] private float m_metallic;

	private VoxelPaletteFile? m_palette;
	[ObservableProperty] private float m_reflectivity;
	[ObservableProperty] private float m_roughness;
	[ObservableProperty] private int m_selectedIndex = 1;
	[ObservableProperty] private float m_alpha = 1f;
	[ObservableProperty] private int m_materialSlot;
	[ObservableProperty] private int m_transformsTo;
	[ObservableProperty] private bool m_transparent;

	public PaletteViewModel() {
		for (var i = 1; i < VoxelPaletteFile.Size; i++)
			Swatches.Add(new PaletteSwatchVM(SelectIndex, i, s_emptyBrush, false));

		if (Design.IsDesignMode) InitDesignData();
	}

	public ObservableCollection<PaletteSwatchVM> Swatches { get; } = [];

	/// <summary>The palette's eight material slots, rebuilt whenever a file is opened</summary>
	public ObservableCollection<PaletteSlotVM> Slots { get; } = [];

	public bool HasContent => m_palette is not null;

	public string SelectedLabel => $"Index {SelectedIndex}";

	/// <summary>Emissive scaled by max_emissive</summary>
	public string EmissiveHint => Emissive > 0f
		? $"{Emissive * MaxEmissive:0.##} intensity"
		: "not emissive";

	private bool Ignore => m_loading || m_palette is null;

	private VoxelPaletteEntry? Selected =>
		m_palette is { } palette && SelectedIndex is > 0 and < VoxelPaletteFile.Size
			? palette.Entries[SelectedIndex]
			: null;

	public bool IsAutosaveDirty => IsDirty && HasContent;

	public string? AutosaveFileName =>
		HasContent && !string.IsNullOrEmpty(CurrentPath)
			? CurrentUid + AssetTypeRegistry.GetExtension(CurrentPath)
			: null;

	public Task WriteAutosaveAsync(string virtualPath) {
		if (m_palette is not { } palette) return Task.CompletedTask;
		var realPath = ProjectContext.Resolve(virtualPath);
		return Task.Run(() => palette.Save(realPath));
	}

	public void OpenFile(string uid, string virtualPath, BaseAsset definition, string? contentSourceRealPath = null) {
		m_loading = true;
		var recovered = contentSourceRealPath != null;
		try {
			var palette = VoxelPaletteFile.FromFile(contentSourceRealPath ?? ProjectContext.Resolve(virtualPath));
			CurrentUid = uid;
			CurrentPath = virtualPath;
			LoadPalette(palette, virtualPath);
		} catch (Exception e) {
			Log.Error($"Palette Editor: failed to open '{virtualPath}': {e.Message}");
			CloseCurrent();
			recovered = false;
		} finally {
			IsDirty = recovered;
			m_loading = false;
		}
	}

	public async Task<bool> ConfirmCloseCurrentAsync() {
		if (!IsDirty || !HasContent) return true;
		if (App.MainWindow is not { } owner) return true;
		var result = await new MessageModal(new ModalConfig(
			"Unsaved Changes",
			$"Save changes to '{FileName}'?",
			ModalButtons.OkNoCancel,
			OkLabel: "Save"
		)).ShowDialog<bool?>(owner);
		if (result is null) return false;
		if (result is true) await Save();
		else AutosaveService.Delete(CurrentUid, AssetTypeRegistry.GetExtension(CurrentPath));
		return true;
	}

	private void InitDesignData() {
		var palette = new VoxelPaletteFile { MaxEmissive = 4f };
		for (var i = 1; i < 40; i++) {
			var entry = palette.Entries[i];
			entry.InUse = true;
			entry.R = (byte)(i * 6);
			entry.G = (byte)(255 - i * 5);
			entry.B = 90;
			entry.Roughness = 0.6f;
		}
		LoadPalette(palette, "sample.tpal");
	}

	private void LoadPalette(VoxelPaletteFile palette, string virtualPath) {
		m_loading = true;

		m_palette = palette;
		FileName = Path.GetFileName(virtualPath);
		MaxEmissive = palette.MaxEmissive;

		RebuildSlots();
		for (var i = 1; i < VoxelPaletteFile.Size; i++) RefreshSwatch(i);

		SelectedIndex = FirstUsedIndex();
		LoadSelectedEntry();
		UpdateTitle();
		OnPropertyChanged(nameof(HasContent));

		m_loading = false;
	}

	private int FirstUsedIndex() {
		if (m_palette is not { } palette) return 1;
		for (var i = 1; i < VoxelPaletteFile.Size; i++)
			if (palette.Entries[i].InUse)
				return i;
		return 1;
	}

	private void CloseCurrent() {
		m_palette = null;
		CurrentUid = "";
		CurrentPath = "";
		FileName = "";
		foreach (var swatch in Swatches) {
			swatch.Brush = s_emptyBrush;
			swatch.IsUsed = false;
		}

		UpdateTitle();
		OnPropertyChanged(nameof(HasContent));
	}

	private void RebuildSlots() {
		Slots.Clear();
		if (m_palette is not { } palette) return;

		for (var i = 0; i < VoxelPaletteFile.SlotCount; i++) {
			Slots.Add(new PaletteSlotVM(i, palette.Slots[i], MarkSlotDirty));
		}
	}

	/// <summary>A slot edit dirties the palette but authors no entry - slots exist without any voxel using them</summary>
	private void MarkSlotDirty() {
		if (m_loading || m_palette is null) return;
		IsDirty = true;
	}

	private void RefreshSwatch(int index) {
		if (m_palette is not { } palette || index < 1 || index >= VoxelPaletteFile.Size) return;

		var entry = palette.Entries[index];
		var swatch = Swatches[index - 1];
		swatch.IsUsed = entry.InUse;
		swatch.Brush = entry.InUse
			? new SolidColorBrush(Color.FromRgb(entry.R, entry.G, entry.B))
			: s_emptyBrush;
	}

	private void SelectIndex(int index) {
		SelectedIndex = index;
	}

	private void LoadSelectedEntry() {
		if (Selected is not { } entry) return;

		var wasLoading = m_loading;
		m_loading = true;

		EntryInUse = entry.InUse;
		AlbedoR = entry.R / 255f;
		AlbedoG = entry.G / 255f;
		AlbedoB = entry.B / 255f;
		Roughness = entry.Roughness;
		Metallic = entry.Metallic;
		Reflectivity = entry.Reflectivity;
		Emissive = entry.Emissive;
		Alpha = entry.Alpha;
		TransformsTo = entry.TransformsTo;
		Transparent = entry.Transparent;
		LoadSelectedMaterial();

		OnPropertyChanged(nameof(EmissiveHint));
		m_loading = wasLoading;
	}

	/// <summary>An entry with no authored material still reads as slot 0, which is what the engine resolves it to</summary>
	private void LoadSelectedMaterial() {
		if (Selected is not { } entry) return;

		var wasLoading = m_loading;
		m_loading = true;
		MaterialSlot = entry.HasMaterial ? Math.Clamp(entry.Material, 0, VoxelPaletteFile.SlotCount - 1) : 0;
		m_loading = wasLoading;
	}

	/// <summary>Any edit authors the entry since unauthored entries are dropped on save</summary>
	private void TouchEntry() {
		if (Selected is not { } entry) return;
		if (!entry.InUse) {
			entry.InUse = true;
			EntryInUse = true;
			SeedNewEntryDefaults(entry);
		}

		RefreshSwatch(SelectedIndex);
		IsDirty = true;
	}

	/// <summary>A newly authored entry starts from a plausible dielectric rather than a black mirror</summary>
	/// <remarks>
	/// Only the fields the user has not touched yet: whichever property triggered TouchEntry() has already
	/// written its own value, and the reload below leaves it alone
	/// </remarks>
	private void SeedNewEntryDefaults(VoxelPaletteEntry entry) {
		if (entry.Roughness == 0f && Roughness == 0f) entry.Roughness = 0.5f;
		if (entry.Metallic == 0f && Metallic == 0f) entry.Metallic = 0.1f;
		if (entry.Reflectivity == 0f && Reflectivity == 0f) entry.Reflectivity = 0.5f;

		var wasLoading = m_loading;
		m_loading = true;
		Roughness = entry.Roughness;
		Metallic = entry.Metallic;
		Reflectivity = entry.Reflectivity;
		m_loading = wasLoading;
	}

	[RelayCommand]
	private async Task Save() {
		if (m_palette is not { } palette || string.IsNullOrEmpty(CurrentPath)) return;
		var realPath = ProjectContext.Resolve(CurrentPath);
		await Task.Run(() => palette.Save(realPath));
		MetaFile.Touch(CurrentPath);
		AutosaveService.Delete(CurrentUid, AssetTypeRegistry.GetExtension(CurrentPath));
		IsDirty = false;
	}

	/// <summary>Unauthors the entry so the file drops it</summary>
	[RelayCommand]
	private void ClearEntry() {
		if (Selected is not { } entry) return;

		entry.InUse = false;
		entry.HasMaterial = false;
		entry.Material = 0;
		entry.R = entry.G = entry.B = 0;
		entry.Roughness = entry.Metallic = entry.Reflectivity = entry.Emissive = 0f;
		entry.Alpha = 1f;
		entry.TransformsTo = 0;
		entry.Transparent = false;

		RefreshSwatch(SelectedIndex);
		LoadSelectedEntry();
		IsDirty = true;
	}

	partial void OnSelectedIndexChanged(int value) {
		for (var i = 0; i < Swatches.Count; i++) Swatches[i].IsSelected = Swatches[i].Index == value;
		OnPropertyChanged(nameof(SelectedLabel));
		LoadSelectedEntry();
	}

	partial void OnMaxEmissiveChanged(float value) {
		OnPropertyChanged(nameof(EmissiveHint));
		if (Ignore) return;
		m_palette!.MaxEmissive = Math.Max(value, 0f);
		IsDirty = true;
	}

	partial void OnAlbedoRChanged(float value) {
		if (Ignore || Selected is not { } entry) return;
		entry.R = ToByte(value);
		TouchEntry();
	}

	partial void OnAlbedoGChanged(float value) {
		if (Ignore || Selected is not { } entry) return;
		entry.G = ToByte(value);
		TouchEntry();
	}

	partial void OnAlbedoBChanged(float value) {
		if (Ignore || Selected is not { } entry) return;
		entry.B = ToByte(value);
		TouchEntry();
	}

	partial void OnRoughnessChanged(float value) {
		if (Ignore || Selected is not { } entry) return;
		entry.Roughness = Math.Clamp(value, 0f, 1f);
		TouchEntry();
	}

	partial void OnMetallicChanged(float value) {
		if (Ignore || Selected is not { } entry) return;
		entry.Metallic = Math.Clamp(value, 0f, 1f);
		TouchEntry();
	}

	partial void OnReflectivityChanged(float value) {
		if (Ignore || Selected is not { } entry) return;
		entry.Reflectivity = Math.Clamp(value, 0f, 1f);
		TouchEntry();
	}

	partial void OnEmissiveChanged(float value) {
		OnPropertyChanged(nameof(EmissiveHint));
		if (Ignore || Selected is not { } entry) return;
		entry.Emissive = Math.Clamp(value, 0f, 1f);
		TouchEntry();
	}

	partial void OnTransformsToChanged(int value) {
		if (Ignore || Selected is not { } entry) return;
		entry.TransformsTo = Math.Clamp(value, 0, 255);
		TouchEntry();
	}

	partial void OnTransparentChanged(bool value) {
		if (Ignore || Selected is not { } entry) return;
		entry.Transparent = value;
		TouchEntry();
	}

	partial void OnMaterialSlotChanged(int value) {
		if (Ignore || Selected is not { } entry) return;

		// Picking a slot authors the material, so the entry stops being one the engine warns about
		entry.HasMaterial = true;
		entry.Material = Math.Clamp(value, 0, VoxelPaletteFile.SlotCount - 1);
		TouchEntry();
	}

	partial void OnAlphaChanged(float value) {
		if (Ignore || Selected is not { } entry) return;
		entry.Alpha = Math.Clamp(value, 0f, 1f);
		TouchEntry();
	}

	partial void OnIsDirtyChanged(bool value) {
		UpdateTitle();
	}

	private void UpdateTitle() {
		Title = IsDirty ? BaseTitle + " *" : BaseTitle;
		DisplayTitle = string.IsNullOrEmpty(FileName) ? BaseTitle
			: IsDirty ? $"{FileName} *"
			: FileName;
	}

	private static byte ToByte(float unit) {
		return (byte)Math.Clamp(MathF.Round(unit * 255f), 0f, 255f);
	}
}
