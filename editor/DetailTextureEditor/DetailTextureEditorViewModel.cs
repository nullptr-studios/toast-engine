using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using editor.Assets;
using editor.Engine;
using editor.Git;
using editor.Workspace;
using Proto.Events;

namespace editor.DetailTextureEditor;

public partial class DetailEntryVM : ObservableObject {
	private readonly Action m_changed;
	private bool m_loading = true;

	[ObservableProperty] private string m_name;
	[ObservableProperty] private string? m_albedoUid;
	[ObservableProperty] private string? m_roughnessUid;
	[ObservableProperty] private string? m_heightUid;
	[ObservableProperty] private bool m_isSelected;

	public DetailEntryVM(DetailTextureEntry entry, Action changed) {
		Id = entry.Id;
		m_name = entry.Name;
		m_albedoUid = entry.AlbedoUid.Length > 0 ? entry.AlbedoUid : null;
		m_roughnessUid = entry.RoughnessUid.Length > 0 ? entry.RoughnessUid : null;
		m_heightUid = entry.HeightUid.Length > 0 ? entry.HeightUid : null;
		m_changed = changed;
		m_loading = false;
	}

	public int Id { get; }

	public string Label => string.IsNullOrWhiteSpace(Name) ? $"{Id}:" : $"{Id}: {Name}";

	partial void OnNameChanged(string value) {
		OnPropertyChanged(nameof(Label));
		if (!m_loading) m_changed();
	}

	partial void OnAlbedoUidChanged(string? value) {
		if (!m_loading) m_changed();
	}

	partial void OnRoughnessUidChanged(string? value) {
		if (!m_loading) m_changed();
	}

	partial void OnHeightUidChanged(string? value) {
		if (!m_loading) m_changed();
	}

	public DetailTextureEntry ToEntry() => new() {
		Id = Id, Name = Name ?? "", AlbedoUid = AlbedoUid ?? "", RoughnessUid = RoughnessUid ?? "", HeightUid = HeightUid ?? ""
	};
}

public partial class DetailTextureEditorViewModel : ObservableObject, IDisposable {
	public const string PreviewPaletteUid = "DetailPrevA";
	public const string DrawBoxScript = "core://voxel_scripts/draw_box.lua";
	private const int PlaneVoxels = 96;

	private readonly DispatcherTimer m_previewTimer;
	private bool m_disposed;
	private bool m_sceneBuilt;
	private bool m_loading;
	private DetailTextureFile m_file = new();

	[ObservableProperty] private bool m_isDirty;
	[ObservableProperty] private DetailEntryVM? m_selectedItem;

	[ObservableProperty] private float m_albedo = 1f;
	[ObservableProperty] private float m_tintR = 1f;
	[ObservableProperty] private float m_tintG = 1f;
	[ObservableProperty] private float m_tintB = 1f;
	[ObservableProperty] private float m_roughness = 0.5f;
	[ObservableProperty] private float m_bump = 0.5f;

	public DetailTextureEditorViewModel(WorkspaceViewModel workspace, string uid, string virtualPath) {
		Workspace = workspace;
		Uid = uid;
		VirtualPath = virtualPath;

		Hierarchy = new HierarchyViewModel(() => Workspace) { Id = "DetailTextureHierarchy", Title = "Preview" };
		Hierarchy.HierarchyChanged += OnHierarchyChanged;

		Hierarchy.SelectedChanged += item => {
			if (item is not null) Hierarchy.SelectedNode = null;
		};

		m_previewTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(150) };
		m_previewTimer.Tick += (_, _) => {
			m_previewTimer.Stop();
			PushPreview();
		};

		Load();
	}

	public WorkspaceViewModel Workspace { get; }
	public HierarchyViewModel Hierarchy { get; }
	public string Uid { get; }
	public string VirtualPath { get; }

	public ObservableCollection<DetailEntryVM> Items { get; } = [];

	public string Title => $"Detail Texture - {Path.GetFileNameWithoutExtension(VirtualPath)}{(IsDirty ? " *" : "")}";

	public bool HasSelection => SelectedItem is not null;

	private bool OwnsEngine => ViewportFocus.DetachedOwner == Workspace.EffectiveHandle && !m_disposed;

	private string RealPath => ProjectContext.Resolve(VirtualPath);

	private void Load() {
		m_loading = true;
		try {
			m_file = DetailTextureFile.FromFile(RealPath);
		} catch (Exception e) {
			Log.Error($"Detail Texture Editor: failed to open '{VirtualPath}': {e.Message}");
			m_file = new DetailTextureFile();
		}

		Items.Clear();
		foreach (var entry in m_file.Entries) Items.Add(new DetailEntryVM(entry, OnEntryChanged));
		SelectedItem = Items.FirstOrDefault();
		IsDirty = false;
		m_loading = false;
	}

	private void OnEntryChanged() {
		MarkDirty();
	}

	private void MarkDirty() {
		if (m_loading) return;
		IsDirty = true;
		SchedulePreview();
	}

	partial void OnIsDirtyChanged(bool value) => OnPropertyChanged(nameof(Title));

	partial void OnSelectedItemChanged(DetailEntryVM? oldValue, DetailEntryVM? newValue) {
		if (oldValue is not null) oldValue.IsSelected = false;
		if (newValue is not null) newValue.IsSelected = true;
	}

	partial void OnSelectedItemChanged(DetailEntryVM? value) {
		OnPropertyChanged(nameof(HasSelection));
		SchedulePreview();
	}

	partial void OnAlbedoChanged(float value) => SchedulePreview();
	partial void OnTintRChanged(float value) => SchedulePreview();
	partial void OnTintGChanged(float value) => SchedulePreview();
	partial void OnTintBChanged(float value) => SchedulePreview();
	partial void OnRoughnessChanged(float value) => SchedulePreview();
	partial void OnBumpChanged(float value) => SchedulePreview();

	private void SchedulePreview() {
		if (m_loading || m_disposed) return;
		m_previewTimer.Stop();
		m_previewTimer.Start();
	}

	public void EnsureEngine() {
		if (!OwnsEngine && !m_disposed) TakeEngine();
	}

	public void TakeEngine() {
		if (m_disposed) return;
		ViewportFocus.DetachedOwner = Workspace.EffectiveHandle;
		Hierarchy.MakeCurrent();
		Events.Send(new SetActiveWorkspace { Handle = Workspace.EffectiveHandle });
		Events.Send(new RequestHierarchyUpdate());
		PushPreview();
	}

	private void OnHierarchyChanged() {
		if (m_sceneBuilt || m_disposed || !OwnsEngine) return;
		if (Workspace.RootUid is not { } root) return;
		m_sceneBuilt = true;

		var script = MetaFile.ReadHeader(ProjectContext.Resolve(DrawBoxScript))?.Uid ?? "";
		Events.Send(new VoxelCreatePiece {
			Parent = root,
			Type = "toast::FillVolume",
			Min = new VoxelInt3 { X = -PlaneVoxels / 2, Y = -PlaneVoxels / 2, Z = 0 },
			Max = new VoxelInt3 { X = (PlaneVoxels / 2) - 1, Y = (PlaneVoxels / 2) - 1, Z = 0 },
			Script = script,
			Mode = 0
		});
		Events.Send(new NodeChangeParam { Node = root, Parameter = "m_palette", Value = PreviewPaletteUid });
		Workspace.SetEditorCameraModeCommand.Execute("Orbit");
		PushPreview();
	}

	private void PushPreview() {
		if (!OwnsEngine) return;
		Events.Send(new SetDetailTexturePreview { Uid = Uid });
		Events.Send(new PreviewAssetToml { Uid = Uid, Toml = CurrentFile().Serialize() });
		Events.Send(new PreviewAssetToml { Uid = PreviewPaletteUid, Toml = BuildPaletteToml() });
	}

	private DetailTextureFile CurrentFile() {
		var file = new DetailTextureFile();
		file.Entries.AddRange(Items.Select(item => item.ToEntry()));
		return file;
	}

	private string BuildPaletteToml() {
		var id = SelectedItem?.Id ?? 0;
		static string Byte(float v) => ((int)Math.Round(Math.Clamp(v, 0f, 1f) * 255f)).ToString(CultureInfo.InvariantCulture);
		static string Unit(float v) => Math.Clamp(v, 0f, 1f).ToString("0.###", CultureInfo.InvariantCulture);

		return "max_emissive = 1.0\n\n[[entries]]\nindex = 1\nmaterial = 0\n"
		       + "albedo = [200, 200, 200]\n"
		       + "roughness = 0.5\nmetallic = 0\n"
		       + $"detail_id = {id}\n"
		       + $"detail_tint = [{Byte(TintR)}, {Byte(TintG)}, {Byte(TintB)}]\n"
		       + $"detail_scale = 4\ndetail_albedo = {Unit(Albedo)}\ndetail_roughness = {Unit(Roughness)}\ndetail_bump = {Unit(Bump)}\n";
	}

	[RelayCommand]
	private void AddTexture() {
		var id = m_file.NextFreeId();
		if (id == 0) {
			Log.Warn("Detail Texture Editor: all 255 ids are used");
			return;
		}

		var used = Items.Select(i => i.Id).ToHashSet();
		while (used.Contains(id)) id++;
		if (id > DetailTextureFile.MaxTextures) return;

		var item = new DetailEntryVM(new DetailTextureEntry { Id = id, Name = $"Texture {id}" }, OnEntryChanged);
		Items.Add(item);
		SelectedItem = item;
		MarkDirty();
	}

	[RelayCommand]
	private void DeleteSelected() {
		if (SelectedItem is not { } item) return;
		var index = Items.IndexOf(item);
		Items.Remove(item);
		SelectedItem = Items.Count == 0 ? null : Items[Math.Min(index, Items.Count - 1)];
		MarkDirty();
	}

	[RelayCommand]
	private void MoveUp() => Move(-1);

	[RelayCommand]
	private void MoveDown() => Move(1);

	private void Move(int delta) {
		if (SelectedItem is not { } item) return;
		var from = Items.IndexOf(item);
		var to = from + delta;
		if (to < 0 || to >= Items.Count) return;
		Items.Move(from, to);
		SelectedItem = item;
		MarkDirty();
	}

	[RelayCommand]
	private async Task Save() {
		var file = CurrentFile();
		if (await GitLockGuard.SaveAsync(Uid, VirtualPath, path => Task.Run(() => file.Save(path)), (_, _) => Load())) {
			IsDirty = false;
			m_file = file;
		}
	}

	public async Task<bool> ConfirmCloseAsync() {
		if (!IsDirty) return true;
		switch (await App.Modals.ShowSaveChanges(Path.GetFileName(VirtualPath))) {
			case Components.Modals.SaveChangesResult.Save:
				await Save();
				return !IsDirty;
			case Components.Modals.SaveChangesResult.DontSave:
				return true;
			default:
				return false;
		}
	}

	public void Dispose() {
		if (m_disposed) return;
		m_previewTimer.Stop();
		if (OwnsEngine) {
			try {
				Events.Send(new PreviewAssetToml { Uid = Uid, Toml = File.ReadAllText(RealPath) });
			} catch (Exception e) {
				Log.Warn($"Detail Texture Editor: could not restore '{VirtualPath}': {e.Message}");
			}
			Events.Send(new SetDetailTexturePreview { Uid = "" });
		}

		m_disposed = true;
		Hierarchy.HierarchyChanged -= OnHierarchyChanged;
		Hierarchy.Dispose();
		if (ViewportFocus.DetachedOwner == Workspace.EffectiveHandle) ViewportFocus.DetachedOwner = 0;
		Events.Send(new SetFocusedNode { Node = "" });
		Events.Send(new WorkspaceDestroy { Handle = Workspace.Handle });
		Workspace.Dispose();
		GC.SuppressFinalize(this);
	}
}
