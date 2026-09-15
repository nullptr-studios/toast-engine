using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Dock.Model.Mvvm.Controls;
using editor.Engine;
using Proto.Events;

namespace editor.Workspace;

public partial class InspectorViewModel : Tool, IDisposable, IInspectorClipboardHost {
	private static readonly string[] Palette =
		["Red", "Green", "Blue", "Magenta", "Orange", "Yellow", "Cyan", "Beige"];

	private static readonly HashSet<string> s_reservedNames = ["root", "world", "global"];
	private readonly DispatcherTimer m_editCommitTimer = new() { Interval = TimeSpan.FromMilliseconds(450) };

	private readonly Dictionary<string, FieldVM> m_fieldByParam = new();

	// ReSharper disable once PrivateFieldCanBeConvertedToLocalVariable
	private readonly Listener m_listener;
	private readonly List<ClassCardVM> m_luaCards = [];
	private uint m_builtLuaVersion;
	private string? m_builtType;
	private string? m_builtUid;
	private string? m_editField;
	private bool m_isPasteBatch;
	private ulong m_editTransaction;
	private ulong m_editWorkspaceHandle;
	[ObservableProperty] private bool m_enabled = true;

	[ObservableProperty] private string m_filterText = "";
	[ObservableProperty] private bool m_hasSelection;
	[ObservableProperty] private string m_iconColorKey = "TextMuted";

	[ObservableProperty] private bool m_isEditingName;
	[ObservableProperty] private Bitmap? m_largeIcon;

	[ObservableProperty] private string m_name = "";
	[ObservableProperty] private string m_nameDraft = "";
	private ulong m_nextEditTransaction = 1;
	private InspectorState? m_state;
	private bool m_suppressEnabled;
	[ObservableProperty] private string m_typeDisplay = "";
	private string? m_uid;

	public InspectorViewModel() {
		if (Design.IsDesignMode) {
			m_listener = null!;
			try {
				const string fallbackJson = @"[
  {
    ""attributes"": {
      ""Icon"": [
        ""DirectionalLight""
      ]
    },
    ""functions"": {},
    ""global_fields"": [],
    ""groups"": [],
    ""name"": ""DirectionalLight"",
    ""namespace"": ""toast"",
    ""parent"": {
      ""name"": ""Light"",
      ""namespace"": null
    },
    ""source_file"": ""toast/world/directional_light.hpp""
  },
  {
    ""attributes"": {
      ""Hidden"": [],
      ""Icon"": [
        ""PointLight""
      ]
    },
    ""functions"": {},
    ""global_fields"": [],
    ""groups"": [],
    ""name"": ""Light"",
    ""namespace"": ""toast"",
    ""parent"": {
      ""name"": ""Node3D"",
      ""namespace"": null
    },
    ""source_file"": ""toast/world/light.hpp""
  },
  {
    ""attributes"": {
      ""Color"": [
        ""Red""
      ],
      ""Icon"": [
        ""BoxMesh""
      ]
    },
    ""functions"": {},
    ""global_fields"": [
      {
        ""attributes"": {
          ""Unit"": [
            ""m""
          ]
        },
        ""default"": null,
        ""field_type"": ""vec3_t"",
        ""is_array"": false,
        ""name"": ""m_position"",
        ""typename"": ""glm::vec3""
      },
      {
        ""attributes"": {
          ""Unit"": [
            ""°""
          ]
        },
        ""default"": null,
        ""field_type"": ""quaternion_t"",
        ""is_array"": false,
        ""name"": ""m_rotation"",
        ""typename"": ""glm::quat""
      },
      {
        ""attributes"": {},
        ""default"": null,
        ""field_type"": ""vec3_t"",
        ""is_array"": false,
        ""name"": ""m_scale"",
        ""typename"": ""glm::vec3""
      }
    ],
    ""groups"": [],
    ""name"": ""Node3D"",
    ""namespace"": ""toast"",
    ""parent"": {
      ""name"": ""Node"",
      ""namespace"": null
    },
    ""source_file"": ""toast/world/node_3d.hpp""
  },
  {
    ""attributes"": {
      ""Icon"": [
        ""Circle""
      ]
    },
    ""functions"": {},
    ""global_fields"": [
      {
        ""attributes"": {
          ""Hidden"": []
        },
        ""default"": null,
        ""field_type"": ""uid_t"",
        ""is_array"": false,
        ""name"": ""m_uid"",
        ""typename"": ""UID""
      },
      {
        ""attributes"": {
          ""Hidden"": []
        },
        ""default"": null,
        ""field_type"": ""string_t"",
        ""is_array"": false,
        ""name"": ""m_name"",
        ""typename"": ""std::string""
      },
      {
        ""attributes"": {
          ""Hidden"": []
        },
        ""default"": ""false"",
        ""field_type"": ""bool_t"",
        ""is_array"": false,
        ""name"": ""m_local_enabled"",
        ""typename"": ""bool""
      },
      {
        ""attributes"": {
          ""InspectorNoModify"": [],
          ""Name"": [
            ""Parent""
          ]
        },
        ""default"": null,
        ""field_type"": ""uid_t"",
        ""is_array"": false,
        ""name"": ""m_parent"",
        ""typename"": ""Box<Node>""
      },
      {
        ""attributes"": {
          ""InspectorNoModify"": [],
          ""Name"": [
            ""Prefab""
          ]
        },
        ""default"": null,
        ""field_type"": ""uid_t"",
        ""is_array"": false,
        ""name"": ""m_source_prefab"",
        ""typename"": ""assets::Handle<assets::Prefab>""
      }
    ],
    ""groups"": [],
    ""name"": ""Node"",
    ""namespace"": ""toast"",
    ""parent"": null,
    ""source_file"": ""toast/world/node.hpp""
  }
]";
				var engine = JsonSerializer.Deserialize<NodeInfo[]>(fallbackJson);
				if (engine != null) ReflectionDatabase.Nodes = engine.ToDictionary(n => n.Name);
			} catch {
				// Ignore
			}

			m_uid = "preview";
			Name = "Neptune Sun";
			TypeDisplay = "toast::DirectionalLight";
			IconColorKey = ReflectionDatabase.Nodes != null ? ReflectionDatabase.ResolveColor(TypeDisplay) : "TextMuted";
			var iconName = ReflectionDatabase.ResolveColor(TypeDisplay);
			try {
				LargeIcon = new Bitmap(
					AssetLoader.Open(new Uri($"avares://editor/Resources/node_icons/2x/{iconName}.png")));
			} catch (Exception ex) {
				Log.Warn($"Failed to load icon for {TypeDisplay} ({iconName}): {ex.Message}");
				LargeIcon = new Bitmap(AssetLoader.Open(new Uri("avares://editor/Resources/node_icons/2x/Circle.png")));
			}

			SetEnabledSuppressed(true);
			IsEditingName = false;
			HasSelection = true;

			// Construct using original Proto.Events.HierarchyElement constructor to avoid changes to outer projects/DLLs
			var protoElement = new Proto.Events.HierarchyElement {
				Name = "Neptune Sun",
				Uid = m_uid,
				Type = TypeDisplay,
				Enabled = true
			};
			var dummy = new HierarchyElement(protoElement, null!);
			Rebuild(dummy);

			if (m_fieldByParam.TryGetValue("m_position", out var posField)) posField.ApplyEngineString("1.5 2.0 -3.5");
			if (m_fieldByParam.TryGetValue("m_rotation", out var rotField)) rotField.ApplyEngineString("0 45 90");
			if (m_fieldByParam.TryGetValue("m_scale", out var scaleField)) scaleField.ApplyEngineString("1 1 1");
			if (m_fieldByParam.TryGetValue("m_parent", out var parentField)) parentField.ApplyEngineString("1001");
			return;
		}

		m_listener = new Listener();
		m_editCommitTimer.Tick += (_, _) => CommitFieldEdit();

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
		}));

		// exported script variables stream beside the reflected fields
		m_listener.Subscribe<InspectorLuaContent>(e => Dispatcher.UIThread.Post(() => {
			if (!HasSelection || e.Uid != m_builtUid) return;

			if (e.SchemaVersion != m_builtLuaVersion || e.Scripts.Count != m_luaCards.Count) {
				RebuildLuaCards(e);
				return;
			}

			foreach (var f in e.Scripts.SelectMany(AllFields)) {
				if (!m_fieldByParam.TryGetValue(f.Path, out var vm)) continue;
				if ((DateTime.UtcNow - vm.LastEdit).TotalMilliseconds < 250) continue;
				vm.ApplyEngineString(f.Value);
			}
		}));

		HierarchyViewModel.SelectionChanged += OnSelectionChanged;
		if (HierarchyViewModel.Current?.SelectedNode is { } sel) OnSelectionChanged(sel);
	}

	public ObservableCollection<ClassCardVM> Cards { get; } = [];

	public void Dispose() {
		CommitFieldEdit();
		HierarchyViewModel.SelectionChanged -= OnSelectionChanged;
		if (!Design.IsDesignMode) m_listener.Dispose();
		GC.SuppressFinalize(this);
	}

	partial void OnFilterTextChanged(string value) {
		ApplyFilter();
	}

	partial void OnEnabledChanged(bool value) {
		if (m_suppressEnabled || m_uid is null) return;
		Events.Send(new NodeEnabled { Node = m_uid, Enabled = value });
	}

	private void SetEnabledSuppressed(bool value) {
		m_suppressEnabled = true;
		Enabled = value;
		m_suppressEnabled = false;
	}

	private void OnSelectionChanged(HierarchyElement? node) {
		CommitFieldEdit();
		Dispatcher.UIThread.Post(() => {
			if (node is null) {
				HasSelection = false;
				Cards.Clear();
				m_fieldByParam.Clear();
				m_luaCards.Clear();
				m_builtLuaVersion = 0;
				m_builtUid = null;
				m_builtType = null;
				m_uid = null;
				return;
			}

			m_uid = node.Uid;
			Name = node.Name;
			TypeDisplay = node.Type;
			IconColorKey = ReflectionDatabase.ResolveColor(node.Type);
			var iconName = ReflectionDatabase.ResolveIcon(node.Type);
			try {
				LargeIcon = new Bitmap(
					AssetLoader.Open(new Uri($"avares://editor/Resources/node_icons/2x/{iconName}.png")));
			} catch (Exception ex) {
				Log.Warn($"Failed to load icon for {node.Type} ({iconName}): {ex.Message}");
				LargeIcon = new Bitmap(AssetLoader.Open(new Uri("avares://editor/Resources/node_icons/2x/Circle.png")));
			}

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
		m_luaCards.Clear();
		m_builtLuaVersion = 0;
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
		var card = new ClassCardVM(typeName, ReflectionDatabase.ResolveColor(typeName),
			ReflectionDatabase.ResolveIcon(typeName), $"class:{typeName}", m_state!, this);

		foreach (var f in info.GlobalFields) AddField(card.Fields, f);

		foreach (var g in info.Groups) {
			var colorKey = Palette[colorCounter++ % Palette.Length];
			var group = new GroupVM(g.Name, colorKey, $"group:{typeName}/{g.Name}", m_state!, this);
			foreach (var f in g.Fields) AddField(group.Fields, f);

			foreach (var sg in g.Subgroups) {
				var sub = new SubgroupVM(sg.Name, $"sub:{typeName}/{g.Name}/{sg.Name}", m_state!, this);
				foreach (var f in sg.Fields) AddField(sub.Fields, f);
				group.Subgroups.Add(sub);
			}

			card.Groups.Add(group);
		}

		foreach (var method in info.Methods) {
			if (!ReflectionDatabase.HasAttr(method.Attributes, "Button") ||
			    method.ReturnType.Trim() != "void" || method.Parameters.Length != 0)
				continue;
			var customLabel = ReflectionDatabase.GetAttr(method.Attributes, "Button");
			var label = string.IsNullOrWhiteSpace(customLabel)
				? InspectorFormat.MethodDisplayName(method.Name)
				: customLabel;
			card.Buttons.Add(new ButtonVM(label, method.Name, OnButtonInvoked));
		}

		return card;
	}

	private void AddField(ObservableCollection<FieldVM> target, FieldInfo info) {
		if (ReflectionDatabase.HasAttr(info.Attributes, "Hidden")) return;
		var vm = new FieldVM(info);
		vm.AttachClipboardHost(this);
		vm.Edited += OnFieldEdited;
		target.Add(vm);
		m_fieldByParam[vm.ParameterName] = vm;
	}

	private void OnFieldEdited(FieldVM field, string value) {
		if (m_uid is null) return;
		if (!m_isPasteBatch) BeginFieldEdit(field);
		if (field.IsLua)
			Events.Send(new NodeChangeLuaParam { Node = m_uid, Path = field.ParameterName, Value = value });
		else
			Events.Send(new NodeChangeParam { Node = m_uid, Parameter = field.ParameterName, Value = value });
	}

	private void BeginFieldEdit(FieldVM field) {
		var handle = HierarchyViewModel.Current?.ActiveWorkspaceHandle ?? 0;
		if (handle == 0 || m_uid is null) return;
		if (m_editTransaction != 0 &&
		    (m_editWorkspaceHandle != handle || m_editField != field.ParameterName)) CommitFieldEdit();
		if (m_editTransaction == 0) {
			m_editTransaction = m_nextEditTransaction++;
			m_editWorkspaceHandle = handle;
			m_editField = field.ParameterName;
			HierarchyViewModel.Current?.ActiveWorkspace?.History.SetTransactionOpen(true);
		}

		Events.Send(new WorkspaceHistoryTransaction {
			WorkspaceHandle = handle,
			Transaction = m_editTransaction,
			Phase = WorkspaceHistoryTransaction.Types.Phase.Begin,
			Operation = HistoryOperation.HistoryChangeValue,
			Node = m_uid,
			Subject = $"{field.ParameterName} changed"
		});
		m_editCommitTimer.Stop();
		m_editCommitTimer.Start();
	}

	private void CommitFieldEdit() {
		m_editCommitTimer.Stop();
		if (m_editTransaction == 0) return;
		Events.Send(new WorkspaceHistoryTransaction {
			WorkspaceHandle = m_editWorkspaceHandle,
			Transaction = m_editTransaction,
			Phase = WorkspaceHistoryTransaction.Types.Phase.Commit
		});
		HierarchyViewModel.Current?.ActiveWorkspace?.History.SetTransactionOpen(false);
		m_editTransaction = 0;
		m_editWorkspaceHandle = 0;
		m_editField = null;
	}

	private void OnButtonInvoked(string function) {
		if (m_uid is not null) Events.Send(new NodeCallFunction { Node = m_uid, Function = function });
	}

	// script cards sit above the class cards
	private void RebuildLuaCards(InspectorLuaContent e) {
		foreach (var card in m_luaCards) Cards.Remove(card);
		m_luaCards.Clear();
		// lua paths always contain ':', reflected C++ names never do
		foreach (var key in m_fieldByParam.Keys.Where(k => k.Contains(':')).ToList()) m_fieldByParam.Remove(key);

		var insertAt = 0;
		var colorCounter = 0;
		foreach (var script in e.Scripts) {
			var title = ScriptStem(script.Script);
			var card = new ClassCardVM(title, "Magenta", "Circle", $"lua:{title}", m_state!, this);

			foreach (var f in script.Fields) AddLuaField(card.Fields, f);

			foreach (var g in script.Groups) {
				var colorKey = Palette[colorCounter++ % Palette.Length];
				var group = new GroupVM(g.Name, colorKey, $"group:lua/{title}/{g.Name}", m_state!, this);
				foreach (var f in g.Fields) AddLuaField(group.Fields, f);

				foreach (var sg in g.Subgroups) {
					var sub = new SubgroupVM(sg.Name, $"sub:lua/{title}/{g.Name}/{sg.Name}", m_state!, this);
					foreach (var f in sg.Fields) AddLuaField(sub.Fields, f);
					group.Subgroups.Add(sub);
				}

				card.Groups.Add(group);
			}

			Cards.Insert(insertAt++, card);
			m_luaCards.Add(card);
		}

		m_builtLuaVersion = e.SchemaVersion;
		ApplyFilter();
	}

	private void AddLuaField(ObservableCollection<FieldVM> target, LuaField info) {
		var vm = new FieldVM(info);
		vm.AttachClipboardHost(this);
		vm.Edited += OnFieldEdited;
		target.Add(vm);
		m_fieldByParam[vm.ParameterName] = vm;
	}

	private static IEnumerable<LuaField> AllFields(LuaScriptCard script) {
		return script.Fields
			.Concat(script.Groups.SelectMany(g => g.Fields
				.Concat(g.Subgroups.SelectMany(s => s.Fields))));
	}

	// "scripts/player_controller.lua" -> "player_controller"
	private static string ScriptStem(string path) {
		var slash = Math.Max(path.LastIndexOf('/'), path.LastIndexOf('\\'));
		var name = slash >= 0 ? path[(slash + 1)..] : path;
		var dot = name.LastIndexOf('.');
		return dot > 0 ? name[..dot] : name;
	}

	async Task IInspectorClipboardHost.CopyFieldAsync(FieldVM field, int? component) {
		if (!IsCurrentField(field)) return;
		var value = component is { } index
			? InspectorClipboardConverter.CaptureComponent(field, index)
			: InspectorClipboardConverter.Capture(field);
		var scope = field.IsArray && component is null
			? InspectorClipboardScope.Array
			: InspectorClipboardScope.Value;
		var payload = new InspectorClipboardPayload(
			InspectorClipboardPayload.CurrentVersion, scope, null, value, null);
		var text = field.IsArray ? string.Join('\n', field.ArrayItems.Select(item => item.EngineValue)) : value.EngineValue;
		await InspectorClipboardService.WriteAsync(payload, text);
	}

	async Task IInspectorClipboardHost.PasteFieldAsync(FieldVM field, int? component) {
		if (!field.Editable || !IsCurrentField(field)) return;
		var uid = m_uid;
		var payload = await InspectorClipboardService.ReadAsync();
		if (uid is null || uid != m_uid || !IsCurrentField(field) || payload is null ||
		    !TryPrepareFieldPaste(field, component, payload, out var value)) return;

		ApplyPasteBatch([(field, value)], $"{field.DisplayName} pasted");
	}

	async Task<bool> IInspectorClipboardHost.CanPasteFieldAsync(FieldVM field, int? component) {
		if (!field.Editable || !IsCurrentField(field)) return false;
		var uid = m_uid;
		var payload = await InspectorClipboardService.ReadAsync();
		return uid is not null && uid == m_uid && IsCurrentField(field) && payload is not null &&
		       TryPrepareFieldPaste(field, component, payload, out _);
	}

	async Task IInspectorClipboardHost.CopyArrayItemAsync(FieldVM array, object? item) {
		if (!IsCurrentField(array) || item is not FieldVM field || !array.ArrayItems.Contains(field)) return;
		var payload = new InspectorClipboardPayload(
			InspectorClipboardPayload.CurrentVersion,
			InspectorClipboardScope.Value,
			null,
			InspectorClipboardConverter.Capture(field),
			null);
		await InspectorClipboardService.WriteAsync(payload, field.EngineValue);
	}

	async Task IInspectorClipboardHost.PasteArrayItemAsync(FieldVM array, object? item) {
		if (!array.Editable || !IsCurrentField(array) || item is not FieldVM field || !array.ArrayItems.Contains(field))
			return;
		var uid = m_uid;
		var payload = await InspectorClipboardService.ReadAsync();
		if (uid is null || uid != m_uid || !IsCurrentField(array) || !array.ArrayItems.Contains(field) ||
		    payload is not { Scope: InspectorClipboardScope.Value, Value: { } source } ||
		    !InspectorClipboardConverter.TryConvert(field, source, out var value)) return;
		ApplyPasteBatch([(field, value)], $"{array.DisplayName} element pasted");
	}

	async Task<bool> IInspectorClipboardHost.CanPasteArrayItemAsync(FieldVM array, object? item) {
		if (!array.Editable || !IsCurrentField(array) || item is not FieldVM field || !array.ArrayItems.Contains(field))
			return false;
		var uid = m_uid;
		var payload = await InspectorClipboardService.ReadAsync();
		return uid is not null && uid == m_uid && IsCurrentField(array) && array.ArrayItems.Contains(field) &&
		       payload is { Scope: InspectorClipboardScope.Value, Value: { } source } &&
		       InspectorClipboardConverter.TryConvert(field, source, out _);
	}

	async Task IInspectorClipboardHost.AppendArrayPasteAsync(FieldVM array) {
		if (!array.Editable || !IsCurrentField(array)) return;
		var uid = m_uid;
		var payload = await InspectorClipboardService.ReadAsync();
		if (uid is null || uid != m_uid || !IsCurrentField(array) || payload is null ||
		    !InspectorClipboardConverter.TryAppend(array, payload, out var value)) return;
		ApplyPasteBatch([(array, value)], $"{array.DisplayName} appended");
	}

	async Task<bool> IInspectorClipboardHost.CanAppendArrayPasteAsync(FieldVM array) {
		if (!array.Editable || !IsCurrentField(array)) return false;
		var uid = m_uid;
		var payload = await InspectorClipboardService.ReadAsync();
		return uid is not null && uid == m_uid && IsCurrentField(array) && payload is not null &&
		       InspectorClipboardConverter.TryAppend(array, payload, out var value) &&
		       !InspectorFormat.ValuesEqual(WidgetKind.Array, array.EngineValue, value);
	}

	async Task IInspectorClipboardHost.CopyScopeAsync(IInspectorClipboardScope scope) {
		if (!IsCurrentScope(scope)) return;
		var fields = scope.ClipboardFields.ToArray();
		var payloadFields = fields
			.Select(field => new InspectorClipboardField(field.ParameterName,
				InspectorClipboardConverter.Capture(field)))
			.ToArray();
		var payload = new InspectorClipboardPayload(
			InspectorClipboardPayload.CurrentVersion,
			scope.ClipboardScope,
			scope.ScopeKey,
			null,
			payloadFields);
		var text = string.Join('\n', fields.Select(field => $"{field.DisplayName} = {field.EngineValue}"));
		await InspectorClipboardService.WriteAsync(payload, text);
	}

	async Task IInspectorClipboardHost.PasteScopeAsync(IInspectorClipboardScope scope) {
		if (!IsCurrentScope(scope)) return;
		var uid = m_uid;
		var payload = await InspectorClipboardService.ReadAsync();
		if (uid is null || uid != m_uid || !IsCurrentScope(scope) || payload is null ||
		    !TryPrepareScopePaste(scope, payload, out var updates)) return;
		ApplyPasteBatch(updates, $"{scope.ScopeName} pasted");
	}

	async Task<bool> IInspectorClipboardHost.CanPasteScopeAsync(IInspectorClipboardScope scope) {
		if (!IsCurrentScope(scope)) return false;
		var uid = m_uid;
		var payload = await InspectorClipboardService.ReadAsync();
		return uid is not null && uid == m_uid && IsCurrentScope(scope) && payload is not null &&
		       TryPrepareScopePaste(scope, payload, out var updates) && updates.Count > 0;
	}

	private static bool TryPrepareFieldPaste(
		FieldVM field, int? component, InspectorClipboardPayload payload, out string value) {
		value = "";
		if (payload.Value is not { } source) return false;
		if (component is { } index) {
			if (payload.Scope != InspectorClipboardScope.Value) return false;
			// A component menu has one Paste entry: scalar payloads target the clicked component,
			// while vector/color payloads replace the whole value.
			return InspectorClipboardConverter.TryConvertComponent(field, index, source, out value) ||
			       InspectorClipboardConverter.TryConvert(field, source, out value);
		}

		var expectedScope = field.IsArray ? InspectorClipboardScope.Array : InspectorClipboardScope.Value;
		return payload.Scope == expectedScope && InspectorClipboardConverter.TryConvert(field, source, out value);
	}

	private static bool TryPrepareScopePaste(
		IInspectorClipboardScope scope,
		InspectorClipboardPayload payload,
		out List<(FieldVM Field, string Value)> updates) {
		updates = [];
		if (payload.Fields is not { } sources || payload.Scope != scope.ClipboardScope ||
		    payload.ScopeId != scope.ScopeKey) return false;
		var targets = scope.ClipboardFields.ToArray();
		if (targets.Length != sources.Length) return false;
		for (var i = 0; i < targets.Length; i++)
			if (targets[i].ParameterName != sources[i].ParameterName ||
			    !InspectorClipboardConverter.SameShape(targets[i], sources[i].Value)) return false;
		for (var i = 0; i < targets.Length; i++) {
			if (!targets[i].Editable) continue;
			if (!InspectorClipboardConverter.TryConvert(targets[i], sources[i].Value, out var value)) return false;
			updates.Add((targets[i], value));
		}
		return true;
	}

	private void ApplyPasteBatch(IEnumerable<(FieldVM Field, string Value)> requested, string subject) {
		if (m_uid is null) return;
		var updates = requested
			.Where(update => update.Field.Editable && IsCurrentField(update.Field) &&
			                 !InspectorFormat.ValuesEqual(update.Field.Kind, update.Field.EngineValue, update.Value))
			.ToArray();
		if (updates.Length == 0) return;

		CommitFieldEdit();
		var handle = HierarchyViewModel.Current?.ActiveWorkspaceHandle ?? 0;
		if (handle == 0) return;
		var transaction = m_nextEditTransaction++;
		HierarchyViewModel.Current?.ActiveWorkspace?.History.SetTransactionOpen(true);
		Events.Send(new WorkspaceHistoryTransaction {
			WorkspaceHandle = handle,
			Transaction = transaction,
			Phase = WorkspaceHistoryTransaction.Types.Phase.Begin,
			Operation = HistoryOperation.HistoryPaste,
			Node = m_uid,
			Subject = subject
		});

		m_isPasteBatch = true;
		try {
			foreach (var (field, value) in updates) field.ApplyPastedEngineString(value);
		} finally {
			m_isPasteBatch = false;
			Events.Send(new WorkspaceHistoryTransaction {
				WorkspaceHandle = handle,
				Transaction = transaction,
				Phase = WorkspaceHistoryTransaction.Types.Phase.Commit
			});
			HierarchyViewModel.Current?.ActiveWorkspace?.History.SetTransactionOpen(false);
		}
	}

	private bool IsCurrentField(FieldVM field) {
		return m_fieldByParam.Values.Any(candidate => ReferenceEquals(candidate, field) ||
			candidate.ArrayItems.Any(item => ReferenceEquals(item, field)));
	}

	private bool IsCurrentScope(IInspectorClipboardScope scope) {
		foreach (var card in Cards) {
			if (ReferenceEquals(card, scope)) return true;
			foreach (var group in card.Groups) {
				if (ReferenceEquals(group, scope)) return true;
				if (group.Subgroups.Any(subgroup => ReferenceEquals(subgroup, scope))) return true;
			}
		}

		return false;
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

	public async void CommitRename() {
		if (!IsEditingName) return;
		IsEditingName = false;
		var n = NameDraft.Trim();
		if (n.Length == 0 || n == Name || m_uid is null) return;
		if (s_reservedNames.Contains(n)) {
			await App.Modals.ShowWarning("Reserved Name",
				$"'{n}' is a reserved keyword and cannot be used as a node name.");
			return;
		}

		Events.Send(new NodeChangeName { Node = m_uid, Name = n });
	}

	public void CancelRename() {
		IsEditingName = false;
	}

	// toast::Camera -> Camera
	private static string Bare(string typeName) {
		var i = typeName.LastIndexOf(':');
		return i >= 0 ? typeName[(i + 1)..] : typeName;
	}
}
