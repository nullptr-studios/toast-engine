using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Globalization;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Interactivity;
using CommunityToolkit.Mvvm.ComponentModel;
using editor.Engine;
using Proto.Events;

namespace editor.Workspace;

public partial class HistoryConflictItemViewModel : ObservableObject {
	public static readonly string[] ChoiceNames = ["Current", "Incoming", "Base", "Custom"];

	[ObservableProperty] private string m_choice = "Current";
	[ObservableProperty] private bool m_customBool;
	[ObservableProperty] private decimal m_customNumber;
	[ObservableProperty] private string m_customValue = "";

	public HistoryConflictItemViewModel(HistoryConflict conflict) {
		Source = conflict;
		DisplayName = string.IsNullOrWhiteSpace(conflict.NodeName)
			? conflict.Field
			: $"{conflict.NodeName} · {conflict.Field}";
		BaseValue = conflict.BaseValue;
		CurrentValue = conflict.CurrentValue;
		IncomingValue = conflict.IncomingValue;
	}

	public HistoryConflict Source { get; }
	public string DisplayName { get; }
	public string BaseValue { get; }
	public string CurrentValue { get; }
	public string IncomingValue { get; }
	public IReadOnlyList<string> Choices => ChoiceNames;
	public bool IsBooleanCustom => ShowCustom && !Source.IsArray && Source.ValueType == 0;
	public bool IsNumberCustom => ShowCustom && !Source.IsArray && Source.ValueType is 1 or 2 or 4;
	public bool IsTextCustom => ShowCustom && !IsBooleanCustom && !IsNumberCustom;

	public string CustomType =>
		Source.IsArray
			? "Array value"
			: Source.ValueType switch {
				0 => "Boolean value",
				1 => "Integer value",
				2 => "Float value",
				3 => "Text value",
				4 => "Double value",
				5 => "Node / asset UID",
				6 => "Vector 2 (x y)",
				7 => "Vector 3 (x y z)",
				8 => "Vector 4 (x y z w)",
				9 => "Rotation (x y z w)",
				_ => "Custom value"
			};

	public bool ShowCustom => Choice == "Custom";

	partial void OnChoiceChanged(string value) {
		if (value == "Custom" && string.IsNullOrEmpty(CustomValue)) {
			CustomValue = CurrentValue == "<not set>" ? "" : CurrentValue;
			CustomBool = string.Equals(CustomValue, "true", StringComparison.OrdinalIgnoreCase);
			decimal.TryParse(CustomValue, NumberStyles.Float, CultureInfo.InvariantCulture, out m_customNumber);
			OnPropertyChanged(nameof(CustomNumber));
		}

		OnPropertyChanged(nameof(ShowCustom));
		OnPropertyChanged(nameof(IsBooleanCustom));
		OnPropertyChanged(nameof(IsNumberCustom));
		OnPropertyChanged(nameof(IsTextCustom));
	}

	partial void OnCustomBoolChanged(bool value) {
		CustomValue = value ? "true" : "false";
	}

	partial void OnCustomNumberChanged(decimal value) {
		CustomValue = value.ToString(CultureInfo.InvariantCulture);
	}
}

public sealed class HistoryConflictDialogViewModel {
	public HistoryConflictDialogViewModel(WorkspaceHistoryConflicts request) {
		Request = request;
		Title = request.IsMerge ? "Resolve merge conflicts" : "Resolve cherry-pick conflicts";
		foreach (var conflict in request.Conflicts) Conflicts.Add(new HistoryConflictItemViewModel(conflict));
	}

	public WorkspaceHistoryConflicts Request { get; }
	public string Title { get; }
	public ObservableCollection<HistoryConflictItemViewModel> Conflicts { get; } = [];
}

public partial class HistoryConflictWindow : Window {
	private bool m_submitted;

	public HistoryConflictWindow() {
		InitializeComponent();
	}

	public static async Task ShowAsync(WorkspaceHistoryConflicts request) {
		if (App.MainWindow is not { } owner) {
			SendCancel(request);
			return;
		}

		var window = new HistoryConflictWindow {
			DataContext = new HistoryConflictDialogViewModel(request)
		};
		await window.ShowDialog(owner);
		if (!window.m_submitted) SendCancel(request);
	}

	private static void SendCancel(WorkspaceHistoryConflicts request) {
		Events.Send(new WorkspaceResolveHistoryConflicts {
			WorkspaceHandle = request.WorkspaceHandle,
			Request = request.Request,
			Cancel = true
		});
	}

	private void OnCancel(object? sender, RoutedEventArgs e) {
		Close();
	}

	private void OnResolve(object? sender, RoutedEventArgs e) {
		if (DataContext is not HistoryConflictDialogViewModel vm) return;
		var response = new WorkspaceResolveHistoryConflicts {
			WorkspaceHandle = vm.Request.WorkspaceHandle,
			Request = vm.Request.Request
		};
		foreach (var item in vm.Conflicts) {
			var choice = item.Choice switch {
				"Base" => 0,
				"Current" => 1,
				"Incoming" => 2,
				_ => 3
			};
			response.Resolutions.Add(new HistoryConflictResolution {
				Conflict = item.Source.Id,
				Choice = (HistoryConflictResolution.Types.Choice)choice,
				CustomValue = item.CustomValue
			});
		}

		Events.Send(response);
		m_submitted = true;
		Close();
	}
}
