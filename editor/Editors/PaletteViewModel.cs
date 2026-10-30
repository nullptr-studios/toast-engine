//
// PaletteViewModel.cs
// 12 Sep 2026
//

using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Media;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Dock.Model.Mvvm.Controls;
using editor.Assets;
using editor.Git;
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
	[ObservableProperty] private bool m_showChecker;
	[NotifyPropertyChangedFor(nameof(HasSlotText))]
	[ObservableProperty] private string? m_slotText;
	[ObservableProperty] private bool m_lightBackground;
	public bool HasSlotText => SlotText is not null;
	[ObservableProperty] private string m_title = "";
	[ObservableProperty] private IBrush? m_albedoDot;
	[ObservableProperty] private string m_albedoText = "";
	[ObservableProperty] private IBrush? m_rmsDot;
	[ObservableProperty] private string m_rmsText = "";
	[ObservableProperty] private string m_emissiveText = "";
	[ObservableProperty] private string m_alphaText = "";

	public PaletteSwatchVM(Action<int> select, int index, IBrush brush, bool used) {
		m_select = select;
		Index = index;
		m_brush = brush;
		m_isUsed = used;
		m_title = $"{index}: empty";
	}

	public int Index { get; }

	[RelayCommand]
	private void Select() {
		m_select(Index);
	}
}

public partial class PaletteSlotVM : ObservableObject {
	private readonly Action<int> m_touch;
	private readonly VoxelMaterialSlot m_slot;
	private bool m_loading;

	[ObservableProperty] private string? m_destructionUid;
	[ObservableProperty] private string m_name;
	[ObservableProperty] private string? m_physicsUid;

	public PaletteSlotVM(int index, VoxelMaterialSlot slot, Action<int> touch) {
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

	public bool IsDefault => Index == 0;

	public string Label => IsDefault ? "0: Default"
		: string.IsNullOrWhiteSpace(Name) ? $"{Index}:"
		: $"{Index}: {Name}";

	partial void OnNameChanged(string value) {
		OnPropertyChanged(nameof(Label));
		if (m_loading) return;
		m_slot.Name = value ?? "";
		m_touch(Index);
	}

	partial void OnPhysicsUidChanged(string? value) {
		if (m_loading) return;
		m_slot.PhysicsUid = value ?? "";
		m_touch(Index);
	}

	partial void OnDestructionUidChanged(string? value) {
		if (m_loading) return;
		m_slot.DestructionUid = value ?? "";
		m_touch(Index);
	}
}

/// <summary>Assigns entry materials which a reimport of the source .vox cannot re-derive</summary>
public partial class PaletteViewModel : Tool, IToastZoneEditor, IAutosavable {
	private const string BaseTitle = "Palette Editor";
	private static readonly IBrush s_emptyBrush = Brushes.Transparent;

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
	[ObservableProperty] private float m_ior = VoxelPaletteEntry.DefaultIor;
	[ObservableProperty] private PaletteSlotVM? m_selectedSlot;
	[ObservableProperty] private int m_transformsTo;

	public PaletteViewModel() {
		for (var i = 1; i < VoxelPaletteFile.Size; i++)
			Swatches.Add(new PaletteSwatchVM(SelectIndex, i, s_emptyBrush, false));

		if (Design.IsDesignMode) InitDesignData();
	}

	public ObservableCollection<PaletteSwatchVM> Swatches { get; } = [];

	private readonly List<PaletteSlotVM> m_slots = [];
	private readonly List<PaletteSlotVM> m_slotOptions = [];
	[ObservableProperty] private IReadOnlyList<string> m_slotLabels = [];
	[ObservableProperty] private string? m_selectedSlotLabel;
	public PaletteSlotVM? EditableSlot => SelectedSlot is { IsDefault: false } slot ? slot : null;

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
		if (!IsDirty && HasContent) AutosaveService.Delete(CurrentUid, AssetTypeRegistry.GetExtension(CurrentPath));
		if (!IsDirty || !HasContent) return true;
		if (App.MainWindow is not { } owner) return true;
		var result = await new MessageModal(new ModalConfig(
			"Unsaved Changes",
			$"Save changes to '{FileName}'?",
			ModalButtons.OkNoCancel,
			OkLabel: "Save"
		)).ShowDialog<bool?>(owner);
		if (result is null) return false;
		if (result is true) {
			await Save();
			return !IsDirty;
		}

		AutosaveService.Delete(CurrentUid, AssetTypeRegistry.GetExtension(CurrentPath));
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
			swatch.ShowChecker = false;
			swatch.SlotText = null;
			swatch.Title = $"{swatch.Index}: empty";
		}

		ClearSlots();

		UpdateTitle();
		OnPropertyChanged(nameof(HasContent));
	}

	private void RebuildSlots() {
		ClearSlots();
		if (m_palette is not { } palette) return;

		for (var i = 0; i < VoxelPaletteFile.SlotCount; i++) {
			m_slots.Add(new PaletteSlotVM(i, palette.Slots[i], OnSlotEdited));
		}
	}

	private void ClearSlots() {
		m_slotOptions.Clear();
		m_slots.Clear();
		SlotLabels = [];
		SelectedSlot = null;
	}

	private static int SlotOf(VoxelPaletteEntry entry) {
		return VoxelPaletteTip.SlotOf(entry);
	}

	private void RefreshSlotOptions() {
		if (m_palette is not { } palette || m_slots.Count == 0) {
			m_slotOptions.Clear();
			SlotLabels = [];
			return;
		}

		var wanted = new bool[VoxelPaletteFile.SlotCount];
		wanted[0] = true;
		for (var i = 1; i < VoxelPaletteFile.SlotCount; i++)
			if (!palette.Slots[i].IsEmpty)
				wanted[i] = true;


		for (var i = 1; i < VoxelPaletteFile.Size; i++)
			if (palette.Entries[i].InUse)
				wanted[SlotOf(palette.Entries[i])] = true;

		if (Selected is { } selected) wanted[SlotOf(selected)] = true;

		var hasEmpty = false;
		for (var i = 1; i < VoxelPaletteFile.SlotCount; i++)
			if (wanted[i] && palette.Slots[i].IsEmpty) {
				hasEmpty = true;
				break;
			}

		if (!hasEmpty)
			for (var i = 1; i < VoxelPaletteFile.SlotCount; i++) {
				if (wanted[i]) continue;
				wanted[i] = true;
				break;
			}

		m_slotOptions.Clear();
		for (var i = 0; i < wanted.Length; i++)
			if (wanted[i])
				m_slotOptions.Add(m_slots[i]);

		SlotLabels = m_slotOptions.Select(slot => slot.Label).ToList();

		// rename changes the slot label
		if (SelectedSlot is { } current) SelectedSlotLabel = current.Label;
	}

	/// <summary>A slot edit dirties the palette but authors no entry - slots exist without any voxel using them</summary>
	private void OnSlotEdited(int slot) {
		if (m_loading || m_palette is null) return;
		IsDirty = true;

		// tooltip
		for (var i = 1; i < VoxelPaletteFile.Size; i++)
			if (m_palette.Entries[i].InUse && SlotOf(m_palette.Entries[i]) == slot)
				RefreshSwatch(i);

		RefreshSlotOptions();
	}

	private void RefreshSwatch(int index) {
		if (m_palette is not { } palette || index < 1 || index >= VoxelPaletteFile.Size) return;

		var entry = palette.Entries[index];
		var swatch = Swatches[index - 1];
		swatch.IsUsed = entry.InUse;
		swatch.ShowChecker = entry.InUse && entry.Alpha < 1.0f;
		swatch.Brush = entry.InUse
			? new SolidColorBrush(Color.FromRgb(entry.R, entry.G, entry.B), Math.Clamp(entry.Alpha, 0f, 1f))
			: s_emptyBrush;

		RefreshSlotBadge(swatch, entry);

		if (!entry.InUse) {
			swatch.Title = $"{index}: empty";
			return;
		}

		var tip = VoxelPaletteTip.Describe(index, palette);
		swatch.Title = tip.Title;
		swatch.AlbedoDot = new SolidColorBrush(tip.Albedo);
		swatch.AlbedoText = tip.AlbedoText;
		swatch.RmsDot = new SolidColorBrush(tip.Rms);
		swatch.RmsText = tip.RmsText;
		swatch.EmissiveText = tip.EmissiveText;
		swatch.AlphaText = tip.AlphaText;
	}

	private void RefreshSlotBadge(PaletteSwatchVM swatch, VoxelPaletteEntry entry) {
		var slot = SelectedSlot?.Index ?? 0;
		if (slot == 0 || !entry.InUse || SlotOf(entry) != slot) {
			swatch.SlotText = null;
			return;
		}

		const float backdrop = 40f;
		var opacity = Math.Clamp(entry.Alpha, 0f, 1f);
		var red = entry.R * opacity + backdrop * (1f - opacity);
		var green = entry.G * opacity + backdrop * (1f - opacity);
		var blue = entry.B * opacity + backdrop * (1f - opacity);

		swatch.LightBackground = (0.299f * red + 0.587f * green + 0.114f * blue) / 255f > 0.5f;
		swatch.SlotText = slot.ToString();
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
		Ior = entry.Ior;
		TransformsTo = entry.TransformsTo;
		LoadSelectedMaterial();

		OnPropertyChanged(nameof(EmissiveHint));
		m_loading = wasLoading;
	}

	/// <summary>An entry with no authored material still reads as slot 0, which is what the engine resolves it to</summary>
	private void LoadSelectedMaterial() {
		if (Selected is not { } entry) return;

		var wasLoading = m_loading;
		m_loading = true;
		RefreshSlotOptions();
		SelectedSlot = m_slots[SlotOf(entry)];
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
		if (entry.Reflectivity == 0f && Reflectivity == 0f) entry.Reflectivity = 0.04f;

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
		if (await GitLockGuard.SaveAsync(CurrentUid, CurrentPath, path => Task.Run(() => palette.Save(path)), ReopenFile))
			IsDirty = false;
	}

	private void ReopenFile(string uid, string virtualPath) {
		OpenFile(uid, virtualPath, GitLockGuard.DefinitionOf(virtualPath)!);
	}

	/// <summary>Unauthors the entry so the file drops it</summary>
	[RelayCommand]
	private void ClearEntry() {
		if (Selected is not { } entry) return;

		entry.Clear();

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

	partial void OnSelectedSlotLabelChanged(string? value) {
		if (value is null || SelectedSlot?.Label == value) return;
		if (m_slotOptions.Find(slot => slot.Label == value) is { } slot) SelectedSlot = slot;
	}

	partial void OnSelectedSlotChanged(PaletteSlotVM? value) {
		OnPropertyChanged(nameof(EditableSlot));
		SelectedSlotLabel = value?.Label;

		if (m_palette is { } palette)
			for (var i = 1; i < VoxelPaletteFile.Size; i++)
				RefreshSlotBadge(Swatches[i - 1], palette.Entries[i]);

		if (Ignore || value is null || Selected is not { } entry) return;

		// Picking a slot authors the material, so the entry stops being one the engine warns about
		entry.HasMaterial = true;
		entry.Material = value.Index;
		TouchEntry();
		RefreshSlotOptions();
	}

	partial void OnAlphaChanged(float value) {
		if (Ignore || Selected is not { } entry) return;
		entry.Alpha = Math.Clamp(value, 0f, 1f);
		TouchEntry();
	}

	partial void OnIorChanged(float value) {
		if (Ignore || Selected is not { } entry) return;
		entry.Ior = Math.Clamp(value, 1f, VoxelPaletteEntry.MaxIor);
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
