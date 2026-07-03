using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Text.Json;
using Avalonia;
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
	[ObservableProperty] private Bitmap? m_largeIcon;
	[ObservableProperty] private bool m_hasSelection;

	[ObservableProperty] private bool m_isEditingName;
	[ObservableProperty] private string m_nameDraft = "";

	[ObservableProperty] private string m_filterText = "";

	public ObservableCollection<ClassCardVM> Cards { get; } = [];

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
				if (engine != null) {
					ReflectionDatabase.Nodes = engine.ToDictionary(n => n.Name);
				}
			} catch {
				// Ignore
			}

			m_uid = "preview";
			Name = "Neptune Sun";
			TypeDisplay = "toast::DirectionalLight";
			IconColorKey = ReflectionDatabase.Nodes != null ? ReflectionDatabase.ResolveColor(TypeDisplay) : "TextMuted";
			string iconName = ReflectionDatabase.ResolveColor(TypeDisplay);
			try {
				LargeIcon = new Bitmap(AssetLoader.Open(new Uri($"avares://editor/Resources/node_icons/2x/{iconName}.png")));
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
			string iconName = ReflectionDatabase.ResolveIcon(node.Type);
			try {
				LargeIcon = new Bitmap(AssetLoader.Open(new Uri($"avares://editor/Resources/node_icons/2x/{iconName}.png")));
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
		var card = new ClassCardVM(typeName, ReflectionDatabase.ResolveColor(typeName), ReflectionDatabase.ResolveIcon(typeName), $"class:{typeName}", m_state!);

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
