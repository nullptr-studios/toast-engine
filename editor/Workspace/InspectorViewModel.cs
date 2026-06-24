using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Dock.Model.Mvvm.Controls;
using editor.Engine;
using Proto.Events;

namespace editor.Workspace;

public partial class InspectorViewModel : Tool {
	private static readonly string[] Palette =
		["Red", "Green", "Blue", "Magenta", "Orange", "Yellow", "Cyan", "Beige"];

	// ReSharper disable once PrivateFieldCanBeConvertedToLocalVariable
	private readonly Listener m_listener;

	private readonly Dictionary<string, FieldVM> m_fieldByParam = new();
	private InspectorState? m_state;
	private string? m_uid;
	private string? m_builtUid;
	private string? m_builtType;
	private bool m_suppressEnabled;

	[ObservableProperty] private string m_name = "";
	[ObservableProperty] private string m_typeDisplay = "";
	[ObservableProperty] private bool m_enabled = true;
	[ObservableProperty] private string m_iconColorKey = "TextMuted";
	[ObservableProperty] private bool m_hasSelection;

	[ObservableProperty] private bool m_isEditingName;
	[ObservableProperty] private string m_nameDraft = "";

	[ObservableProperty] private string m_filterText = "";

	public ObservableCollection<ClassCardVM> Cards { get; } = [];

	public InspectorViewModel() {
		m_listener = new Listener();

		// engine streams the focused node's values at ~12fps; ignore frames for a different node
		m_listener.Subscribe<InspectorContent>(e => Dispatcher.UIThread.Post(() => {
			if (!HasSelection || e.Uid != m_builtUid) return;

			if (!IsEditingName) Name = e.Name;
			SetEnabledSuppressed(e.Enabled);

			foreach (var p in e.Parameters) {
				if (!m_fieldByParam.TryGetValue(p.Name, out var vm)) continue;
				// don't clobber a value the user is actively editing
				if ((DateTime.UtcNow - vm.LastEdit).TotalMilliseconds < 250) continue;
				vm.ApplyEngineString(p.Value);
			}

			Engine.Log.Warn("Updated log");
		}));

		HierarchyViewModel.SelectionChanged += OnSelectionChanged;
		if (HierarchyViewModel.Current?.SelectedNode is { } sel) OnSelectionChanged(sel);
	}

	partial void OnFilterTextChanged(string value) => ApplyFilter();

	partial void OnEnabledChanged(bool value) {
		if (m_suppressEnabled || m_uid is null) return;
		Events.Send(new NodeEnabled { Node = m_uid, Enabled = value });
		WorkspaceState.MarkModified();
	}

	private void SetEnabledSuppressed(bool value) {
		m_suppressEnabled = true;
		Enabled = value;
		m_suppressEnabled = false;
	}

	private void OnSelectionChanged(HierarchyElement? node) {
		Dispatcher.UIThread.Post(() => {
			if (node is null) {
				HasSelection = false;
				Cards.Clear();
				m_fieldByParam.Clear();
				m_builtUid = null;
				m_builtType = null;
				m_uid = null;
				return;
			}

			m_uid = node.Uid;
			Name = node.Name;
			TypeDisplay = node.Type;
			IconColorKey = ReflectionDatabase.ResolveColor(node.Type);
			SetEnabledSuppressed(node.Enabled);
			IsEditingName = false;
			HasSelection = true;

			// only rebuild the card structure when the node or its type actually changes
			if (node.Uid != m_builtUid || node.Type != m_builtType) Rebuild(node);
		});
	}

	private void Rebuild(HierarchyElement node) {
		Cards.Clear();
		m_fieldByParam.Clear();
		if (ReflectionDatabase.Nodes is null) return;

		m_state = InspectorState.Load(node.Uid);
		var colorCounter = 0;

		// walk the inheritance chain most-derived -> base
		var current = Bare(node.Type);
		while (ReflectionDatabase.Nodes.TryGetValue(current, out var info)) {
			Cards.Add(BuildCard(info, ref colorCounter));
			if (info.Parent is null) break;
			current = Bare(info.Parent.Name);
		}

		m_builtUid = node.Uid;
		m_builtType = node.Type;
		ApplyFilter();
	}

	private ClassCardVM BuildCard(NodeInfo info, ref int colorCounter) {
		// class cards show the bare type name; the namespaced form lives in the header label only
		var typeName = info.Name;
		var card = new ClassCardVM(typeName, ReflectionDatabase.ResolveColor(typeName), $"class:{typeName}", m_state!);

		foreach (var f in info.GlobalFields) AddField(card.Fields, f);

		foreach (var g in info.Groups) {
			var colorKey = Palette[colorCounter++ % Palette.Length];
			var group = new GroupVM(g.Name, colorKey, $"group:{typeName}/{g.Name}", m_state!);
			foreach (var f in g.Fields) AddField(group.Fields, f);

			foreach (var sg in g.Subgroups) {
				var sub = new SubgroupVM(sg.Name, $"sub:{typeName}/{g.Name}/{sg.Name}", m_state!);
				foreach (var f in sg.Fields) AddField(sub.Fields, f);
				group.Subgroups.Add(sub);
			}

			card.Groups.Add(group);
		}

		// TODO: function reflection needed to expose [[Button]] void fn(void) methods -> card.Buttons
		return card;
	}

	private void AddField(ObservableCollection<FieldVM> target, FieldInfo info) {
		if (ReflectionDatabase.HasAttr(info.Attributes, "Hidden")) return;
		var vm = new FieldVM(info);
		vm.Edited += OnFieldEdited;
		target.Add(vm);
		m_fieldByParam[vm.ParameterName] = vm;
	}

	private void OnFieldEdited(FieldVM field, string value) {
		Events.Send(new NodeChangeParam { Parameter = field.ParameterName, Value = value });
		WorkspaceState.MarkModified();
	}

	private void ApplyFilter() {
		foreach (var card in Cards) card.ApplyFilter(FilterText);
	}

	// header rename
	[RelayCommand]
	private void BeginRename() {
		if (!HasSelection) return;
		NameDraft = Name;
		IsEditingName = true;
	}

	public void CommitRename() {
		if (!IsEditingName) return;
		IsEditingName = false;
		var n = NameDraft.Trim();
		if (n.Length == 0 || n == Name || m_uid is null) return;

		Events.Send(new NodeChangeName { Node = m_uid, Name = n });
		WorkspaceState.MarkModified();
	}

	public void CancelRename() => IsEditingName = false;

	// toast::Camera -> Camera
	private static string Bare(string typeName) {
		var i = typeName.LastIndexOf(':');
		return i >= 0 ? typeName[(i + 1)..] : typeName;
	}
}
