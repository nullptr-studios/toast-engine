using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Lucide.Avalonia;
using editor.Assets.Types;
using editor.LuaEditor.Controls;
using editor.LuaEditor.Editing;
using editor.LuaEditor.Graph;
using editor.LuaEditor.Types;

namespace editor.LuaEditor;

public sealed partial class ParamRow : ObservableObject {
	[ObservableProperty] private string m_name = "";
	[ObservableProperty] private string m_typeText = "any";

	internal Action<ParamRow, string, string>? Changed { get; set; }
	internal bool Loading { get; set; }

	partial void OnNameChanged(string oldValue, string newValue) {
		if (!Loading) Changed?.Invoke(this, oldValue, newValue);
	}

	partial void OnTypeTextChanged(string oldValue, string newValue) {
		if (!Loading) Changed?.Invoke(this, "", "");
	}
}

public sealed partial class ReturnRow : ObservableObject {
	[ObservableProperty] private string m_typeText = "any";

	internal Action? Changed { get; set; }
	internal bool Loading { get; set; }

	partial void OnTypeTextChanged(string value) {
		if (!Loading) Changed?.Invoke();
	}
}

public sealed partial class LuaGraphEditorViewModel {
	[ObservableProperty] private IReadOnlyList<PickItem> m_typeItems = [];

	public ObservableCollection<ParamRow> ParamRows { get; } = [];
	public ObservableCollection<ReturnRow> ReturnRows { get; } = [];

	private string m_signatureKey = "";

	private IReadOnlyList<PickItem> BuildTypeItems() {
		var items = new List<PickItem>();
		void Basic(string name, LuaType type) => items.Add(new PickItem(name, "Basic", LucideIconKind.CircleDot, PinStyle.ColorOf(type), PinStyle.ShapeOf(type)));
		Basic("number", LuaType.Number);
		Basic("integer", LuaType.Integer);
		Basic("string", LuaType.String);
		Basic("boolean", LuaType.Boolean);
		Basic("table", new PrimitiveType(PrimitiveKind.Table));
		Basic("function", new PrimitiveType(PrimitiveKind.Function));
		Basic("nil", LuaType.Nil);
		Basic("any", LuaType.Any);
		foreach (var name in new[] { "vec2", "vec3", "vec4", "quat", "color" })
			items.Add(new PickItem(name, "Math", LucideIconKind.CircleDot, PinStyle.Vector, PinShape.Circle));
		if (Session is not null) {
			foreach (var cls in Session.Universe.Classes.OrderBy(c => c.Name, StringComparer.Ordinal)) {
				if (items.Any(i => i.Value == cls.Name)) continue;
				var asset = AssetTypeRegistry.ByCppTypeName(cls.Name) ?? AssetTypeRegistry.ByType(cls.Name.Replace("Asset", "").ToLowerInvariant());
				if (cls.Name.Contains("Asset", StringComparison.OrdinalIgnoreCase)) items.Add(new PickItem(cls.Name, "Assets", asset?.Icon ?? LucideIconKind.Image, asset?.ChipColor ?? "Orange"));
				else if (cls.Name.Contains("Event", StringComparison.OrdinalIgnoreCase)) items.Add(new PickItem(cls.Name, "Events", LucideIconKind.CirclePlay, "Red"));
				else items.Add(new PickItem(cls.Name, "Nodes", LucideIconKind.Box, "Blue"));
			}
			foreach (var e in Session.Universe.Enums.OrderBy(e => e.Name, StringComparer.Ordinal))
				items.Add(new PickItem(e.Name, "Enums", LucideIconKind.List, "Orange"));
		}
		return items;
	}

	private IReadOnlyList<PickItem> BuildBaseItems() => TypeItems.Where(i => i.Group is not ("Basic" or "Math" or "Enums")).Prepend(new PickItem("(none)", "Base", LucideIconKind.Minus, "TextMuted")).ToList();

	partial void OnScriptClassChanged(string value) => CommitScriptClass();

	partial void OnScriptBaseChanged(string value) => CommitScriptClass();

	private void CommitScriptClass() {
		if (m_inspecting || Session is null || !InspectorIsScript) return;
		var name = ScriptClass.Trim();
		var parent = ScriptBase is "(none)" or "" ? null : ScriptBase;
		if (parent is not null && name.Length == 0) name = char.ToUpperInvariant(InspectorName[0]) + InspectorName[1..] + "Script";
		Session.Apply(g => g.SetScriptClass(name.Length == 0 ? null : name, parent));
	}

	public ObservableCollection<TableEntryRow> TableRows { get; } = [];

	[ObservableProperty] private bool m_inspectorIsTableField;

	private string m_tableKey = "";

	private void LoadTable(GraphVariable v) {
		var table = GraphSession.TableOfField(v);
		var declared = v.Type is UnionType u && u.Members.Count(m => m != LuaType.Nil) == 1 ? u.Members.First(m => m != LuaType.Nil) : v.Type;
		InspectorIsTableField = v.Kind == VariableKind.Field && declared is not (ArrayType or TableType) && (declared is PrimitiveType { Kind: PrimitiveKind.Table } || table is { Flavor: TableFlavor.Table });
		if (table is null) {
			m_tableKey = "";
			TableRows.Clear();
			return;
		}
		var key = v.Name + "|" + string.Join(",", table.Fields.Select(f => f.Kind + ":" + (f.Name ?? "") + ":" + GraphSession.TableEntryType(f)));
		if (key == m_tableKey) return;
		m_tableKey = key;
		if (TableRows.Count == table.Fields.Count && TableRows.Select((r, i) => r.Field == table.Fields[i]).All(same => same)) {
			for (var i = 0; i < table.Fields.Count; i++) {
				var row = TableRows[i];
				var field = table.Fields[i];
				row.Loading = true;
				row.Name = field.Kind == TableFieldKind.Named ? field.Name ?? "" : field.Kind == TableFieldKind.Keyed ? "[key]" : field.Value.Name;
				row.TypeText = GraphSession.TableEntryType(field);
				row.Loading = false;
			}
			return;
		}
		TableRows.Clear();
		foreach (var field in table.Fields) {
			var named = field.Kind == TableFieldKind.Named;
			var label = named ? field.Name ?? "" : field.Kind == TableFieldKind.Keyed ? "[key]" : field.Value.Name;
			TableRows.Add(new TableEntryRow { Loading = true, Name = label, TypeText = GraphSession.TableEntryType(field), IsNamed = named, Kind = field.Kind.ToString(), Table = table, Field = field, Changed = OnTableEntryChanged }.Ready());
		}
	}

	private void ClearTable() {
		m_tableKey = "";
		InspectorIsTableField = false;
		TableRows.Clear();
	}

	private void OnTableEntryChanged(TableEntryRow row, bool nameChanged) {
		if (Session is null || row.Table is not { } table || row.Field is not { } field) return;
		var index = table.Fields.ToList().IndexOf(field);
		if (index < 0) return;
		try {
			if (nameChanged) {
				if (row.IsNamed) Session.RenameTableField(table, field, row.Name.Trim());
			} else {
				Session.SetTableEntryType(table, index, row.TypeText);
			}
		} catch (TypeSyntaxException e) {
			Status = e.Message;
		}
	}

	[RelayCommand]
	private void AddTableField() => AddEntryToSelected(TableFieldKind.Named);

	[RelayCommand]
	private void AddTableItem() => AddEntryToSelected(TableFieldKind.Positional);

	private void AddEntryToSelected(TableFieldKind kind) {
		if (Session is null || SelectedVariable?.Variable is not { Kind: VariableKind.Field } v || DefaultPin(v) is not { } pin) return;
		m_tableKey = "";
		Session.AddFieldEntry(pin, kind);
	}

	[RelayCommand]
	private void RemoveTableEntryRow(TableEntryRow? row) {
		if (Session is null || row?.Table is not { } table || row.Field is not { } field) return;
		var index = table.Fields.ToList().IndexOf(field);
		m_tableKey = "";
		if (index >= 0) Session.RemoveTableEntry(table, index);
	}

	private void Safely(Action edit) {
		try {
			edit();
		} catch (Exception e) when (e is not OutOfMemoryException) {
			Status = e.Message;
		}
	}

	private FunctionDecl? SelectedFunction

 => SelectedVariable?.Variable?.Declaration as FunctionDecl;

	private void LoadSignature(FunctionDecl function, FunctionType type) {
		var parameters = function.Entry.Params.Select(p => (p.Name, Type: type.Params.FirstOrDefault(x => x.Name == p.Name)?.Type ?? LuaType.Any)).ToList();
		var key = function.Name + "|" + string.Join(",", parameters.Select(p => p.Name + ":" + TypeFormatter.Format(p.Type))) + "|" + string.Join(",", type.Returns.Select(TypeFormatter.Format));
		if (key == m_signatureKey) return;
		m_signatureKey = key;
		// replacing rows during an edit can detach the active picker
		if (ParamRows.Count == parameters.Count && ReturnRows.Count == type.Returns.Count) {
			for (var i = 0; i < parameters.Count; i++) {
				var row = ParamRows[i];
				row.Loading = true;
				row.Name = parameters[i].Name;
				row.TypeText = TypeFormatter.Format(parameters[i].Type);
				row.Loading = false;
			}
			for (var i = 0; i < type.Returns.Count; i++) {
				var row = ReturnRows[i];
				row.Loading = true;
				row.TypeText = TypeFormatter.Format(type.Returns[i]);
				row.Loading = false;
			}
			return;
		}
		ParamRows.Clear();
		foreach (var (name, paramType) in parameters)
			ParamRows.Add(new ParamRow { Loading = true, Name = name, TypeText = TypeFormatter.Format(paramType), Changed = OnParamChanged }.Ready());
		ReturnRows.Clear();
		foreach (var returned in type.Returns)
			ReturnRows.Add(new ReturnRow { Loading = true, TypeText = TypeFormatter.Format(returned), Changed = OnReturnChanged }.Ready());
	}

	private void ClearSignature() {
		m_signatureKey = "";
		ParamRows.Clear();
		ReturnRows.Clear();
	}

	private void OnParamChanged(ParamRow row, string oldName, string newName) {
		if (Session is null || SelectedFunction is not { } function) return;
		if (oldName.Length > 0 || newName.Length > 0) {
			var binding = function.Entry.Params.FirstOrDefault(p => p.Name == oldName);
			if (binding is null || newName.Length == 0) return;
			Safely(() => Session.Apply(g => g.Rename(binding, newName)));
			return;
		}
		try {
			var type = row.TypeText.Trim().Length == 0 ? null : TypeExprParser.Parse(row.TypeText);
			Safely(() => Session.Apply(g => g.SetParamType(function, row.Name, type)));
		} catch (TypeSyntaxException e) {
			Status = e.Message;
		}
	}

	private void OnReturnChanged() {
		if (Session is null || SelectedFunction is not { } function) return;
		var types = new List<LuaType>();
		try {
			foreach (var row in ReturnRows) types.Add(TypeExprParser.Parse(row.TypeText.Trim().Length == 0 ? "any" : row.TypeText));
		} catch (TypeSyntaxException e) {
			Status = e.Message;
			return;
		}
		Safely(() => Session.Apply(g => g.SetReturnTypes(function, types)));
	}

	[RelayCommand]
	private void AddParameter() {
		if (Session is null || SelectedFunction is not { } function) return;
		var taken = function.Entry.Params.Select(p => p.Name).ToHashSet();
		var name = "value";
		for (var i = 2; taken.Contains(name); i++) name = "value" + i;
		m_signatureKey = "";
		Session.Apply(g => g.AddParam(function, name));
	}

	[RelayCommand]
	private void RemoveParameter(ParamRow? row) {
		if (Session is null || row is null || SelectedFunction is not { } function) return;
		m_signatureKey = "";
		Session.Apply(g => g.RemoveParam(function, row.Name));
	}

	[RelayCommand]
	private void AddReturn() {
		if (Session is null || SelectedFunction is not { } function) return;
		var types = ReturnRows.Select(r => TypeExprParser.Parse(r.TypeText.Trim().Length == 0 ? "any" : r.TypeText)).Append(LuaType.Any).ToList();
		m_signatureKey = "";
		Session.Apply(g => g.SetReturnTypes(function, types));
	}

	[RelayCommand]
	private void RemoveReturn(ReturnRow? row) {
		if (Session is null || row is null || SelectedFunction is not { } function) return;
		var types = ReturnRows.Where(r => r != row).Select(r => TypeExprParser.Parse(r.TypeText.Trim().Length == 0 ? "any" : r.TypeText)).ToList();
		m_signatureKey = "";
		Session.Apply(g => g.SetReturnTypes(function, types));
	}
}

public sealed partial class TableEntryRow : ObservableObject {
	[ObservableProperty] private string m_name = "";
	[ObservableProperty] private string m_typeText = "any";

	public bool IsNamed { get; init; }
	public string Kind { get; init; } = "";
	internal TableNode? Table { get; init; }
	internal TableField? Field { get; init; }
	internal Action<TableEntryRow, bool>? Changed { get; set; }
	internal bool Loading { get; set; }

	partial void OnNameChanged(string value) {
		if (!Loading) Changed?.Invoke(this, true);
	}

	partial void OnTypeTextChanged(string value) {
		if (!Loading) Changed?.Invoke(this, false);
	}
}

internal static class RowExtensions {
	public static TableEntryRow Ready(this TableEntryRow row) {
		row.Loading = false;
		return row;
	}

	public static ParamRow Ready(this ParamRow row) {
		row.Loading = false;
		return row;
	}

	public static ReturnRow Ready(this ReturnRow row) {
		row.Loading = false;
		return row;
	}
}
