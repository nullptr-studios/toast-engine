using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Text.Json;
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

public partial class InspectorViewModel : Tool {
	private static readonly string[] Palette =
		["Red", "Green", "Blue", "Magenta", "Orange", "Yellow", "Cyan", "Beige"];

	private readonly Dictionary<string, FieldVM> m_fieldByParam = new();

	// ReSharper disable once PrivateFieldCanBeConvertedToLocalVariable
	private readonly Listener m_listener;
	private readonly List<ClassCardVM> m_luaCards = [];
	private string? m_builtType;
	private string? m_builtUid;
	private uint m_builtLuaVersion;
	[ObservableProperty] private bool m_enabled = true;

	[ObservableProperty] private string m_filterText = "";
	[ObservableProperty] private bool m_hasSelection;
	[ObservableProperty] private string m_iconColorKey = "TextMuted";

	[ObservableProperty] private bool m_isEditingName;
	[ObservableProperty] private Bitmap? m_largeIcon;

	[ObservableProperty] private string m_name = "";
	[ObservableProperty] private string m_nameDraft = "";
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
        ""typename"": ""assets::AssetHandle<assets::Prefab>""
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

			if (e.SchemaVersion != m_builtLuaVersion || m_luaCards.Count == 0) {
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

	partial void OnFilterTextChanged(string value) {
		ApplyFilter();
	}

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
			ReflectionDatabase.ResolveIcon(typeName), $"class:{typeName}", m_state!);

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
		if (field.IsLua) Events.Send(new NodeChangeLuaParam { Path = field.ParameterName, Value = value });
		else Events.Send(new NodeChangeParam { Parameter = field.ParameterName, Value = value });
		WorkspaceState.MarkModified();
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
			var card = new ClassCardVM(title, "Cyan", "Circle", $"lua:{title}", m_state!);

			foreach (var f in script.Fields) AddLuaField(card.Fields, f);

			foreach (var g in script.Groups) {
				var colorKey = Palette[colorCounter++ % Palette.Length];
				var group = new GroupVM(g.Name, colorKey, $"group:lua/{title}/{g.Name}", m_state!);
				foreach (var f in g.Fields) AddLuaField(group.Fields, f);

				foreach (var sg in g.Subgroups) {
					var sub = new SubgroupVM(sg.Name, $"sub:lua/{title}/{g.Name}/{sg.Name}", m_state!);
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

	public void CancelRename() {
		IsEditingName = false;
	}

	// toast::Camera -> Camera
	private static string Bare(string typeName) {
		var i = typeName.LastIndexOf(':');
		return i >= 0 ? typeName[(i + 1)..] : typeName;
	}
}
