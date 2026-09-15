using System;
using System.Collections.ObjectModel;
using System.Linq;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Dock.Model.Mvvm.Controls;
using editor.Engine;
using Proto.Events;

namespace editor.Workspace;

public partial class SignalCardViewModel : ObservableObject {
	[ObservableProperty] private bool m_expanded = true;

	public string TypeName { get; init; } = "";
	public IBrush Color { get; init; } = Brushes.Gray;
	public Bitmap? Icon { get; init; }
	public ObservableCollection<SignalItemViewModel> Signals { get; } = [];
}

public partial class SignalItemViewModel : ObservableObject {
	private readonly SignalsViewModel m_owner;

	public SignalItemViewModel(SignalsViewModel owner, string nodeUid, string declaringType, Engine.SignalInfo info) {
		m_owner = owner;
		NodeUid = nodeUid;
		DeclaringType = declaringType;
		Name = info.Name;
		Arguments = info.Arguments;
	}

	public string NodeUid { get; }
	public string DeclaringType { get; }
	public string Name { get; }
	public string[] Arguments { get; }
	public string Signature => $"{Name}({string.Join(", ", Arguments)})";
	public ObservableCollection<SignalConnectionViewModel> Connections { get; } = [];

	[RelayCommand]
	private Task AddConnection() => m_owner.AddConnectionAsync(this);

	[RelayCommand]
	private void RemoveAllConnections() {
		Events.Send(new ClearEditorSignalConnections { SourceNode = NodeUid, DeclaringType = DeclaringType, Signal = Name });
	}
}

public partial class SignalConnectionViewModel : ObservableObject {
	private readonly SignalItemViewModel m_signal;

	public SignalConnectionViewModel(SignalItemViewModel signal, SignalConnection connection, bool isLast) {
		m_signal = signal;
		TargetUid = connection.TargetUid;
		TargetName = connection.TargetName;
		TargetType = connection.TargetType;
		Function = connection.Function;
		Source = connection.Source;
		Color = SignalsViewModel.SourceBrush(connection.Source);
		IsLast = isLast;
	}

	public string TargetUid { get; }
	public string TargetName { get; }
	public string TargetType { get; }
	public string Function { get; }
	public SignalConnectionSource Source { get; }
	public IBrush Color { get; }
	public bool IsLast { get; }
	public bool CanDelete => Source == SignalConnectionSource.Editor;
	public string Label => $"{TargetName}:{Function}()";
	public string Detail => $"{TargetName} ({TargetUid})";

	[RelayCommand(CanExecute = nameof(CanDelete))]
	private void Delete() {
		Events.Send(new RemoveSignalConnection {
			SourceNode = m_signal.NodeUid, DeclaringType = m_signal.DeclaringType, Signal = m_signal.Name,
			TargetNode = TargetUid, Function = Function
		});
	}
}

public partial class SignalsViewModel : Tool, IDisposable {
	private readonly Listener m_listener = new();
	private string? m_nodeUid;

	[ObservableProperty] private bool m_hasSelection;
	[ObservableProperty] private bool m_hasSignals;
	[ObservableProperty] private string m_emptyMessage = "Select a node to see its signals";

	public SignalsViewModel() {
		HierarchyViewModel.SelectionChanged += OnSelectionChanged;
		m_listener.SubscribeOnUiThread<SignalState>(ApplyState);
	}

	public ObservableCollection<SignalCardViewModel> Cards { get; } = [];

	public void Dispose() {
		HierarchyViewModel.SelectionChanged -= OnSelectionChanged;
		m_listener.Dispose();
		GC.SuppressFinalize(this);
	}

	public void Refresh() {
		if (!string.IsNullOrEmpty(m_nodeUid)) Events.Send(new RequestSignalState { Node = m_nodeUid });
	}

	private void OnSelectionChanged(HierarchyElement? node) => Dispatcher.UIThread.Post(() => Select(node));

	private void Select(HierarchyElement? node) {
		m_nodeUid = node?.Uid;
		Cards.Clear();
		HasSelection = node is not null;
		HasSignals = false;
		EmptyMessage = node is null ? "Select a node to see its signals" : "This node has no signals";
		if (node is null || ReflectionDatabase.Nodes is null) return;
		var type = Bare(node.Type);
		while (ReflectionDatabase.Nodes.TryGetValue(type, out var info)) {
			if (info.Signals.Length > 0) {
				var card = new SignalCardViewModel {
					TypeName = info.Name, Color = ResourceBrush(ReflectionDatabase.ResolveColor(info.Name)),
					Icon = LoadIcon(ReflectionDatabase.ResolveIcon(info.Name))
				};
				var declaringType = ReflectionDatabase.QualifiedName(info);
				foreach (var signal in info.Signals)
					card.Signals.Add(new SignalItemViewModel(this, node.Uid, declaringType, signal));
				Cards.Add(card);
			}
			if (info.Parent is null) break;
			type = Bare(info.Parent.Name);
		}
		HasSignals = Cards.Count > 0;
		Refresh();
	}

	private void ApplyState(SignalState state) {
		if (state.Node != m_nodeUid) return;
		foreach (var entry in state.Signals.Where(entry => entry.DeclaringType == "Lua")) {
			var card = Cards.FirstOrDefault(item => item.TypeName == "Lua");
			if (card is null) {
				card = new SignalCardViewModel { TypeName = "Lua", Color = ResourceBrush("Magenta"), Icon = LoadIcon("Circle") };
				Cards.Insert(0, card);
			}
			if (!card.Signals.Any(item => item.Name == entry.Signal))
				card.Signals.Add(new SignalItemViewModel(this, state.Node, "Lua", new Engine.SignalInfo(entry.Signal, "Signal0", [], default)));
		}
		foreach (var signal in Cards.SelectMany(card => card.Signals)) signal.Connections.Clear();
		foreach (var entry in state.Signals) {
			var signal = Cards.SelectMany(card => card.Signals)
				.FirstOrDefault(item => item.DeclaringType == entry.DeclaringType && item.Name == entry.Signal);
			if (signal is null) continue;
			for (var i = 0; i < entry.Connections.Count; ++i)
				signal.Connections.Add(new SignalConnectionViewModel(signal, entry.Connections[i], i == entry.Connections.Count - 1));
		}
	}

	internal async Task AddConnectionAsync(SignalItemViewModel signal) {
		if (App.MainWindow is not { } owner || HierarchyViewModel.Current is not { } hierarchy) return;
		var result = await SignalConnectionWindow.ShowAsync(owner, hierarchy, signal);
		if (result is null) return;
		Events.Send(new AddSignalConnection {
			SourceNode = signal.NodeUid, DeclaringType = signal.DeclaringType, Signal = signal.Name,
			TargetNode = result.TargetUid, Function = result.Function.Name, ForwardsArgs = result.Function.ForwardsArgs
		});
	}

	internal static IBrush SourceBrush(SignalConnectionSource source) {
		var key = source switch {
			SignalConnectionSource.Lua => "Magenta", SignalConnectionSource.Editor => "Green", _ => "Blue"
		};
		return ResourceBrush(key);
	}

	private static IBrush ResourceBrush(string key) {
		return Application.Current?.TryGetResource(key, null, out var value) == true && value is IBrush brush
			? brush
			: Brushes.Gray;
	}

	private static Bitmap? LoadIcon(string name) {
		try { return new Bitmap(AssetLoader.Open(new Uri($"avares://editor/Resources/node_icons/1x/{name}.png"))); }
		catch { return null; }
	}

	private static string Bare(string type) {
		var index = type.LastIndexOf(':');
		return index < 0 ? type : type[(index + 1)..];
	}
}
