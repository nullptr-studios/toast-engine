//
// NodeBox.cs by Xein
// 24 Jun 2026
//

using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Data;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Styling;
using editor.Components.Modals;
using editor.Engine;
using editor.Workspace;

namespace editor.Components.Elements;

public sealed class NodeBox : TemplatedControl {
	public static readonly StyledProperty<string?> ValueProperty =
		AvaloniaProperty.Register<NodeBox, string?>(nameof(Value), defaultBindingMode: BindingMode.TwoWay);

	public static readonly StyledProperty<string?> NodeTypeProperty =
		AvaloniaProperty.Register<NodeBox, string?>(nameof(NodeType));

	private string? m_displayName;

	public static readonly DirectProperty<NodeBox, string?> DisplayNameProperty =
		AvaloniaProperty.RegisterDirect<NodeBox, string?>(nameof(DisplayName), o => o.m_displayName);

	private bool m_hasNode;

	public static readonly DirectProperty<NodeBox, bool> HasNodeProperty =
		AvaloniaProperty.RegisterDirect<NodeBox, bool>(nameof(HasNode), o => o.m_hasNode);

	private bool m_isMissing;

	public static readonly DirectProperty<NodeBox, bool> IsMissingProperty =
		AvaloniaProperty.RegisterDirect<NodeBox, bool>(nameof(IsMissing), o => o.m_isMissing);

	private readonly MenuItem m_selectItem;
	private readonly MenuItem m_seeItem;
	private readonly MenuItem m_clearItem;

	private TopLevel? m_keyHost;
	private string? m_lastKnownName;

	public NodeBox() {
		DragDrop.SetAllowDrop(this, true);
		AddHandler(DragDrop.DragOverEvent, OnDragOver);
		AddHandler(DragDrop.DropEvent, OnDrop);

		m_selectItem = new MenuItem { Header = "Select node..." };
		m_selectItem.Click += (_, _) => OpenPicker();
		m_seeItem = new MenuItem { Header = "See in hierarchy" };
		m_seeItem.Click += (_, _) => SeeInHierarchy();
		m_clearItem = new MenuItem { Header = "Clear", InputGesture = new KeyGesture(Key.Delete) };
		m_clearItem.Click += (_, _) => Clear();

		var menu = new ContextMenu();
		menu.Items.Add(m_selectItem);
		menu.Items.Add(m_seeItem);
		menu.Items.Add(m_clearItem);
		ContextMenu = menu;
	}

	public string? Value {
		get => GetValue(ValueProperty);
		set => SetValue(ValueProperty, value);
	}

	public string? NodeType {
		get => GetValue(NodeTypeProperty);
		set => SetValue(NodeTypeProperty, value);
	}

	public string? DisplayName {
		get => m_displayName;
		private set => SetAndRaise(DisplayNameProperty, ref m_displayName, value);
	}

	public bool HasNode {
		get => m_hasNode;
		private set => SetAndRaise(HasNodeProperty, ref m_hasNode, value);
	}

	public bool IsMissing {
		get => m_isMissing;
		private set => SetAndRaise(IsMissingProperty, ref m_isMissing, value);
	}

	protected override Type StyleKeyOverride => typeof(NodeBox);

	protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e) {
		base.OnAttachedToVisualTree(e);
		if (HierarchyViewModel.Current is { } h) h.HierarchyChanged += Refresh;
		Refresh();
	}

	protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e) {
		base.OnDetachedFromVisualTree(e);
		if (HierarchyViewModel.Current is { } h) h.HierarchyChanged -= Refresh;
		UnhookKeys();
	}

	protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change) {
		base.OnPropertyChanged(change);
		if (change.Property == ValueProperty) {
			m_lastKnownName = null;
			Refresh();
		}
		else if (change.Property == NodeTypeProperty) Refresh();
		else if (change.Property == IsEnabledProperty) UpdateMenu();
	}

	protected override void OnPointerPressed(PointerPressedEventArgs e) {
		base.OnPointerPressed(e);
		if (!IsEnabled || !e.GetCurrentPoint(this).Properties.IsLeftButtonPressed) return;
		OpenPicker();
		e.Handled = true;
	}

	protected override void OnPointerEntered(PointerEventArgs e) {
		base.OnPointerEntered(e);
		HookKeys();
	}

	protected override void OnPointerExited(PointerEventArgs e) {
		base.OnPointerExited(e);
		UnhookKeys();
	}

	private void HookKeys() {
		if (m_keyHost is not null) return;
		m_keyHost = TopLevel.GetTopLevel(this);
		m_keyHost?.AddHandler(KeyDownEvent, OnHostKeyDown, RoutingStrategies.Tunnel);
	}

	private void UnhookKeys() {
		m_keyHost?.RemoveHandler(KeyDownEvent, OnHostKeyDown);
		m_keyHost = null;
	}

	private void OnHostKeyDown(object? sender, KeyEventArgs e) {
		if (!IsEnabled || string.IsNullOrEmpty(Value) || e.Key is not (Key.Delete or Key.Back)) return;
		Clear();
		e.Handled = true;
	}

	private void Refresh() {
		var hasValue = !string.IsNullOrEmpty(Value);
		var node = hasValue ? HierarchyViewModel.Current?.Find(Value!) : null;
		HasNode = node is not null;
		IsMissing = hasValue && node is null;

		if (node is not null) {
			m_lastKnownName = node.Name;
			DisplayName = node.Name;
		}
		else if (IsMissing) {
			DisplayName = m_lastKnownName is not null ? $"({m_lastKnownName} missing)" : "(missing)";
		}
		else {
			DisplayName = $"({NodeType})";
		}

		UpdateMenu();
	}

	private void UpdateMenu() {
		m_selectItem.IsEnabled = IsEnabled;
		m_seeItem.IsEnabled = IsEnabled && HasNode;
		m_clearItem.IsEnabled = IsEnabled && (HasNode || IsMissing);
	}

	private async void OpenPicker() {
		if (!IsEnabled || HierarchyViewModel.Current is not { } h || App.MainWindow is not { } owner) return;
		var picked = await new HierarchyTree(h.Root, null, NodeType).ShowDialog<string?>(owner);
		if (picked is not null) Value = picked;
	}

	private void SeeInHierarchy() {
		if (HierarchyViewModel.Current is { } h && !string.IsNullOrEmpty(Value) && h.Find(Value!) is { } node)
			h.SelectedNode = node;
	}

	private void Clear() {
		if (IsEnabled) Value = null;
	}

	private bool IsAcceptable(DragEventArgs e) =>
		e.DataTransfer.TryGetValue(NodeDragData.Format) is { } el &&
		ReflectionDatabase.IsTypeOrSubtypeOf(el.Type, NodeType);

	private void OnDragOver(object? sender, DragEventArgs e) {
		e.DragEffects = IsEnabled && IsAcceptable(e) ? DragDropEffects.Copy : DragDropEffects.None;
		e.Handled = true;
	}

	private void OnDrop(object? sender, DragEventArgs e) {
		if (IsEnabled && IsAcceptable(e) && e.DataTransfer.TryGetValue(NodeDragData.Format) is { } el) Value = el.Uid;
		e.Handled = true;
	}
	
	private static IBrush? Brush(string key) {
		if (Application.Current?.Resources.TryGetResource(key, ThemeVariant.Dark, out var r) == true)
			return r as IBrush;
		return null;
	}
}
