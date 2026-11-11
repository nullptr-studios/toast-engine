using System;
using System.Collections.Generic;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Controls.Primitives;
using Avalonia.Threading;
using editor.Components.Elements;
using Avalonia.Interactivity;
using editor.LuaEditor.Canvas;
using editor.LuaEditor.Editing;
using editor.LuaEditor.Graph;
using editor.LuaEditor.Layout;
using editor.Workspace;

namespace editor.LuaEditor;

public partial class LuaGraphEditorView : UserControl {
	private SearchRequest? m_search;
	private ConstantEditRequest? m_constant;
	private GroupEditRequest? m_group;

	public LuaGraphEditorView() {
		InitializeComponent();
		GraphView.SearchRequested += OpenSearch;
		GraphView.ConstantEditRequested += OpenConstantEditor;
		GraphView.GroupEditRequested += OpenGroupEditor;
		GraphView.NodeMenuRequested += OpenNodeMenu;
		GraphView.ChoiceMenuRequested += OpenChoiceMenu;
		GraphView.RenameRequested += (box, text, commit) => OpenTextEditor(box, text, commit);
		GraphView.FindRequested += () => OpenSearch(new SearchRequest(default, new Point(80, 60), null, null, null, false));
		GraphView.SaveRequested += () => Vm?.SaveCommand.Execute(null);

		Rows.PointerMoved += OnVariableMoved;
		Rows.PointerReleased += OnRowReleased;
		SearchBox.TextChanged += (_, _) => FillSearch();
		SearchBox.KeyDown += OnSearchKey;
		SearchPanel.GetObservable(IsKeyboardFocusWithinProperty).Subscribe(new Avalonia.Reactive.AnonymousObserver<bool>(inside => {
			if (!inside && SearchPanel.IsVisible) Dispatcher.UIThread.Post(() => { if (!SearchPanel.IsKeyboardFocusWithin) CloseSearch(refocus: false); });
		}));
SearchList.DoubleTapped += (_, _) => CommitSearch();
		ConstantBox.KeyDown += OnConstantKey;
		ConstantBox.LostFocus += (_, _) => CloseConstantEditor(commit: false);
		GroupBox.KeyDown += OnGroupKey;
		GroupBox.LostFocus += (_, _) => CloseGroupEditor(commit: true);
		DataContextChanged += (_, _) => {
			WireTree();
			if (Vm is { } vm) vm.FocusRequested += node => GraphView.FrameNodes([node]);
		};
	}

	private LuaGraphEditorViewModel? Vm => DataContext as LuaGraphEditorViewModel;

	private const double FoldedDetailsHeight = 46;
	private bool m_detailsFolded;
	private double m_detailsHeight;
	private Avalonia.Threading.DispatcherTimer? m_detailsTimer;

	private void OnInspectorHeaderPressed(object? sender, PointerPressedEventArgs e) {
		var rows = LeftPanel.RowDefinitions;
		var details = rows[2];
		var script = rows[0];
		m_detailsTimer?.Stop();
		var current = DetailsCard.Bounds.Height;
		m_detailsFolded = !m_detailsFolded;
		InspectorChevron.Kind = m_detailsFolded ? Lucide.Avalonia.LucideIconKind.ChevronRight : Lucide.Avalonia.LucideIconKind.ChevronDown;
		details.MinHeight = 0;
		script.Height = new GridLength(1, GridUnitType.Star);
		if (m_detailsFolded) {
			m_detailsHeight = current;
			m_detailsTimer = editor.Components.Elements.CollapseAnimator.Run(current, FoldedDetailsHeight, h => details.Height = new GridLength(h), () => details.Height = new GridLength(FoldedDetailsHeight));
		} else {
			var target = Math.Max(m_detailsHeight, 140);
			m_detailsTimer = editor.Components.Elements.CollapseAnimator.Run(current, target, h => details.Height = new GridLength(h), () => {
				script.Height = new GridLength(3, GridUnitType.Star);
				details.Height = new GridLength(2, GridUnitType.Star);
			});
		}
		e.Handled = true;
	}

	private void OpenSearch(SearchRequest request) {
		if (Vm?.Session is null) return;
		CloseConstantEditor(commit: false);
		m_search = request;
		SearchPanel.Margin = new Thickness(Math.Clamp(request.Screen.X, 0, Math.Max(0, Bounds.Width - 640)), Math.Clamp(request.Screen.Y, 0, Math.Max(0, Bounds.Height - 480)), 0, 0);
		SearchPanel.IsVisible = true;
		SearchBox.Text = "";
		m_searchOpen.Clear();
		m_searchOpen.Add("This script");
		if ((request.From is not null || request.Into is not null) && Vm.Session.Search("", request.From?.Type, request.Into?.Expected, request.StatementsOnly).Count <= 40)
			foreach (var e in Vm.Session.Search("", request.From?.Type, request.Into?.Expected, request.StatementsOnly)) {
				m_searchOpen.Add(SearchRow.GroupOf(e));
				m_searchOpen.Add(SearchRow.GroupOf(e) + "/" + SearchRow.CategoryOf(e));
			}
		FillSearch();
		SearchBox.Focus();
	}

	private readonly HashSet<string> m_searchOpen = [];

	private void FillSearch(string? keepKey = null) {
		if (m_search is null || Vm?.Session is not { } session) return;
		var producing = m_search.From?.Type;
		var expecting = m_search.Into?.Expected;
		var text = SearchBox.Text ?? "";
		var all = session.Search(text, producing, expecting, m_search.StatementsOnly).ToList();
		var searching = text.Trim().Length > 0;
		var rows = SearchRow.Tree(searching ? all.Take(120).ToList() : all, m_searchOpen, searching);
		SearchList.ItemsSource = rows;
		var at = keepKey is null ? -1 : rows.FindIndex(r => r.Key == keepKey);
		SearchList.SelectedIndex = at >= 0 ? at : rows.FindIndex(r => !r.IsGroup) is var first and >= 0 && searching ? first : 0;
	}

	private void ToggleSearchGroup(SearchRow group, bool? open = null) {
		if (!group.IsGroup || (SearchBox.Text ?? "").Trim().Length > 0) return;
		var want = open ?? !m_searchOpen.Contains(group.Key);
		if (want) m_searchOpen.Add(group.Key);
		else m_searchOpen.Remove(group.Key);
		FillSearch(group.Key);
	}

	private void OnSearchKey(object? sender, KeyEventArgs e) {
		switch (e.Key) {
			case Key.Escape:
				CloseSearch();
				e.Handled = true;
				break;
			case Key.Right when SearchList.SelectedItem is SearchRow { IsGroup: true } closed:
				ToggleSearchGroup(closed, true);
				e.Handled = true;
				break;
			case Key.Left when SearchList.SelectedItem is SearchRow { IsGroup: true } open:
				ToggleSearchGroup(open, false);
				e.Handled = true;
				break;
			case Key.Enter:
				CommitSearch();
				e.Handled = true;
				break;
			case Key.Down:
				SearchList.SelectedIndex = Math.Min(SearchList.SelectedIndex + 1, SearchList.ItemCount - 1);
				SearchList.ScrollIntoView(SearchList.SelectedItem!);
				e.Handled = true;
				break;
			case Key.Up:
				SearchList.SelectedIndex = Math.Max(SearchList.SelectedIndex - 1, 0);
				SearchList.ScrollIntoView(SearchList.SelectedItem!);
				e.Handled = true;
				break;
		}
	}

	private void CommitSearch() {
		if (SearchList.SelectedItem is SearchRow { IsGroup: true } group) {
			ToggleSearchGroup(group);
			return;
		}
		if (m_search is { } request && (SearchList.SelectedItem as SearchRow)?.Entry is { } entry && Vm?.Session is { } session)
			session.Create(entry, request.World, request.From, request.Into, request.Insert);
		CloseSearch();
	}

	private void CloseSearch(bool refocus = true) {
		m_search = null;
		SearchPanel.IsVisible = false;
		if (refocus) GraphView.Focus();
	}

	private void OnAction(object? sender, RoutedEventArgs e) {
		if ((sender as Control)?.Tag is string name && Enum.TryParse<CanvasAction>(name, out var action)) {
			GraphView.Execute(action);
			GraphView.Focus();
		}
	}

	private void OnAlignTop(object? sender, RoutedEventArgs e) => GraphView.Execute(CanvasAction.AlignTop);

	private void OnAlignLeft(object? sender, RoutedEventArgs e) => GraphView.Execute(CanvasAction.AlignLeft);

	private void OnStraighten(object? sender, RoutedEventArgs e) => GraphView.Execute(CanvasAction.Straighten);

	private void OnFrameAll(object? sender, RoutedEventArgs e) => GraphView.FrameAll();

	private void OnFindClick(object? sender, RoutedEventArgs e) => OpenSearch(new SearchRequest(default, new Point(80, 20), null, null, null, false));

	private void OpenConstantEditor(ConstantEditRequest request) {
		m_constant = request;
		var height = Math.Max(request.Screen.Height, 20);
		ConstantBox.Margin = new Thickness(request.Screen.X, request.Screen.Y - (height - request.Screen.Height) / 2, 0, 0);
		ConstantBox.Width = Math.Max(request.Screen.Width, 40);
		ConstantBox.Height = height;
		ConstantBox.VerticalContentAlignment = VerticalAlignment.Center;
		ConstantBox.Text = request.Text;
		ConstantBox.IsVisible = true;
		// the opening click would otherwise steal focus back
		Dispatcher.UIThread.Post(() => {
			ConstantBox.Focus();
			ConstantBox.SelectAll();
		}, DispatcherPriority.Input);
	}

	private void OnConstantKey(object? sender, KeyEventArgs e) {
		if (e.Key == Key.Enter) CloseConstantEditor(commit: true);
		else if (e.Key == Key.Escape) CloseConstantEditor(commit: false);
		else return;
		e.Handled = true;
	}

	private Action<string>? m_textCommit;

	private void OpenTextEditor(Avalonia.Rect screen, string text, Action<string> commit) {
		m_textCommit = commit;
		m_constant = new ConstantEditRequest(null!, screen, text);
		ConstantBox.Margin = new Thickness(screen.X, screen.Y, 0, 0);
		ConstantBox.Width = Math.Max(220, screen.Width);
		ConstantBox.Text = text;
		ConstantBox.IsVisible = true;
		ConstantBox.Focus();
		ConstantBox.SelectAll();
	}

	private void OpenChoiceMenu(InputPin pin, Avalonia.Rect screen, IReadOnlyList<GraphSession.PinChoice> choices) {
		var menu = new ContextMenu();
		foreach (var choice in choices) {
			var item = new MenuItem { Header = choice.Label, FontWeight = choice.IsCurrent ? Avalonia.Media.FontWeight.Bold : Avalonia.Media.FontWeight.Normal };
			var apply = choice.Apply;
			item.Click += (_, _) => apply();
			menu.Items.Add(item);
		}
		menu.Placement = PlacementMode.Bottom;
		menu.PlacementRect = new PixelRect((int)screen.X, (int)screen.Y, (int)screen.Width, (int)screen.Height).ToRect(1);
		menu.Open(GraphView);
	}

	private static string FieldText(TableField field) => field.Kind switch { TableFieldKind.Named => "field " + field.Name, TableFieldKind.Keyed => "entry [" + NodeShape.KeyText(field.Key!) + "]", _ => "item " + field.Value.Name };

	private void OpenNodeMenu(GraphNode node, Point screen) {
		if (Vm?.Session is not { } session) return;
		if (!session.Selection.Contains(node)) session.Select([node]);
		var menu = new ContextMenu();
		var call = NodeShape.CallOf(node);
		if (call is not null && session.CanSwitchCallStyle(call)) {
			var method = call.Style == CallStyle.Method;
			var name = call.Method ?? (call.Target.Source?.Owner as MemberGet)?.Name ?? "fn";
			var item = new MenuItem { Header = method ? $"Write as a.{name}(a)" : $"Write as a:{name}()" };
			item.Click += (_, _) => session.SetCallStyle(call, method ? CallStyle.Function : CallStyle.Method);
			menu.Items.Add(item);
		}
		if (node is TableNode table) {
			var kind = GraphSession.EntryKindOf(table);
			var add = new MenuItem { Header = kind switch { TableFieldKind.Positional => "Add item", TableFieldKind.Keyed => "Add entry  [key] = value", _ => "Add field  name = value" } };
			add.Click += (_, _) => session.AddTableEntry(table, kind);
			menu.Items.Add(add);
			foreach (var entry in table.Fields.Where(f => f.Kind != TableFieldKind.Positional).ToList()) {
				var target = entry;
				var rename = new MenuItem { Header = entry.Kind == TableFieldKind.Named ? $"Rename field {entry.Name}..." : $"Change key [{NodeShape.KeyText(entry.Key!)}]..." };
				rename.Click += (_, _) => {
					var box = new Avalonia.Rect(screen.X, screen.Y, 220, 24);
					if (target.Kind == TableFieldKind.Named) OpenTextEditor(box, target.Name ?? "", text => session.RenameTableField(table, target, text.Trim()));
					else OpenTextEditor(box, NodeShape.KeyText(target.Key!), text => session.SetTableKey(table, target, text));
				};
				menu.Items.Add(rename);
			}
			for (var index = table.Fields.Count - 1; index >= 0; index--) {
				var at = index;
				var removeEntry = new MenuItem { Header = $"Remove {FieldText(table.Fields[at])}" };
				removeEntry.Click += (_, _) => session.RemoveTableEntry(table, at);
				menu.Items.Add(removeEntry);
			}
			menu.Items.Add(new Separator());
		}
		if (node is MemberGet { Object.Source: not { Binding: { } } } || node is AssignStatement { Targets: [{ Kind: TargetKind.Member }] }) {
			var access = node;
			var current = node is MemberGet member ? member.Name : ((AssignStatement)node).Targets[0].Name ?? "";
			var renameAccess = new MenuItem { Header = $"Rename field {current}..." };
			renameAccess.Click += (_, _) => OpenTextEditor(new Avalonia.Rect(screen.X, screen.Y, 220, 24), current, text => session.RenameAccess(access, text.Trim()));
			menu.Items.Add(renameAccess);
		}
		if (call is not null) {
			var add = new MenuItem { Header = "Add argument" };

			add.Click += (_, _) => session.AddCallArgument(call);
			menu.Items.Add(add);
			if (call.Args.Count > 0) {
				var remove = new MenuItem { Header = "Remove last argument" };
				remove.Click += (_, _) => session.RemoveLastCallArgument(call);
				menu.Items.Add(remove);
			}
		}
		if (node is Statement statement) {
			var current = string.Join(" ", statement.LeadingLines.Where(l => !l.TrimStart().StartsWith("---@", StringComparison.Ordinal)).Select(l => l.TrimStart('-', ' ')));
			var edit = new MenuItem { Header = "Edit comment..." };
			edit.Click += (_, _) => OpenTextEditor(new Avalonia.Rect(screen.X, screen.Y, 260, 24), current, text => session.SetComment(statement, text.Length == 0 ? null : text));
			menu.Items.Add(edit);
		}
		var copy = new MenuItem { Header = "Copy", InputGesture = new KeyGesture(Key.C, KeyModifiers.Control) };
		copy.Click += (_, _) => GraphView.Execute(CanvasAction.Copy);
		menu.Items.Add(copy);
		var paste = new MenuItem { Header = "Paste", InputGesture = new KeyGesture(Key.V, KeyModifiers.Control) };
		paste.Click += (_, _) => GraphView.Execute(CanvasAction.Paste);
		menu.Items.Add(paste);
		var group = new MenuItem { Header = "Group into a function", InputGesture = new KeyGesture(Key.G) };
		group.Click += (_, _) => session.GroupSelectionAsFunction();
		menu.Items.Add(group);
		var duplicate = new MenuItem { Header = "Duplicate" };
		duplicate.Click += (_, _) => session.Duplicate();
		menu.Items.Add(duplicate);
		var delete = new MenuItem { Header = "Delete" };
		delete.Click += (_, _) => session.Delete(session.Selection.ToList());
		menu.Items.Add(delete);
		menu.Open(GraphView);
	}

	private void CloseConstantEditor(bool commit) {
		if (m_constant is not { } request) return;
		m_constant = null;
		ConstantBox.IsVisible = false;
		var textCommit = m_textCommit;
		m_textCommit = null;
		if (commit && textCommit is not null) textCommit(ConstantBox.Text ?? "");
		else if (commit && Vm?.Session is { } session) session.SetConstant(request.Pin, ConstantBox.Text ?? "");
		GraphView.Focus();
	}

	private void OpenGroupEditor(GroupEditRequest request) {
		m_group = request;
		GroupBox.Margin = new Thickness(request.Screen.X + 6, request.Screen.Y, 0, 0);
		GroupBox.Width = Math.Max(160, request.Screen.Width - 12);
		GroupBox.Text = request.Group.Title;
		GroupBox.IsVisible = true;
		GroupBox.Focus();
		GroupBox.SelectAll();
	}

	private void OnGroupKey(object? sender, KeyEventArgs e) {
		if (e.Key == Key.Enter) CloseGroupEditor(commit: true);
		else if (e.Key == Key.Escape) CloseGroupEditor(commit: false);
		else return;
		e.Handled = true;
	}

	private void CloseGroupEditor(bool commit) {
		if (m_group is not { } request) return;
		m_group = null;
		GroupBox.IsVisible = false;
		if (commit && !string.IsNullOrWhiteSpace(GroupBox.Text)) {
			request.Group.Title = GroupBox.Text.Trim();
			Vm?.Session?.CommitLayout();
		}
		GraphView.InvalidateVisual();
		GraphView.Focus();
	}

	private LuaGraphEditorViewModel? m_treeVm;

	private void WireTree() {
		if (m_treeVm is not null) {
			m_treeVm.Variables.CollectionChanged -= OnRowsChanged;
			m_treeVm.PropertyChanged -= OnTreeVmChanged;
		}
		m_treeVm = Vm;
		if (m_treeVm is null) return;
		m_treeVm.Variables.CollectionChanged += OnRowsChanged;
		m_treeVm.PropertyChanged += OnTreeVmChanged;
		RefreshConnector();
	}

	private void OnRowsChanged(object? sender, System.Collections.Specialized.NotifyCollectionChangedEventArgs e) => RefreshConnector();

	private void OnTreeVmChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e) {
		if (e.PropertyName == nameof(LuaGraphEditorViewModel.SelectedVariable)) RefreshConnector();
	}

	private void RefreshConnector() {
		if (m_treeVm is not { } vm) return;
		Connector.Rows = vm.Variables.ToList();
		Connector.SelectedIndex = vm.SelectedVariable is { } selected ? vm.Variables.IndexOf(selected) : -1;
	}

	private readonly HierarchyRowPress<VariableRow> m_rowPress = new();

	private void OnRowPressed(object? sender, PointerPressedEventArgs e) {
		if (sender is not Control control || control.DataContext is not VariableRow row || Vm is not { } vm) return;
		var properties = e.GetCurrentPoint(control).Properties;
		m_rowPress.Clear();
		if (properties.IsRightButtonPressed) {
			if (row.Kind is VariableRowKind.Field or VariableRowKind.Tick && row.Variable is not null)
				OpenRowMenu(control, row);
			return;
		}
		if (!properties.IsLeftButtonPressed) return;
		var wasSelected = ReferenceEquals(vm.SelectedVariable, row);
		vm.SelectRow(row);
		if (row.Kind == VariableRowKind.Tick) {
			if (row.Variable?.Declaration is FunctionDecl tick) GraphView.ShowFunction(tick);
			else if (vm.CanEdit && vm.Session is { } session && ScriptInfo.Find(session.Graph) is not null &&
				session.AddFunction(row.Id["tick/".Length..]) && vm.FindRow(row.Id) is { Variable.Declaration: FunctionDecl created } newRow) {
				vm.SelectRow(newRow);
				GraphView.ShowFunction(created);
			}
			return;
		}
		if (row.Kind == VariableRowKind.Local && e.ClickCount >= 2 && row.Variable?.Declaration is FunctionDecl local) {
			GraphView.ShowFunction(local);
			return;
		}
		m_rowPress.Begin(row, e, this, wasSelected);
	}

	private void OnRowReleased(object? sender, PointerReleasedEventArgs e) {
		if (m_rowPress.TakeClick(e) is not { } click) return;
		if (click.Row.Kind == VariableRowKind.Method && click.Row.Variable?.Declaration is FunctionDecl function)
			GraphView.ShowFunction(function);
		else if (click.WasSelected) Vm?.SetSection(click.Row);
	}

	private void OpenRowMenu(Control control, VariableRow row) {
		if (Vm is not { CanEdit: true, Session: { } session } vm || row.Variable is not { } variable) return;
		vm.SelectRow(row);
		var items = new List<MenuItem>();
		void Item(string title, Action action) {
			var item = new MenuItem { Header = title };
			item.Click += (_, _) => action();
			items.Add(item);
		}
		if (row.Kind == VariableRowKind.Field && variable.Declaration is AssignStatement field) {
			Item("Get", () => GraphView.CreateHierarchyNode(row, set: false));
			Item("Set", () => GraphView.CreateHierarchyNode(row, set: true));
			Item("Rename", () => OpenTextEditor(new Avalonia.Rect(12, 70, 230, 28), variable.Name, name => vm.CommitName(name.Trim())));
			Item("Duplicate", () => session.DuplicateField(field, vm.FreshName(variable.Name + "_copy")));
		}
		Item("Delete", () => session.Delete([variable.Declaration]));
		var menu = new ContextMenu { ItemsSource = items };
		menu.Open(control);
	}

	private async void OnVariableMoved(object? sender, PointerEventArgs e) {
		if (m_rowPress.TakeDrag(e, this) is not { Row.CanDrag: true } drag || Vm is not { CanEdit: true, Session: { } session }) return;
		var data = new DataTransfer();
		data.Add(DataTransferItem.Create(VariableDrag.Format, new HierarchyDrag(session, drag.Row)));
		await DragDrop.DoDragDropAsync(drag.Args, data, DragDropEffects.Copy);
	}

	private void OnProblemSelected(object? sender, SelectionChangedEventArgs e) {
		if (e.AddedItems.OfType<ProblemRow>().FirstOrDefault() is { } row) Vm?.FocusProblem(row);
	}
}
