using System;
using System.Collections.Generic;
using System.Globalization;
using System.Collections.ObjectModel;
using System.Linq;
using System.IO;
using System.Threading.Tasks;
using Avalonia.Controls;
using Lucide.Avalonia;
using Loretta.CodeAnalysis.Lua.Syntax;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using editor.LuaEditor.Canvas;
using editor.LuaEditor.Editing;
using editor.LuaEditor.Graph;
using editor.LuaEditor.Layout;
using editor.LuaEditor.Syntax;
using editor.LuaEditor.Types;
using Dock.Model.Mvvm.Controls;
using editor.Assets;
using editor.Assets.Types;
using editor.Components.Modals;
using editor.Editors;

namespace editor.LuaEditor;

public sealed record ProblemRow(string Severity, string Text, GraphNode? Node) {
	public uint Level => Severity switch { "Error" => 3u, "Warning" => 2u, _ => 1u };
	public string ShortName => Level switch { 3 => "ERROR", 2 => "WARN", _ => "INFO" };
}

public sealed partial class LuaGraphEditorViewModel : Tool, IToastZoneEditor, Workspace.IAutosavable {
	[ObservableProperty] private GraphSession? m_session;
	[ObservableProperty] private string? m_banner;
	[ObservableProperty] private bool m_isReadOnly;
	[ObservableProperty] private string m_inspectorTitle = "Nothing selected";
	[ObservableProperty] private string m_inspectorName = "";
	[ObservableProperty] private string m_inspectorType = "";
	[ObservableProperty] private string m_inspectorDoc = "";
	[ObservableProperty] private bool m_inspectorIsVariable;
	[ObservableProperty] private bool m_inspectorIsFunction;
	[ObservableProperty] private bool m_inspectorIsScript;
	[ObservableProperty] private string m_scriptClass = "";
	[ObservableProperty] private string m_scriptBase = "";
	[ObservableProperty] private IReadOnlyList<editor.LuaEditor.Controls.PickItem> m_scriptBaseItems = [];
	[ObservableProperty] private string m_defaultKind = "none";
	[ObservableProperty] private float m_defaultNumber;
	[ObservableProperty] private string m_defaultInteger = "0";
	[ObservableProperty] private string m_defaultText = "";
	[ObservableProperty] private bool m_defaultBool;
	[ObservableProperty] private bool m_inspectorNullable;
	[ObservableProperty] private bool m_inspectorExported;
	[ObservableProperty] private bool m_inspectorCanExport;
	private bool m_inspecting;
	[ObservableProperty] private string m_inspectorParams = "";
	[ObservableProperty] private string m_inspectorReturns = "";
	[ObservableProperty] private string m_status = "";
	[ObservableProperty] private VariableRow? m_selectedVariable;

	public LuaGraphEditorViewModel() {
		if (Design.IsDesignMode) LoadDesignData();
	}

	public ObservableCollection<VariableRow> Variables { get; } = [];

	private readonly List<VariableRow> m_allRows = [];
	private readonly HashSet<string> m_collapsedSections = [];

	[ObservableProperty] private string m_filterText = "";

	partial void OnFilterTextChanged(string value) => ShowRows();

	public void SetSection(VariableRow row) {
		if (!row.HasChildren) return;
		row.IsExpanded = !row.IsExpanded;
		if (row.IsExpanded) m_collapsedSections.Remove(row.Id);
		else m_collapsedSections.Add(row.Id);
		ShowRows();
	}

	private void ShowRows() {
		var filter = FilterText.Trim();
		var matching = new HashSet<VariableRow>();
		if (filter.Length > 0) {
			foreach (var row in m_allRows.Where(r => r.Name.Contains(filter, StringComparison.OrdinalIgnoreCase)))
				for (var ancestor = row; ancestor is not null; ancestor = ancestor.Parent) matching.Add(ancestor);
		}
		bool Visible(VariableRow row) => row.Parent is null || row.Parent.IsExpanded && Visible(row.Parent);
		Variables.Clear();
		foreach (var row in m_allRows) {
			row.IsSelected = row == SelectedVariable;
			row.IsFolded = row.HasChildren && !row.IsExpanded && filter.Length == 0;
			if (filter.Length > 0 ? matching.Contains(row) : Visible(row)) Variables.Add(row);
		}
	}

	public ObservableCollection<ProblemRow> Problems { get; } = [];

	public event Action<GraphNode>? FocusRequested;

	public bool CanEdit => Session is not null && !IsReadOnly && !m_lockedByOther;

	private bool m_lockedByOther;

	partial void OnIsReadOnlyChanged(bool value) => OnPropertyChanged(nameof(CanEdit));

	partial void OnSessionChanged(GraphSession? oldValue, GraphSession? newValue) {
		OnPropertyChanged(nameof(CanEdit));
		if (oldValue is not null) {
			oldValue.Changed -= Refresh;
			oldValue.SelectionChanged -= SelectCanvasMember;
		}
		SelectedVariable = null;
		if (newValue is not null) {
			newValue.Changed += Refresh;
			newValue.SelectionChanged += SelectCanvasMember;
		}
		Refresh();
	}

	partial void OnSelectedVariableChanged(VariableRow? value) {
		foreach (var row in m_allRows) row.IsSelected = row == value;
		Inspect();
	}

	public void ShowError(string message) {
		Session = null;
		Banner = message;
		IsReadOnly = true;
		m_allRows.Clear();
		ShowRows();
		Problems.Clear();
	}

	public void Open(GraphSession session) {
		Banner = null;
		IsReadOnly = false;
		Session = session;
	}

	public void Refresh() {
		RefreshVariables();
		RefreshProblems();
		Inspect();
		Status = Session?.Message ?? "";
		OnPropertyChanged(nameof(IsDirty));
	}

	private object? m_itemsUniverse;

	private void RefreshVariables() {
		if (!ReferenceEquals(m_itemsUniverse, Session?.Universe)) {
			m_itemsUniverse = Session?.Universe;
			TypeItems = BuildTypeItems();
		}
		var keep = SelectedVariable;
		m_allRows.Clear();
		if (Session is { } session) {
			var graph = session.Graph;
			var script = ScriptInfo.Find(graph);
			var all = graph.Variables;
			VariableRow Add(string id, VariableRow? parent, string name, LucideIconKind icon, VariableRowKind kind,
				GraphVariable? variable = null, Binding? binding = null, MemberInfo? native = null, Avalonia.Media.Imaging.Bitmap? bitmap = null, string? nodeColor = null) {
				var row = new VariableRow(id, parent, name, icon, kind, variable, binding, native, bitmap, nodeColor) {
					IsExpanded = !m_collapsedSections.Contains(id)
				};
				if (parent is not null) parent.HasChildren = true;
				m_allRows.Add(row);
				return row;
			}
			VariableRow Group(string id, VariableRow? parent, string name, LucideIconKind icon, Avalonia.Media.Imaging.Bitmap? bitmap = null, string? nodeColor = null)
				=> Add(id, parent, name, icon, VariableRowKind.Group, bitmap: bitmap, nodeColor: nodeColor);
			var table = script is null ? null : new GraphVariable(script.Table.Name, VariableKind.ModuleLocal, false,
				new NamedType(script.ClassName, []), null, script.ClassAnnotations.Doc, script.Declaration);
			var scriptRow = Add("script", null, "Script Table", LucideIconKind.CodeXml, VariableRowKind.Script, table, script?.Table);
			var fields = Group("fields", scriptRow, "Fields", LucideIconKind.Cuboid);
			foreach (var v in Sort(all.Where(v => v.Kind == VariableKind.Field)))
				Add("field/" + v.Name, fields, v.Name, LucideIconKind.Cuboid, VariableRowKind.Field, v);
			var methods = Group("methods", scriptRow, "Methods", LucideIconKind.Box);
			var own = all.Where(v => v.Declaration is FunctionDecl f && script?.Methods.Contains(f) == true).ToList();
			foreach (var v in Sort(own.Where(v => !GraphSession.TickFunctions.Contains(v.Name))))
				Add("method/" + v.Name, methods, v.Name, LucideIconKind.Box, VariableRowKind.Method, v);
			var ticks = Group("ticks", null, "Tick functions", LucideIconKind.Metronome);
			foreach (var name in GraphSession.TickFunctions)
				Add("tick/" + name, ticks, name == "_end" ? "end" : name, LucideIconKind.Metronome, VariableRowKind.Tick,
					own.FirstOrDefault(v => v.Name == name));

			var source = Group("source", null, "Source", LucideIconKind.File);
			foreach (var cls in session.SourceClasses()) {
				var group = Group("source/" + cls.Name, source, cls.Name, LucideIconKind.Box,
					NodeIconCache.Get(editor.Engine.ReflectionDatabase.ResolveIcon(cls.Name)), editor.Engine.ReflectionDatabase.ResolveColor(cls.Name));
				foreach (var member in cls.Members.OrderBy(m => m.Name, StringComparer.OrdinalIgnoreCase).ThenBy(m => m.Name, StringComparer.Ordinal)) {
					var function = member.Type is FunctionType;
					var v = new GraphVariable(member.Name, function ? VariableKind.Function : VariableKind.Field, true,
						member.Type, member.Unit, member.Doc, script!.Declaration);
					Add(group.Id + "/" + member.Name, group, member.Name, function ? LucideIconKind.SquareFunction : LucideIconKind.Binary,
						VariableRowKind.Native, v, native: new MemberInfo(member, cls.Name));
				}
			}
			var locals = Group("local", null, "Local", LucideIconKind.Variable);
			foreach (var v in Sort(all.Where(v => v.Kind == VariableKind.ModuleLocal || v.Declaration is FunctionDecl { IsLocal: true }))) {
				var binding = v.Declaration switch {
					LocalDecl d => d.Names.First(n => n.Name == v.Name),
					FunctionDecl f => f.FunctionBinding,
					_ => null
				};
				var index = graph.Module.IndexOf(v.Declaration);
				Add($"local/{index}/{binding?.OutputIndex}", locals, v.Name, LocalIcon(v, binding), VariableRowKind.Local, v, binding);
			}
		}
		SelectedVariable = keep is null ? null : m_allRows.FirstOrDefault(r =>
			keep.Binding is not null && r.Binding == keep.Binding ||
			keep.Native is null && keep.Binding is null && keep.Variable is { } old && r.Kind == keep.Kind && r.Variable?.Declaration == old.Declaration)
			?? m_allRows.FirstOrDefault(r => r.Id == keep.Id && (r.Kind != VariableRowKind.Local || r.Name == keep.Name))
			?? m_allRows.LastOrDefault(r => r.Kind == keep.Kind && r.Name == keep.Name);
		ShowRows();
	}

	private static IEnumerable<GraphVariable> Sort(IEnumerable<GraphVariable> variables)
		=> variables.OrderBy(v => v.Name, StringComparer.OrdinalIgnoreCase).ThenBy(v => v.Name, StringComparer.Ordinal);

	private static LucideIconKind LocalIcon(GraphVariable variable, Binding? binding) {
		if (variable.Declaration is LocalDecl local && binding is not null &&
			local.Values.ElementAtOrDefault(local.Names.IndexOf(binding))?.Source?.Owner is TableNode table)
			return table.Flavor switch {
				TableFlavor.Array => LucideIconKind.Brackets,
				TableFlavor.Dictionary => LucideIconKind.Book,
				_ => LucideIconKind.Table
			};
		return WithoutNil(variable.Type) switch {
				FunctionType or PrimitiveType { Kind: PrimitiveKind.Function } => LucideIconKind.Code,
			ArrayType => LucideIconKind.Brackets,
			TableType => LucideIconKind.Book,
			PrimitiveType { Kind: PrimitiveKind.Table } => LucideIconKind.Table,
			_ => LucideIconKind.Variable
		};
	}

	public VariableRow? FindRow(string id) => m_allRows.FirstOrDefault(row => row.Id == id);

	public void SelectRow(VariableRow? row) {
		SelectedVariable = row;
		if (row is null || Variables.Contains(row)) return;
		for (var parent = row.Parent; parent is not null; parent = parent.Parent) {
			m_collapsedSections.Remove(parent.Id);
			parent.IsExpanded = true;
		}
		if (FilterText.Length > 0) FilterText = "";
		else ShowRows();
	}

	private void SelectCanvasMember() {
		SelectRow(Session?.Selection is [var node] ? RowForNode(node) : null);
	}

	private VariableRow? RowForNode(GraphNode node) {
		var graph = node.Graph;
		var script = ScriptInfo.Find(graph);
		VariableRow? BindingRow(Binding? binding) => binding is null ? null : m_allRows.FirstOrDefault(r => r.Binding == binding);
		VariableRow? MemberRow(InputPin? receiver, string? name, bool function) {
			if (receiver is null || name is null) return null;
			var type = receiver.ActualType as NamedType;
			if (script is not null && (script.IsTable(receiver) || type?.Name == script.ClassName)) {
				var own = m_allRows.FirstOrDefault(r => r.Variable?.Name == name &&
					(function ? r.Kind is VariableRowKind.Method or VariableRowKind.Tick : r.Kind == VariableRowKind.Field));
				if (own is not null) return own;
				type = script.ClassTag?.Bases.OfType<NamedType>().FirstOrDefault();
			}
			var member = type is null ? null : graph.Universe.MembersOf(type.Name).FirstOrDefault(m => m.Decl.Name == name);
			return member is null ? null : m_allRows.FirstOrDefault(r => r.Native?.DeclaringClass == member.DeclaringClass && r.Name == name);
		}

		if (NodeShape.CallOf(node) is { } call) node = call;
		if (node is FunctionEntry entry) node = entry.Owner;
		var declaration = m_allRows.Where(r => r.Native is null && r.Variable?.Declaration == node).Take(2).ToList();
		if (declaration.Count == 1) return declaration[0];
		return node switch {
			VariableGet get => BindingRow(get.Binding),
			MemberGet get => MemberRow(get.Object, get.Name, false),
			CallNode { Style: CallStyle.Method } c => MemberRow(c.Target, c.Method, true),
			CallNode { Target.Source.Owner: MemberGet member } => MemberRow(member.Object, member.Name, true),
			CallNode c => BindingRow(NodeShape.BindingOf(c.Target.Source)),
			AssignStatement { Targets: [var target] } => target.Binding is { } binding
				? BindingRow(binding) : MemberRow(target.Object, target.Name, false),
			_ => null
		};
	}

	private void RefreshProblems() {
		Problems.Clear();
		if (Session is null) return;
		foreach (var d in Session.Graph.Diagnostics)
			Problems.Add(new ProblemRow(d.Severity.ToString(), $"{NodeShape.TitleOf(InlineRules.Drawn(d.Node))}: {d.Message}", d.Node));
		foreach (var w in Session.Graph.Warnings) Problems.Add(new ProblemRow("Warning", w, null));
	}

	private void Inspect() {
		var before = m_inspecting;
		m_inspecting = true;
		try {
			InspectCore();
		} finally {
			m_inspecting = before;
		}
	}

	private void InspectCore() {
		Tags.Clear();
		InspectorIsScript = false;
		if (SelectedVariable is { Native: { } native, Variable: { } nativeMember }) {
			InspectorIsVariable = false;
			InspectorIsFunction = false;
			ClearSignature();
			ClearTable();
			InspectorTitle = "C++ " + (nativeMember.Kind == VariableKind.Function ? "function " : "field ") + nativeMember.Name;
			InspectorName = native.DeclaringClass + "\n" + nativeMember.Doc.Trim();
			InspectorType = TypeFormatter.Display(nativeMember.Type);
			InspectorDoc = "";
			return;
		}
		if (SelectedVariable?.Variable is { Kind: VariableKind.ModuleLocal } scriptVariable && ScriptInfo.Find(Session!.Graph) is { } script && scriptVariable.Declaration == script.Declaration) {
			InspectorIsScript = true;
			InspectorIsVariable = false;
			InspectorIsFunction = false;
			InspectorTitle = "Script " + script.Table.Name;
			InspectorName = script.Table.Name;
			ScriptClass = script.ClassTag?.Name ?? "";
			ScriptBase = script.ClassTag?.Bases.OfType<NamedType>().FirstOrDefault()?.Name ?? "";
			ScriptBaseItems = BuildBaseItems();
			InspectorDoc = "";
			ClearSignature();
			ClearTable();
			return;
		}
		if (SelectedVariable?.Variable is { } v) {
			InspectorIsVariable = true;
			InspectorIsFunction = v.Kind == VariableKind.Function;
			LoadDefault(v);
			InspectorNullable = v.Type is UnionType u && u.Members.Any(m => m is PrimitiveType { Kind: PrimitiveKind.Nil });
			InspectorCanExport = v.Kind == VariableKind.Field;
			InspectorExported = v.Exported;
			if (v.Declaration is FunctionDecl declared && v.Type is FunctionType fn) LoadSignature(declared, fn);
			else ClearSignature();
			if (v.Kind == VariableKind.Field) LoadTable(v);
			else ClearTable();
			InspectorTitle = v.Kind + " " + v.Name;
			InspectorName = v.Name;
			InspectorType = TypeFormatter.Format(WithoutNil(v.Type));
			TypeOptions = TypeOptionsFor(InspectorType);
			LoadTags(v);
			InspectorDoc = v.Doc;
			return;
		}
		InspectorIsVariable = false;
		InspectorIsFunction = false;
		ClearTable();
		ClearSignature();
		InspectorTitle = "Nothing selected";
		InspectorName = InspectorType = InspectorDoc = "";
	}

	[RelayCommand]
	private void Undo() => Session?.Undo();

	[RelayCommand]
	private void Redo() => Session?.Redo();

	[RelayCommand]
	private void Tidy() => Session?.Tidy(null);

	[RelayCommand]
	private Task Save() {
		if (m_document is null) return Task.CompletedTask;
		var saved = m_document.Save(out var message);
		if (saved) Workspace.AutosaveService.Delete(CurrentUid, ".lua");
		Refresh();
		Status = saved ? "Saved" : message ?? "Save failed";
		OnPropertyChanged(nameof(IsDirty));
		return Task.CompletedTask;
	}

	[RelayCommand]
	private void AddField() {
		var name = FreshName("new_field");
		if (Session?.AddField(name) == true) SelectRow(FindRow("field/" + name));
	}

	[RelayCommand]
	private void AddFunction() => Session?.AddFunction(FreshName("new_function"));

	[RelayCommand]
	private void AddLocal() {
		var name = FreshName("value");
		if (Session?.AddModuleLocal(name) == true) SelectRow(m_allRows.LastOrDefault(r => r.Kind == VariableRowKind.Local && r.Name == name));
	}

	public string FreshName(string stem) {
		var taken = Session?.Graph.Variables.Select(v => v.Name).ToHashSet() ?? [];
		if (!taken.Contains(stem)) return stem;
		for (var i = 2;; i++)
			if (!taken.Contains(stem + i)) return stem + i;
	}

	public void FocusProblem(ProblemRow row) {
		if (row.Node is null || Session is null) return;
		var drawn = InlineRules.Drawn(row.Node);
		if (!drawn.IsExpanded && !Session.Layout.Loose.Contains(drawn) &&
			Session.Layout.Keys.RegionOf(drawn) is { IsModule: true }) {
			Session.ClearSelection();
			SelectRow(RowForNode(drawn));
			return;
		}
		Session.Select([row.Node]);
		FocusRequested?.Invoke(drawn);
	}

	public void CommitName(string name) {
		if (InspectorIsScript && Session is not null && SelectedVariable?.Variable is { } renamed && name.Length > 0 && name != renamed.Name) {
			Session.Apply(g => g.Rename(((LocalDecl)renamed.Declaration).Names[0], name));
			return;
		}
		if (Session is null || SelectedVariable?.Variable is not { } v || name == v.Name || name.Length == 0) return;
		Session.Apply(g => {
			switch (v.Declaration) {
				case LocalDecl local: g.Rename(SelectedVariable?.Binding ?? local.Names[0], name); break;
				case FunctionDecl f when f.IsLocal: g.Rename(f.FunctionBinding!, name); break;
				case AssignStatement: g.RenameMember(v.Name, name); break;
				case FunctionDecl f: g.RenameMember(f.Name, name); break;
			}
		});
	}

	public void CommitType(string typeText) {
		if (Session is null || SelectedVariable?.Variable is not { } v || v.Kind == VariableKind.Function) return;
		LuaType? type = null;
		if (!string.IsNullOrWhiteSpace(typeText)) {
			try {
				type = TypeExprParser.Parse(typeText);
			} catch (TypeSyntaxException e) {
				Status = e.Message;
				return;
			}
		}
		if (type == LuaType.Function && SelectedVariable?.Binding is { Declarer: LocalDecl } binding && v.Kind == VariableKind.ModuleLocal) {
			Session.MakeLocalFunction(binding);
			return;
		}
		if (type is not null && InspectorNullable) type = LuaType.Optional(type);
		Safely(() => Session.SetVariableType(v, SelectedVariable?.Binding, type));
	}

	public void CommitDoc(string doc) {
		if (Session is null || SelectedVariable?.Variable is not { } v) return;
		Session.Apply(g => g.SetDoc(v.Declaration, doc.Length == 0 ? null : doc));
	}

	public const string DesignScript = """
		---@class Door
		local M = {}

		---@type number
		M.open_speed = 2.5
		M.label = "Door"
		local closed = true

		---Opens the door
		function M:init()
		    self.angle = 0
		    if closed then
		        print("closed", self.open_speed)
		    else
		        self.angle = self.angle + 1
		    end
		end

		function M:tick(dt)
		    for i = 1, 3 do
		        self.angle = self.angle + dt * self.open_speed * i
		    end
		    print(math.floor(self.angle), self.label .. "!")
		end

		return M
		""";

	private void LoadDesignData() {
		var universe = TypeUniverse.Build([]);
		var document = LuaDocument.Parse(DesignScript);
		if (!LuaGraph.TryBuild(document, universe, out var graph, out _)) return;
		Open(GraphSession.Open(graph!, null));
		Title = "Door.lua";
	}
}

public sealed partial class LuaGraphEditorViewModel {
	public void CommitParams(string text) {
		if (Session is null || SelectedVariable?.Variable is not { Declaration: FunctionDecl function }) return;
		var changes = new List<(string Name, LuaType? Type)>();
		foreach (var part in TypeText.SplitTopLevel(text)) {
			var colon = part.IndexOf(':');
			var name = (colon < 0 ? part : part[..colon]).Trim().TrimEnd('?');
			if (!function.Entry.Params.Any(p => p.Name == name)) {
				Status = $"{function.Name} has no parameter called {name}";
				return;
			}
			try {
				changes.Add((name, colon < 0 ? null : TypeExprParser.Parse(part[(colon + 1)..].Trim())));
			} catch (TypeSyntaxException e) {
				Status = e.Message;
				return;
			}
		}
		Session.Apply(g => {
			foreach (var (name, type) in changes) g.SetParamType(function, name, type);
		});
	}

	public void CommitReturns(string text) {
		if (Session is null || SelectedVariable?.Variable is not { Declaration: FunctionDecl function }) return;
		var types = new List<LuaType>();
		try {
			foreach (var part in TypeText.SplitTopLevel(text)) types.Add(TypeExprParser.Parse(part));
		} catch (TypeSyntaxException e) {
			Status = e.Message;
			return;
		}
		Session.Apply(g => g.SetReturnTypes(function, types));
	}
}

public sealed partial class LuaGraphEditorViewModel {
	public IReadOnlyList<string> TypeNames {
		get {
			var names = new List<string> { "nil", "boolean", "number", "integer", "string", "table", "function", "any" };
			if (Session is not null) {
				names.AddRange(Session.Universe.Classes.Select(c => c.Name));
				names.AddRange(Session.Universe.Enums.Select(e => e.Name));
			}
			return names.Distinct().Order(StringComparer.Ordinal).ToList();
		}
	}

	public IReadOnlyDictionary<string, string> TypeGroups {
		get {
			var groups = new Dictionary<string, string>();
			foreach (var name in new[] { "nil", "boolean", "number", "integer", "string", "table", "function", "any" }) groups[name] = "Basic";
			if (Session is null) return groups;
			foreach (var name in new[] { "vec2", "vec3", "vec4", "quat", "color" }) groups[name] = "Math";
			foreach (var cls in Session.Universe.Classes)
				groups.TryAdd(cls.Name, cls.Name.Contains("Asset", StringComparison.OrdinalIgnoreCase) ? "Assets" : cls.Name.Contains("Event", StringComparison.OrdinalIgnoreCase) ? "Events" : "Classes");
			foreach (var e in Session.Universe.Enums) groups.TryAdd(e.Name, "Enums");
			return groups;
		}
	}

	private InputPin? DefaultPin(GraphVariable v) => GraphSession.VariableDefault(v, SelectedVariable?.Binding);

	private void LoadDefault(GraphVariable variable) {
		DefaultKind = "none";
		if (variable.Kind == VariableKind.Function) return;
		var pin = DefaultPin(variable);
		var constant = GraphSession.LiteralDefault(pin);
		if (pin?.Source is not null && constant is null) return;
		var type = WithoutNil(Session!.Universe.ResolveAliases(variable.Type));
		if (type == LuaType.Any || type == LuaType.Unknown) type = constant?.ValueType ?? LuaType.Nil;
		switch (type) {
			case PrimitiveType { Kind: PrimitiveKind.Integer }:
				DefaultKind = "integer";
				DefaultInteger = constant is { Kind: ConstantKind.Number } && TryDefaultInteger(constant.Raw, out var integer)
					? integer.ToString(CultureInfo.InvariantCulture) : "0";
				break;
			case PrimitiveType { Kind: PrimitiveKind.Number }:
				DefaultKind = "number";
				DefaultNumber = constant is { Kind: ConstantKind.Number } &&
					float.TryParse(constant.Raw, NumberStyles.Float, CultureInfo.InvariantCulture, out var number) && float.IsFinite(number) ? number : 0;
				break;
			case PrimitiveType { Kind: PrimitiveKind.Boolean }:
				DefaultKind = "boolean";
				DefaultBool = constant?.Raw == "true";
				break;
			case PrimitiveType { Kind: PrimitiveKind.String }:
				DefaultKind = "string";
				DefaultText = constant is { Kind: ConstantKind.String } ? ReadDefaultString(pin, constant) : "";
				break;
		}
	}

	private static string ReadDefaultString(InputPin? pin, LuaConstant constant) {
		if (pin?.Origin is LiteralExpressionSyntax original && original.Token.Text == constant.Raw && original.Token.Value is string text)
			return text;
		var statement = LuaDocument.Parse("return " + constant.Raw).Root.Statements.Statements.FirstOrDefault();
		return statement is ReturnStatementSyntax ret && ret.Expressions.FirstOrDefault() is LiteralExpressionSyntax literal &&
			literal.Token.Value is string decoded ? decoded : GraphSession.DisplayText(constant);
	}

	private static bool TryDefaultInteger(string text, out long value) => text.StartsWith("0x", StringComparison.OrdinalIgnoreCase)
		? long.TryParse(text.AsSpan(2), NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out value)
		: long.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out value);

	private void CommitDefault(LuaConstant value) {
		if (m_inspecting || Session is null || SelectedVariable?.Variable is not { } variable) return;
		if (GraphSession.LiteralDefault(DefaultPin(variable)) == value) return;
		Session.SetVariableDefault(variable, SelectedVariable.Binding, value);
	}

	partial void OnDefaultNumberChanged(float value) {
		if (float.IsFinite(value)) CommitDefault(LuaConstant.Float(value));
	}

	partial void OnDefaultIntegerChanged(string value) {
		if (m_inspecting) return;
		if (TryDefaultInteger(value, out var number)) CommitDefault(LuaConstant.Integer(number));
		else Status = "enter a whole number between " + long.MinValue + " and " + long.MaxValue;
	}

	partial void OnDefaultBoolChanged(bool value) => CommitDefault(value ? LuaConstant.True : LuaConstant.False);

	partial void OnDefaultTextChanged(string value) => CommitDefault(LuaConstant.String(value));

	partial void OnInspectorNullableChanged(bool value) {
		if (m_inspecting || Session is null || SelectedVariable?.Variable is not { } v || v.Kind == VariableKind.Function) return;
		if (v.Declaration is LocalDecl local && SelectedVariable.Binding != local.Names[0]) return;
		var members = v.Type is UnionType u ? u.Members.ToList() : [v.Type];
		var hasNil = members.Any(m => m is PrimitiveType { Kind: PrimitiveKind.Nil });
		if (value == hasNil) return;
		var next = value ? LuaType.Optional(v.Type) : LuaType.Union(members.Where(m => m is not PrimitiveType { Kind: PrimitiveKind.Nil }).ToArray());
		Session.Apply(g => g.SetDeclaredType(v.Declaration, next));
	}

	partial void OnInspectorExportedChanged(bool value) {
		if (m_inspecting || Session is null || SelectedVariable?.Variable is not { Kind: VariableKind.Field } v || v.Exported == value) return;
		var name = value ? v.Name.TrimStart('_') : "_" + v.Name;
		if (name.Length == 0) return;
		Session.Apply(g => g.RenameMember(v.Name, name));
	}
}

public sealed partial class LuaGraphEditorViewModel {
	partial void OnInspectorNameChanged(string value) {
		if (!m_inspecting) CommitName(value);
	}

	partial void OnInspectorTypeChanged(string value) {
		if (!m_inspecting) CommitType(value);
	}

	partial void OnInspectorDocChanged(string value) {
		if (!m_inspecting) CommitDoc(value);
	}

	partial void OnInspectorParamsChanged(string value) {
		if (!m_inspecting) CommitParams(value);
	}

	partial void OnInspectorReturnsChanged(string value) {
		if (!m_inspecting) CommitReturns(value);
	}
}
