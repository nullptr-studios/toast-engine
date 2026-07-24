//
// TableViewModel.cs by Xein
// 18 Jul 2026
//

using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Avalonia.Controls;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Dock.Model.Mvvm.Controls;
using editor.Assets;
using editor.Assets.Types;
using editor.Components.Modals;
using editor.Engine;
using editor.Workspace;

namespace editor.Editors;

public partial class TableViewModel : Tool, IToastZoneEditor, IAutosavable {
	private const string BaseTitle = "Table Editor";

	[ObservableProperty] private string m_currentPath = "";
	[ObservableProperty] private string m_currentUid = "";
	[ObservableProperty] private string m_displayTitle = BaseTitle;
	[ObservableProperty] private string m_fileName = "";
	[ObservableProperty] private bool m_isDirty;
	[ObservableProperty] private bool m_hasContent;
	[ObservableProperty] private bool m_usesAssetCells;

	private bool m_loading;
	private readonly List<string> m_storageHeaders = [];

	public TableViewModel() {
		ProjectContext.LanguagesChanged += OnLanguagesChanged;
		if (Design.IsDesignMode) InitDesignData();
	}

	public ObservableCollection<TableColumnVM> Columns { get; } = [];
	public ObservableCollection<TableRowVM> Rows { get; } = [];

	public bool IsAutosaveDirty => IsDirty && HasContent;

	public string? AutosaveFileName =>
		HasContent && !string.IsNullOrEmpty(CurrentPath)
			? CurrentUid + AssetTypeRegistry.GetExtension(CurrentPath)
			: null;

	public Task WriteAutosaveAsync(string virtualPath) {
		var csv = Serialize();
		var realPath = ProjectContext.Resolve(virtualPath);
		return Task.Run(() => File.WriteAllText(realPath, csv));
	}

	public void OpenFile(string uid, string virtualPath, BaseAsset definition, string? contentSourceRealPath = null) {
		m_loading = true;
		var recovered = contentSourceRealPath != null;
		try {
			var realPath = ProjectContext.Resolve(virtualPath);
			if (File.Exists(realPath)) ReconcileFileOnDisk(realPath);
			var text = File.ReadAllText(contentSourceRealPath ?? realPath);
			text = ReconcileCsvForLanguages(text, ProjectContext.Languages, out _);
			CurrentUid = uid;
			CurrentPath = virtualPath;
			UsesAssetCells = definition.Type == "image_localization";
			LoadTable(text, virtualPath);
		} catch (Exception e) {
			Log.Error($"Table Editor: failed to open '{virtualPath}': {e.Message}");
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
		LoadTable("id,en,es\nhud_health,Health,Salud\nhud_ammo,Ammo,Munición\n", "sample.tloc");
	}

	private void LoadTable(string csv, string virtualPath) {
		m_loading = true;

		var table = ParseCsv(csv);
		Columns.Clear();
		Rows.Clear();
		m_storageHeaders.Clear();

		if (table.Count > 0 && table[0].Count > 0) {
			m_storageHeaders.AddRange(table[0]);
		} else {
			m_storageHeaders.Add("id");
			m_storageHeaders.AddRange(ProjectContext.Languages);
		}
		RebuildColumns();

		for (var r = 1; r < table.Count; r++)
			Rows.Add(MakeRow(table[r]));

		FileName = Path.GetFileName(virtualPath);
		HasContent = true;
		UpdateTitle();

		m_loading = false;
	}

	private TableRowVM MakeRow(IReadOnlyList<string> cells) {
		var row = new TableRowVM(OnCellEdited);
		foreach (var column in Columns) {
			var value = column.StorageIndex < cells.Count ? cells[column.StorageIndex] : "";
			row.Cells.Add(new TableCellVM(row, column.StorageIndex, value,
				UsesAssetCells && column.StorageIndex > 0, column.IsVisible));
		}
		return row;
	}

	private void RebuildColumns() {
		Columns.Clear();
		var configured = new HashSet<string>(ProjectContext.Languages, StringComparer.Ordinal);

		void AddColumn(string name, bool visible) {
			var index = m_storageHeaders.IndexOf(name);
			if (index >= 0) Columns.Add(new TableColumnVM(name, index, visible));
		}

		AddColumn("id", true);
		foreach (var language in ProjectContext.Languages) AddColumn(language, true);
		foreach (var header in m_storageHeaders)
			if (header != "id" && !configured.Contains(header)) AddColumn(header, false);
	}

	private void OnLanguagesChanged() {
		if (!HasContent || string.IsNullOrEmpty(CurrentPath)) return;
		var dirty = IsDirty;
		try {
			ReconcileFileOnDisk(ProjectContext.Resolve(CurrentPath));
			ReconcileOpenRows();
		} catch (Exception e) {
			Log.Error($"Table Editor: failed to reconcile languages for '{CurrentPath}': {e.Message}");
		} finally {
			IsDirty = dirty;
		}
	}

	private void ReconcileOpenRows() {
		m_loading = true;
		try {
			var storedRows = Rows.Select(row => {
				var values = Enumerable.Repeat("", m_storageHeaders.Count).ToList();
				foreach (var cell in row.Cells)
					if (cell.StorageIndex < values.Count) values[cell.StorageIndex] = cell.Value ?? "";
				return values;
			}).ToList();

			if (m_storageHeaders.Count == 0) m_storageHeaders.Add("id");
			var idIndex = m_storageHeaders.IndexOf("id");
			if (idIndex < 0) {
				m_storageHeaders.Insert(0, "id");
				foreach (var row in storedRows) row.Insert(0, "");
			} else if (idIndex > 0) {
				m_storageHeaders.RemoveAt(idIndex);
				m_storageHeaders.Insert(0, "id");
				foreach (var row in storedRows) {
					var id = idIndex < row.Count ? row[idIndex] : "";
					if (idIndex < row.Count) row.RemoveAt(idIndex);
					row.Insert(0, id);
				}
			}
			foreach (var language in ProjectContext.Languages)
				if (!m_storageHeaders.Contains(language, StringComparer.Ordinal)) {
					m_storageHeaders.Add(language);
					foreach (var row in storedRows) row.Add("");
				}

			RebuildColumns();
			Rows.Clear();
			foreach (var values in storedRows) Rows.Add(MakeRow(values));
		} finally {
			m_loading = false;
		}
	}

	private void CloseCurrent() {
		CurrentUid = "";
		CurrentPath = "";
		FileName = "";
		Columns.Clear();
		Rows.Clear();
		m_storageHeaders.Clear();
		HasContent = false;
		UsesAssetCells = false;
		UpdateTitle();
	}

	[RelayCommand]
	private async Task Save() {
		if (!HasContent || string.IsNullOrEmpty(CurrentPath)) return;
		var csv = Serialize();
		var realPath = ProjectContext.Resolve(CurrentPath);
		await Task.Run(() => File.WriteAllText(realPath, csv));
		MetaFile.Touch(CurrentPath);
		AutosaveService.Delete(CurrentUid, AssetTypeRegistry.GetExtension(CurrentPath));
		IsDirty = false;
	}

	[RelayCommand]
	private void AddRow() {
		if (!HasContent) return;
		Rows.Add(MakeRow([]));
		IsDirty = true;
	}

	[RelayCommand]
	private void DeleteRow(TableRowVM? row) {
		if (row is null) return;
		Rows.Remove(row);
		IsDirty = true;
	}

	private void OnCellEdited() {
		if (m_loading) return;
		IsDirty = true;
	}

	private string Serialize() {
		var sb = new StringBuilder();
		AppendCsvRow(sb, m_storageHeaders);
		foreach (var row in Rows) {
			var stored = Enumerable.Repeat("", m_storageHeaders.Count).ToArray();
			foreach (var cell in row.Cells)
				if (cell.StorageIndex < stored.Length) stored[cell.StorageIndex] = cell.Value ?? "";
			AppendCsvRow(sb, stored);
		}
		return sb.ToString();
	}

	private static void ReconcileFileOnDisk(string realPath) {
		var original = File.ReadAllText(realPath);
		var reconciled = ReconcileCsvForLanguages(original, ProjectContext.Languages, out var changed);
		if (!changed) return;

		var temp = realPath + ".tmp." + Guid.NewGuid().ToString("N");
		try {
			File.WriteAllText(temp, reconciled);
			File.Move(temp, realPath, true);
		} finally {
			if (File.Exists(temp)) File.Delete(temp);
		}
	}

	internal static string ReconcileCsvForLanguages(string csv, IReadOnlyList<string> languages, out bool changed) {
		var table = ParseCsv(csv);
		changed = false;
		if (table.Count == 0) {
			table.Add(["id"]);
			changed = true;
		}
		if (table[0].Count == 0) {
			table[0].Add("id");
			changed = true;
		}

		var header = table[0];
		var idIndex = header.IndexOf("id");
		if (idIndex < 0) {
			header.Insert(0, "id");
			for (var r = 1; r < table.Count; r++) table[r].Insert(0, "");
			changed = true;
		} else if (idIndex > 0) {
			header.RemoveAt(idIndex);
			header.Insert(0, "id");
			for (var r = 1; r < table.Count; r++) {
				var id = idIndex < table[r].Count ? table[r][idIndex] : "";
				if (idIndex < table[r].Count) table[r].RemoveAt(idIndex);
				table[r].Insert(0, id);
			}
			changed = true;
		}

		foreach (var language in languages)
			if (!header.Contains(language, StringComparer.Ordinal)) {
				header.Add(language);
				for (var r = 1; r < table.Count; r++) table[r].Add("");
				changed = true;
			}

		if (!changed) return csv;
		var sb = new StringBuilder();
		foreach (var row in table) AppendCsvRow(sb, row);
		return sb.ToString();
	}

	private static void AppendCsvRow(StringBuilder sb, IEnumerable<string> fields) {
		var first = true;
		foreach (var field in fields) {
			if (!first) sb.Append(',');
			first = false;
			if (field.IndexOfAny([',', '"', '\r', '\n']) >= 0)
				sb.Append('"').Append(field.Replace("\"", "\"\"")).Append('"');
			else
				sb.Append(field);
		}
		sb.Append('\n');
	}

	private static List<List<string>> ParseCsv(string text) {
		var table = new List<List<string>>();
		var row = new List<string>();
		var field = new StringBuilder();
		var inQuotes = false;
		var started = false;

		void PushField() {
			row.Add(field.ToString());
			field.Clear();
			started = false;
		}

		void PushRow() {
			PushField();
			table.Add(row);
			row = [];
		}

		for (var i = 0; i < text.Length; i++) {
			var c = text[i];
			if (inQuotes) {
				if (c == '"') {
					if (i + 1 < text.Length && text[i + 1] == '"') {
						field.Append('"');
						i++;
					} else {
						inQuotes = false;
					}
				} else {
					field.Append(c);
				}

				continue;
			}

			switch (c) {
				case '"':
					inQuotes = true;
					started = true;
					break;
				case ',':
					PushField();
					started = true;
					break;
				case '\r': break;
				case '\n':
					if (row.Count > 0 || started || field.Length > 0) PushRow();
					break;
				default:
					field.Append(c);
					started = true;
					break;
			}
		}

		if (row.Count > 0 || started || field.Length > 0) PushRow();
		return table;
	}

	private void UpdateTitle() {
		Title = IsDirty ? BaseTitle + " *" : BaseTitle;
		DisplayTitle = string.IsNullOrEmpty(FileName) ? BaseTitle
			: IsDirty ? $"{FileName} *"
			: FileName;
	}

	partial void OnIsDirtyChanged(bool value) {
		UpdateTitle();
	}
}

public sealed partial class TableRowVM : ObservableObject {
	private readonly Action m_onEdited;

	public TableRowVM(Action onEdited) {
		m_onEdited = onEdited;
	}

	public ObservableCollection<TableCellVM> Cells { get; } = [];

	internal void NotifyEdited() {
		m_onEdited();
	}
}

public sealed partial class TableCellVM : ObservableObject {
	private readonly TableRowVM m_row;
	[ObservableProperty] private string? m_value;

	public TableCellVM(TableRowVM row, int storageIndex, string value, bool isAssetReference, bool isVisible) {
		m_row = row;
		StorageIndex = storageIndex;
		m_value = value;
		IsAssetReference = isAssetReference;
		IsVisible = isVisible;
	}

	public int StorageIndex { get; }
	public bool IsAssetReference { get; }
	public bool IsVisible { get; }

	partial void OnValueChanged(string? value) {
		m_row.NotifyEdited();
	}
}

public sealed record TableColumnVM(string Name, int StorageIndex, bool IsVisible);
