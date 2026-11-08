using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Linq;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using Avalonia.VisualTree;
using CommunityToolkit.Mvvm.ComponentModel;
using editor.Engine;
using Proto.Events;

namespace editor.Workspace;

public sealed class SignalFunctionViewModel {
	public SignalFunctionViewModel(SignalCallable callable) {
		Source = callable;
		Signature = $"{callable.Name}({string.Join(", ", callable.Parameters.Select(p => $"{p.Name}: {p.Type}"))})";
		Brush = CreateBrush(callable);
		DeclaringClass = callable.DeclaringClass;
	}

	public SignalCallable Source { get; }
	public string Name => Source.Name;
	public string Signature { get; }
	public bool IsEnabled => Source.Compatible && !Source.AlreadyConnected;
	public double Opacity => IsEnabled ? 1 : 0.45;
	public bool ForwardsArgs => Source.ForwardsArgs;
	public string DisabledReason => Source.DisabledReason;
	public IBrush Brush { get; }
	public string DeclaringClass { get; }
	public bool ShowHeader { get; set; }

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

public sealed record SignalConnectionResult(
	string TargetUid, SignalFunctionViewModel Function, IReadOnlyList<ParamBinding> Bindings);

public partial class SignalConnectionDialogViewModel : ObservableObject, IDisposable {
	private static ulong s_request;
	private readonly Listener? m_listener;
	private readonly SignalItemViewModel? m_signal;
	private ulong m_activeRequest;

	[ObservableProperty] private HierarchyElement? m_selectedNode;
	[ObservableProperty] private SignalFunctionViewModel? m_selectedFunction;
	[ObservableProperty] private string m_emptyMessage = "Select a node to see its functions";

	public SignalConnectionDialogViewModel(HierarchyViewModel hierarchy, SignalItemViewModel signal) {
		m_signal = signal;
		m_listener = new Listener();
		SignalText = signal.Signature;
		Nodes = hierarchy.Root;
		SignalArgs = signal.Arguments.Select((type, index) => new SignalArgChip(index, type)).ToArray();
		m_listener.SubscribeOnUiThread<SignalCallables>(ApplyCallables);
	}

	public string SignalText { get; }

	public ObservableCollection<HierarchyElement> Nodes { get; }
	public ObservableCollection<SignalFunctionViewModel> Functions { get; } = [];
	public ObservableCollection<SignalParameterViewModel> Parameters { get; } = [];
	public IReadOnlyList<SignalArgChip> SignalArgs { get; }
	public bool HasSignalArgs => SignalArgs.Count > 0;
	public bool HasParameters => Parameters.Count > 0;
	public bool ShowNoParameters => SelectedFunction is not null && Parameters.Count == 0;
	public bool CanAccept => SelectedNode is not null && SelectedFunction?.IsEnabled == true && Parameters.All(p => p.IsBound);
	public string AcceptHint {
		get {
			if (SelectedFunction is null || !SelectedFunction.IsEnabled) return "";
			var missing = Parameters.Where(p => !p.IsBound).Select(p => p.Name).ToArray();
			return missing.Length == 0 ? "" : $"Bind {string.Join(", ", missing)} to continue";
		}
	}

	partial void OnSelectedNodeChanged(HierarchyElement? value) {
		SelectedFunction = null;
		Functions.Clear();
		EmptyMessage = value is null ? "Select a node to see its functions" : "This node has no callable functions";
		RaiseAcceptChanged();
		if (value is null || m_signal is null) return;
		PopulateReflectedFunctions(value.Type);
		m_activeRequest = ++s_request;
		Events.Send(new RequestSignalCallables {
			Request = m_activeRequest, SourceNode = m_signal.NodeUid, DeclaringType = m_signal.DeclaringType,
			Signal = m_signal.Name, TargetNode = value.Uid
		});
	}

	partial void OnSelectedFunctionChanged(SignalFunctionViewModel? value) {
		foreach (var parameter in Parameters) parameter.PropertyChanged -= OnParameterChanged;
		Parameters.Clear();
		if (value is not null) {
			foreach (var parameter in value.Source.Parameters) {
				var item = new SignalParameterViewModel(parameter.Name, parameter.Type, SignalArgs);
				item.PropertyChanged += OnParameterChanged;
				Parameters.Add(item);
			}
		}
		OnPropertyChanged(nameof(HasParameters));
		OnPropertyChanged(nameof(ShowNoParameters));
		RaiseAcceptChanged();
	}

	private void OnParameterChanged(object? sender, PropertyChangedEventArgs e) {
		if (e.PropertyName is nameof(SignalParameterViewModel.IsBound)) RaiseAcceptChanged();
	}

	private void RaiseAcceptChanged() {
		OnPropertyChanged(nameof(CanAccept));
		OnPropertyChanged(nameof(AcceptHint));
	}

	public bool TryBind(SignalParameterViewModel parameter, int argIndex) {
		if (argIndex < 0 || argIndex >= SignalArgs.Count) return false;
		if (!SignalTypes.CanFeed(SignalArgs[argIndex].Type, parameter.Type)) return false;
		parameter.BoundArg = argIndex;
		return true;
	}

	public bool CanBind(SignalParameterViewModel parameter, int argIndex) =>
		argIndex >= 0 && argIndex < SignalArgs.Count && SignalTypes.CanFeed(SignalArgs[argIndex].Type, parameter.Type);

	public IReadOnlyList<ParamBinding> CreateBindings() => Parameters.Select(p => p.ToBinding()).ToArray();

	private void ApplyCallables(SignalCallables response) {
		if (response.Request != m_activeRequest || response.TargetNode != SelectedNode?.Uid) return;
		if (!string.IsNullOrEmpty(response.Error)) {
			// the engine can't resolve
			// drop every connection it has
			SelectedFunction = null;
			Functions.Clear();
			EmptyMessage = response.Error;
			return;
		}
		if (response.Callables.Count > 0) {
			// replacing the items drops the ListBox selection so restore it by name
			var selected = SelectedFunction?.Name;
			Replace(response.Callables.Select(callable => new SignalFunctionViewModel(callable)));
			SelectedFunction = Functions.FirstOrDefault(function => function.Name == selected);
		}
		EmptyMessage = Functions.Count == 0 ? "This node has no callable functions" : "";
	}

	private void PopulateReflectedFunctions(string typeName) {
		if (ReflectionDatabase.Nodes is null) return;
		var seen = new HashSet<string>();
		var found = new List<SignalFunctionViewModel>();
		var type = Bare(typeName);
		while (ReflectionDatabase.Nodes.TryGetValue(type, out var info)) {
			foreach (var method in info.Methods) {
				if (!seen.Add(method.Name)) continue;
				var returnsVoid = NormalizeType(method.ReturnType) == "void";
				var container = method.Parameters.FirstOrDefault(p => !SignalTypes.IsBindable(p.Type));
				var callable = new SignalCallable {
					Name = method.Name,
					DeclaringClass = type,
					HasCpp = true,
					Compatible = returnsVoid && container is null,
					ForwardsArgs = method.Parameters.Length > 0,
					DisabledReason = !returnsVoid ? "The function returns a value"
						: container is null ? "" : $"Parameter '{container.Name}' is a container, which cannot be bound"
				};
				callable.Parameters.AddRange(method.Parameters.Select(p => new SignalParameter { Name = p.Name, Type = p.Type }));
				found.Add(new SignalFunctionViewModel(callable));
			}
			if (info.Parent is null) break;
			type = Bare(info.Parent.Name);
		}
		Replace(found);
		EmptyMessage = Functions.Count == 0 ? "This node has no callable functions" : "";
	}

	private void Replace(IEnumerable<SignalFunctionViewModel> incoming) {
		var all = incoming.ToList();
		var classes = all.Select(function => function.DeclaringClass).Distinct().ToList();
		if (classes.Remove("Lua")) classes.Insert(0, "Lua");

		Functions.Clear();
		foreach (var declaringClass in classes) {
			var first = true;
			foreach (var function in all.Where(function => function.DeclaringClass == declaringClass)
				         .OrderBy(function => function.Name, StringComparer.OrdinalIgnoreCase)) {
				function.ShowHeader = first;
				first = false;
				Functions.Add(function);
			}
		}
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

	public void Dispose() {
		foreach (var parameter in Parameters) parameter.PropertyChanged -= OnParameterChanged;
		m_listener?.Dispose();
	}
}

public partial class SignalConnectionWindow : Window {
	private static readonly DataFormat<SignalArgChip> s_argFormat =
		DataFormat.CreateInProcessFormat<SignalArgChip>("toast-signal-arg");

	private Point m_pressPoint;
	private SignalArgChip? m_pressedChip;
	private PointerPressedEventArgs? m_pressArgs;

	public SignalConnectionWindow() {
		InitializeComponent();
		AddHandler(DragDrop.DragOverEvent, OnDragOverSlot);
		AddHandler(DragDrop.DropEvent, OnDropSlot);
		AddHandler(PointerPressedEvent, OnChipPressed, RoutingStrategies.Bubble, true);
		AddHandler(PointerMovedEvent, OnChipMoved, RoutingStrategies.Bubble, true);
		AddHandler(PointerReleasedEvent, OnChipReleased, RoutingStrategies.Bubble, true);
	}

	public static async Task<SignalConnectionResult?> ShowAsync(Window owner, HierarchyViewModel hierarchy, SignalItemViewModel signal) {
		using var vm = new SignalConnectionDialogViewModel(hierarchy, signal);
		var window = new SignalConnectionWindow { DataContext = vm };
		return await window.ShowDialog<SignalConnectionResult?>(owner);
	}

	private SignalConnectionDialogViewModel? Vm => DataContext as SignalConnectionDialogViewModel;

	private void OnCancel(object? sender, RoutedEventArgs e) => Close(null);

	private void OnAccept(object? sender, RoutedEventArgs e) {
		if (Vm is { CanAccept: true, SelectedNode: { } node, SelectedFunction: { } function } vm)
			Close(new SignalConnectionResult(node.Uid, function, vm.CreateBindings()));
	}

	private void OnChipPressed(object? sender, PointerPressedEventArgs e) {
		if (e.Source is not Visual source || FindChip(source) is not { } chip ||
		    !e.GetCurrentPoint(this).Properties.IsLeftButtonPressed) return;
		m_pressedChip = chip;
		m_pressArgs = e;
		m_pressPoint = e.GetPosition(this);
	}

	private async void OnChipMoved(object? sender, PointerEventArgs e) {
		if (m_pressedChip is null || m_pressArgs is null || !e.GetCurrentPoint(this).Properties.IsLeftButtonPressed) return;
		var delta = e.GetPosition(this) - m_pressPoint;
		if (delta.X * delta.X + delta.Y * delta.Y < 16) return;

		var chip = m_pressedChip;
		var args = m_pressArgs;
		m_pressedChip = null;
		m_pressArgs = null;

		var data = new DataTransfer();
		data.Add(DataTransferItem.Create(s_argFormat, chip));
		await DragDrop.DoDragDropAsync(args, data, DragDropEffects.Copy);
	}

	private void OnChipReleased(object? sender, PointerReleasedEventArgs e) {
		m_pressedChip = null;
		m_pressArgs = null;
	}

	private bool Accepts(DragEventArgs e, out SignalParameterViewModel? parameter, out SignalArgChip? chip) {
		parameter = null;
		chip = e.DataTransfer.TryGetValue(s_argFormat);
		if (chip is null || Vm is not { } vm) return false;
		parameter = e.Source is Visual source ? FindParameter(source) : null;
		return parameter is not null && vm.CanBind(parameter, chip.Index);
	}

	private static SignalArgChip? FindChip(Visual source) {
		for (Visual? current = source; current is not null; current = current.GetVisualParent()) {
			if (current is Control { Tag: "arg-chip", DataContext: SignalArgChip chip }) return chip;
		}
		return null;
	}

	private static SignalParameterViewModel? FindParameter(Visual source) {
		for (Visual? current = source; current is not null; current = current.GetVisualParent()) {
			if (current is Control { Tag: "param-slot", DataContext: SignalParameterViewModel parameter }) return parameter;
		}
		return null;
	}

	private void OnDragOverSlot(object? sender, DragEventArgs e) {
		e.DragEffects = Accepts(e, out _, out _) ? DragDropEffects.Copy : DragDropEffects.None;
		e.Handled = true;
	}

	private void OnDropSlot(object? sender, DragEventArgs e) {
		if (Accepts(e, out var parameter, out var chip) && Vm is { } vm) vm.TryBind(parameter!, chip!.Index);
		e.Handled = true;
	}
}
