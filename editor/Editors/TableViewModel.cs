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

	private bool m_loading;

	public TableViewModel() {
		if (Design.IsDesignMode) InitDesignData();
	}

	public ObservableCollection<string> Columns { get; } = [];
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
			var text = File.ReadAllText(contentSourceRealPath ?? ProjectContext.Resolve(virtualPath));
			CurrentUid = uid;
			CurrentPath = virtualPath;
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

		// Header row is generated
		if (table.Count > 0 && table[0].Count > 0) {
			foreach (var col in table[0]) Columns.Add(col);
		} else {
			Columns.Add("id");
			foreach (var lang in ProjectContext.Languages) Columns.Add(lang);
		}

		for (var r = 1; r < table.Count; r++)
			Rows.Add(MakeRow(table[r]));

		FileName = Path.GetFileName(virtualPath);
		HasContent = true;
		UpdateTitle();

		m_loading = false;
	}

	private TableRowVM MakeRow(IReadOnlyList<string> cells) {
		var row = new TableRowVM(OnCellEdited);
		for (var c = 0; c < Columns.Count; c++)
			row.Cells.Add(new TableCellVM(row, c < cells.Count ? cells[c] : ""));
		return row;
	}

	private void CloseCurrent() {
		CurrentUid = "";
		CurrentPath = "";
		FileName = "";
		Columns.Clear();
		Rows.Clear();
		HasContent = false;
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
		AppendCsvRow(sb, Columns);
		foreach (var row in Rows)
			AppendCsvRow(sb, row.Cells.Select(c => c.Value));
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
	[ObservableProperty] private string m_value;

	public TableCellVM(TableRowVM row, string value) {
		m_row = row;
		m_value = value;
	}

	partial void OnValueChanged(string value) {
		m_row.NotifyEdited();
	}
}
