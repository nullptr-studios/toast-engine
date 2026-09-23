using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Media;
using CommunityToolkit.Mvvm.ComponentModel;
using editor.Engine;
using Proto.Events;

namespace editor.Workspace;

public sealed class SignalFunctionViewModel {
	public SignalFunctionViewModel(SignalCallable callable) {
		Source = callable;
		Signature = $"{callable.Name}({string.Join(", ", callable.Parameters.Select(p => $"{p.Name}: {p.Type}"))})";
		Brush = CreateBrush(callable);
	}

	public SignalCallable Source { get; }
	public string Name => Source.Name;
	public string Signature { get; }
	public bool IsEnabled => Source.Compatible && !Source.AlreadyConnected;
	public double Opacity => IsEnabled ? 1 : 0.45;
	public bool ForwardsArgs => Source.ForwardsArgs;
	public string DisabledReason => Source.DisabledReason;
	public IBrush Brush { get; }

	private static IBrush CreateBrush(SignalCallable callable) {
		if (callable.HasCpp && callable.HasLua) {
			return new LinearGradientBrush {
				StartPoint = new RelativePoint(0, 0, RelativeUnit.Relative),
				EndPoint = new RelativePoint(1, 1, RelativeUnit.Relative),
				GradientStops = {
					new GradientStop(ToColor(SignalsViewModel.SourceBrush(SignalConnectionSource.Cpp)), 0.499),
					new GradientStop(ToColor(SignalsViewModel.SourceBrush(SignalConnectionSource.Lua)), 0.5)
				}
			};
		}
		return SignalsViewModel.SourceBrush(callable.HasLua ? SignalConnectionSource.Lua : SignalConnectionSource.Cpp);
	}

	private static Color ToColor(IBrush brush) => brush is ISolidColorBrush solid ? solid.Color : Colors.Gray;
}

public sealed record SignalConnectionResult(string TargetUid, SignalFunctionViewModel Function);

public partial class SignalConnectionDialogViewModel : ObservableObject, IDisposable {
	private static ulong s_request;
	private readonly Listener m_listener = new();
	private readonly SignalItemViewModel m_signal;
	private ulong m_activeRequest;

	[ObservableProperty] private HierarchyElement? m_selectedNode;
	[ObservableProperty] private SignalFunctionViewModel? m_selectedFunction;
	[ObservableProperty] private string m_emptyMessage = "Select a node to see its functions";

	public SignalConnectionDialogViewModel(HierarchyViewModel hierarchy, SignalItemViewModel signal) {
		m_signal = signal;
		Nodes = hierarchy.Root;
		m_listener.SubscribeOnUiThread<SignalCallables>(ApplyCallables);
	}

	public ObservableCollection<HierarchyElement> Nodes { get; }
	public ObservableCollection<SignalFunctionViewModel> Functions { get; } = [];
	public bool CanAccept => SelectedNode is not null && SelectedFunction?.IsEnabled == true;

	partial void OnSelectedNodeChanged(HierarchyElement? value) {
		SelectedFunction = null;
		Functions.Clear();
		EmptyMessage = value is null ? "Select a node to see its functions" : "This node has no callable functions";
		OnPropertyChanged(nameof(CanAccept));
		if (value is null) return;
		PopulateReflectedFunctions(value.Type);
		m_activeRequest = ++s_request;
		Events.Send(new RequestSignalCallables {
			Request = m_activeRequest, SourceNode = m_signal.NodeUid, DeclaringType = m_signal.DeclaringType,
			Signal = m_signal.Name, TargetNode = value.Uid
		});
	}

	partial void OnSelectedFunctionChanged(SignalFunctionViewModel? value) => OnPropertyChanged(nameof(CanAccept));

	private void ApplyCallables(SignalCallables response) {
		if (response.Request != m_activeRequest || response.TargetNode != SelectedNode?.Uid) return;
		if (!string.IsNullOrEmpty(response.Error)) {
			EmptyMessage = response.Error;
			return;
		}
		if (response.Callables.Count > 0) {
			Functions.Clear();
			foreach (var callable in response.Callables) Functions.Add(new SignalFunctionViewModel(callable));
		}
		EmptyMessage = Functions.Count == 0 ? "This node has no callable functions" : "";
	}

	private void PopulateReflectedFunctions(string typeName) {
		if (ReflectionDatabase.Nodes is null) return;
		var signalTypes = m_signal.Arguments.Select(NormalizeType).ToArray();
		var seen = new HashSet<string>();
		var type = Bare(typeName);
		while (ReflectionDatabase.Nodes.TryGetValue(type, out var info)) {
			foreach (var method in info.Methods) {
				if (!seen.Add(method.Name)) continue;
				var returnsVoid = NormalizeType(method.ReturnType) == "void";
				var parametersMatch = method.Parameters.Length == 0 ||
				                      method.Parameters.Select(p => NormalizeType(p.Type)).SequenceEqual(signalTypes);
				var callable = new SignalCallable {
					Name = method.Name,
					HasCpp = true,
					Compatible = returnsVoid && parametersMatch,
					ForwardsArgs = method.Parameters.Length > 0,
					DisabledReason = returnsVoid && parametersMatch ? "" : "The function signature is not compatible"
				};
				callable.Parameters.AddRange(method.Parameters.Select(p => new SignalParameter { Name = p.Name, Type = p.Type }));
				Functions.Add(new SignalFunctionViewModel(callable));
			}
			if (info.Parent is null) break;
			type = Bare(info.Parent.Name);
		}
		var sorted = Functions.OrderBy(function => function.Name).ToArray();
		Functions.Clear();
		foreach (var function in sorted) Functions.Add(function);
		EmptyMessage = Functions.Count == 0 ? "This node has no callable functions" : "";
	}

	private static string Bare(string type) {
		var index = type.LastIndexOf(':');
		return index < 0 ? type : type[(index + 1)..];
	}

	private static string NormalizeType(string type) {
		return type.Replace("const", "", StringComparison.Ordinal)
			.Replace("&", "", StringComparison.Ordinal)
			.Replace(" ", "", StringComparison.Ordinal);
	}

	public void Dispose() => m_listener.Dispose();
}

public partial class SignalConnectionWindow : Window {
	public SignalConnectionWindow() => InitializeComponent();

	public static async Task<SignalConnectionResult?> ShowAsync(Window owner, HierarchyViewModel hierarchy, SignalItemViewModel signal) {
		using var vm = new SignalConnectionDialogViewModel(hierarchy, signal);
		var window = new SignalConnectionWindow { DataContext = vm };
		return await window.ShowDialog<SignalConnectionResult?>(owner);
	}

	private void OnCancel(object? sender, RoutedEventArgs e) => Close(null);

	private void OnAccept(object? sender, RoutedEventArgs e) {
		if (DataContext is SignalConnectionDialogViewModel { CanAccept: true, SelectedNode: { } node, SelectedFunction: { } function })
			Close(new SignalConnectionResult(node.Uid, function));
	}
}
