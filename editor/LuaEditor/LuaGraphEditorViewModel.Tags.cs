using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using editor.LuaEditor.Graph;
using editor.LuaEditor.Types;

namespace editor.LuaEditor;

public sealed partial class TagRow(LuaGraphEditorViewModel owner, string text) : ObservableObject {
	[ObservableProperty] private string m_text = text;

	partial void OnTextChanged(string value) => owner.TagEdited();
}

public sealed partial class LuaGraphEditorViewModel {
	public ObservableCollection<TagRow> Tags { get; } = [];

	[ObservableProperty] private IReadOnlyList<string> m_typeOptions = [];

	private static LuaType WithoutNil(LuaType type) => type is UnionType u && u.Members.Any(m => m is PrimitiveType { Kind: PrimitiveKind.Nil })
		? LuaType.Union(u.Members.Where(m => m is not PrimitiveType { Kind: PrimitiveKind.Nil }).ToArray())
		: type;

	private IReadOnlyList<string> TypeOptionsFor(string current) {
		var names = TypeNames.ToList();
		if (current.Length > 0 && !names.Contains(current)) names.Insert(0, current);
		return names;
	}

	private static bool IsManagedTag(string name, bool function) => name == "type" && !function || function && name is "param" or "return";

	private static string? TagText(string line) => line.StartsWith("---@", StringComparison.Ordinal) ? line[4..].TrimEnd() : null;

	private void LoadTags(GraphVariable v) {
		var function = v.Kind == VariableKind.Function;
		foreach (var line in v.Declaration.LeadingLines) {
			if (TagText(line) is not { } text) continue;
			var name = text.Split(' ', 2)[0];
			if (!IsManagedTag(name, function)) Tags.Add(new TagRow(this, text));
		}
	}

	internal void TagEdited() {
		if (m_inspecting) return;
		WriteTags();
	}

	private void WriteTags() {
		if (Session is null || SelectedVariable?.Variable is not { } v) return;
		var function = v.Kind == VariableKind.Function;
		var kept = v.Declaration.LeadingLines
			.Where(l => l.StartsWith("---", StringComparison.Ordinal) && (TagText(l) is not { } t || IsManagedTag(t.Split(' ', 2)[0], function)))
			.ToList();
		var lines = kept.Concat(Tags.Select(r => r.Text.Trim()).Where(t => t.Length > 0).Select(t => "---@" + t.TrimStart('@'))).ToList();
		Session.Apply(g => g.SetAnnotationLines(v.Declaration, lines));
	}

	[RelayCommand]
	private void AddTag() {
		if (SelectedVariable?.Variable is null) return;
		m_inspecting = true;
		Tags.Add(new TagRow(this, ""));
		m_inspecting = false;
	}

	[RelayCommand]
	private void RemoveTag(TagRow row) {
		Tags.Remove(row);
		WriteTags();
	}
}
