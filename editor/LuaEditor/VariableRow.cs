using CommunityToolkit.Mvvm.ComponentModel;
using Avalonia;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Lucide.Avalonia;
using editor.LuaEditor.Graph;
using editor.LuaEditor.Types;
using editor.Workspace;

namespace editor.LuaEditor;

public enum VariableRowKind { Script, Group, Field, Method, Tick, Native, Local }

public sealed partial class VariableRow(string id, VariableRow? parent, string name, LucideIconKind icon,
	VariableRowKind kind, GraphVariable? variable = null, Binding? binding = null, MemberInfo? native = null,
	Bitmap? bitmap = null, string? nodeColor = null) : ObservableObject, IHierarchyRow {
	[ObservableProperty] private bool m_isSelected;
	[ObservableProperty] private bool m_isExpanded = true;
	[ObservableProperty] private bool m_isFolded;
	public string Id { get; } = id;
	public VariableRow? Parent { get; } = parent;
	public string Name { get; } = name;
	public VariableRowKind Kind { get; } = kind;
	public GraphVariable? Variable { get; } = variable;
	public Binding? Binding { get; } = binding;
	public MemberInfo? Native { get; } = native;
	public Bitmap? Bitmap { get; } = bitmap;
	public bool UsesLucide => Bitmap is null && Glyph is null;
	public bool UsesGlyph => Glyph is not null;
	public bool UsesBitmap => Bitmap is not null;
	public Geometry? Glyph => Icon switch {
		LucideIconKind.Code when Kind == VariableRowKind.Local => s_lambda,
		LucideIconKind.Book when Kind == VariableRowKind.Local => s_bookBookmark,
		_ => null
	};

	// Lucide Lambda and Book Bookmark paths, from lucide-icons/lucide.
	// Copyright (c) 2026 Lucide Icons and Contributors
	// Permission to use, copy, modify, and/or distribute this software for any
	// purpose with or without fee is hereby granted, provided that the above
	// copyright notice and this permission notice appear in all copies.
	// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
	// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
	// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
	// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
	// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
	// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
	// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
	private static readonly Geometry s_lambda = Geometry.Parse("M11.38 10 5 20 M19 18a2 2 0 0 1 -2 2c-4.87 -.003 -5.052 -16 -10 -16a2 2 0 0 0 -2 2");
	private static readonly Geometry s_bookBookmark = Geometry.Parse("M10 2v7.751a.25 .25 0 0 0 .407 .195l2.28 -1.834a.5 .5 0 0 1 .627 0l2.28 1.834A.25 .25 0 0 0 16 9.751V2 M4 19.5v-15A2.5 2.5 0 0 1 6.5 2H19a1 1 0 0 1 1 1v18a1 1 0 0 1 -1 1H6.5a1 1 0 0 1 0 -5H20");
	public LucideIconKind Icon { get; } = icon;
	public int Depth => Parent is null ? 0 : Parent.Depth + 1;
	public bool HasChildren { get; internal set; }
	public bool CanDrag => Variable is not null && Kind != VariableRowKind.Tick;
	public bool IsPlaceholder => Kind == VariableRowKind.Tick && Variable is null;
	public FontStyle FontStyle => IsPlaceholder ? FontStyle.Italic : FontStyle.Normal;
	public double Opacity => IsPlaceholder ? 0.45 : 1;
	public string Detail => Variable is null || Kind == VariableRowKind.Script ? "" : TypeFormatter.Display(Variable.Type);
	public string ColorKey => nodeColor ?? Kind switch {
		VariableRowKind.Native => Parent?.ColorKey ?? "TextMuted",
		VariableRowKind.Field => "Blue",
		VariableRowKind.Method => "Magenta",
		VariableRowKind.Tick when IsPlaceholder => "TextMuted",
		VariableRowKind.Tick => "Orange",
		VariableRowKind.Local when Icon == LucideIconKind.Code => "Magenta",
		VariableRowKind.Local when Icon == LucideIconKind.Table => "Cyan",
		VariableRowKind.Local => "Green",
		_ => Id switch {
			"fields" => "Blue",
			"methods" or "script" => "Magenta",
			"ticks" => "Orange",
			"local" => "Green",
			_ => "TextMuted"
		}
	};
	public Thickness ContentMargin => new(HierarchyConnectorLayer.LeftPad + Depth * HierarchyConnectorLayer.IndentStep, 0, 0, 0);
}
