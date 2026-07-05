//
// AssetBox.cs by Xein
// 24 Jun 2026
//

using System;
using System.IO;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Data;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using Avalonia.Styling;
using editor.Assets;
using editor.Assets.Types;
using editor.Components.Modals;
using editor.Workspace;
using Lucide.Avalonia;

namespace editor.Components.Elements;

public sealed class AssetBox : TemplatedControl {
	public static readonly StyledProperty<string?> ValueProperty =
		AvaloniaProperty.Register<AssetBox, string?>(nameof(Value), defaultBindingMode: BindingMode.TwoWay);

	public static readonly StyledProperty<string?> AssetTypeProperty =
		AvaloniaProperty.Register<AssetBox, string?>(nameof(AssetType));

	public static readonly DirectProperty<AssetBox, string?> DisplayNameProperty =
		AvaloniaProperty.RegisterDirect<AssetBox, string?>(nameof(DisplayName), o => o.m_displayName);

	public static readonly DirectProperty<AssetBox, bool> HasAssetProperty =
		AvaloniaProperty.RegisterDirect<AssetBox, bool>(nameof(HasAsset), o => o.m_hasAsset);

	public static readonly DirectProperty<AssetBox, bool> IsMissingProperty =
		AvaloniaProperty.RegisterDirect<AssetBox, bool>(nameof(IsMissing), o => o.m_isMissing);

	public static readonly DirectProperty<AssetBox, IBrush?> IconColorProperty =
		AvaloniaProperty.RegisterDirect<AssetBox, IBrush?>(nameof(IconColor), o => o.m_iconColor);

	public static readonly DirectProperty<AssetBox, LucideIconKind> IconKindProperty =
		AvaloniaProperty.RegisterDirect<AssetBox, LucideIconKind>(nameof(IconKind), o => o.m_iconKind);

	private readonly MenuItem m_clearItem;
	private readonly MenuItem m_seeItem;

	private readonly MenuItem m_selectItem;

	private string? m_displayName;

	private bool m_hasAsset;

	private IBrush? m_iconColor;

	private LucideIconKind m_iconKind = LucideIconKind.Package;

	private bool m_isMissing;

	private TopLevel? m_keyHost;

	private string? m_lastKnownName;

	public AssetBox() {
		DragDrop.SetAllowDrop(this, true);
		AddHandler(DragDrop.DragOverEvent, OnDragOver);
		AddHandler(DragDrop.DropEvent, OnDrop);

		m_selectItem = new MenuItem { Header = "Select asset..." };
		m_selectItem.Click += (_, _) => OpenPicker();
		m_seeItem = new MenuItem { Header = "Show in asset browser" };
		m_seeItem.Click += (_, _) => ShowInBrowser();
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

	public string? AssetType {
		get => GetValue(AssetTypeProperty);
		set => SetValue(AssetTypeProperty, value);
	}

	public string? DisplayName {
		get => m_displayName;
		private set => SetAndRaise(DisplayNameProperty, ref m_displayName, value);
	}

	public bool HasAsset {
		get => m_hasAsset;
		private set => SetAndRaise(HasAssetProperty, ref m_hasAsset, value);
	}

	public bool IsMissing {
		get => m_isMissing;
		private set => SetAndRaise(IsMissingProperty, ref m_isMissing, value);
	}

	public IBrush? IconColor {
		get => m_iconColor;
		private set => SetAndRaise(IconColorProperty, ref m_iconColor, value);
	}

	public LucideIconKind IconKind {
		get => m_iconKind;
		private set => SetAndRaise(IconKindProperty, ref m_iconKind, value);
	}

	protected override Type StyleKeyOverride => typeof(AssetBox);

	protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e) {
		base.OnAttachedToVisualTree(e);
		AssetDatabase.ReloadedDatabase += Refresh;
		Refresh();
	}

	protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e) {
		base.OnDetachedFromVisualTree(e);
		AssetDatabase.ReloadedDatabase -= Refresh;
		UnhookKeys();
	}

	protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change) {
		base.OnPropertyChanged(change);
		if (change.Property == ValueProperty) {
			m_lastKnownName = null;
			Refresh();
		} else if (change.Property == AssetTypeProperty) {
			Refresh();
		} else if (change.Property == IsEnabledProperty) {
			UpdateMenu();
		}
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
		var effectiveValue = Value == InspectorFormat.NullUid ? null : Value;
		var hasValue = !string.IsNullOrEmpty(effectiveValue);
		var path = "";
		var type = "";
		var resolved = hasValue && AssetDatabase.TryResolve(effectiveValue!, out path, out type);
		HasAsset = resolved;
		IsMissing = hasValue && !resolved;

		if (resolved) {
			var name = Path.GetFileNameWithoutExtension(path);
			m_lastKnownName = name;
			DisplayName = name;
			var def = AssetTypeRegistry.ByType(type);
			if (def is not null &&
			    Application.Current?.TryGetResource(def.ChipColor, ThemeVariant.Default, out var res) == true &&
			    res is IBrush defBrush)
				IconColor = defBrush;
			else
				IconColor = new SolidColorBrush(Color.Parse("#696969"));
			IconKind = def?.Icon ?? LucideIconKind.Package;
		} else if (IsMissing) {
			DisplayName = m_lastKnownName is not null ? $"({m_lastKnownName} missing)" : "(missing)";
		} else {
			DisplayName = string.IsNullOrEmpty(AssetType) ? "(Asset)" : $"({AssetType})";
		}

		UpdateMenu();
	}

	private void UpdateMenu() {
		m_selectItem.IsEnabled = IsEnabled;
		m_seeItem.IsEnabled = IsEnabled && HasAsset;
		m_clearItem.IsEnabled = IsEnabled && (HasAsset || IsMissing);
	}

	private static string? NormalizeAssetType(string? type) {
		if (string.IsNullOrEmpty(type)) return null;
		// Direct lookup (handles types that are already identifiers like "texture")
		var def = AssetTypeRegistry.ByType(type);
		if (def != null) return def.Type;
		// C++ class name → asset type (handles "AudioBank" → "audio_bank")
		def = AssetTypeRegistry.ByCppTypeName(type);
		if (def != null) return def.Type;
		// Try PascalCase conversion as fallback (handles e.g. "AudioVca" → "audio_vca")
		def = AssetTypeRegistry.All.FirstOrDefault(a =>
			string.Equals(a.Type, type, StringComparison.OrdinalIgnoreCase) ||
			a.CppTypeNames.Any(n => n.Equals(type, StringComparison.OrdinalIgnoreCase)));
		return def?.Type ?? type;
	}

	private async void OpenPicker() {
		if (!IsEnabled || App.MainWindow is not { } owner) return;
		var picked = await new AssetList(NormalizeAssetType(AssetType)).ShowDialog<string?>(owner);
		if (picked is not null) Value = picked;
	}

	private void ShowInBrowser() {
		if (!string.IsNullOrEmpty(Value)) AssetBrowserViewModel.Current?.RevealAsset(Value!);
	}

	private void Clear() {
		if (IsEnabled) Value = null;
	}

	private bool IsAcceptable(DragEventArgs e) {
		return e.DataTransfer.TryGetValue(AssetDragData.Format) is { } a &&
			(string.IsNullOrEmpty(AssetType) || string.Equals(
				NormalizeAssetType(AssetType) ?? AssetType,
				a.Type, StringComparison.OrdinalIgnoreCase));
	}

	private void OnDragOver(object? sender, DragEventArgs e) {
		e.DragEffects = IsEnabled && IsAcceptable(e) ? DragDropEffects.Copy : DragDropEffects.None;
		e.Handled = true;
	}

	private void OnDrop(object? sender, DragEventArgs e) {
		if (IsEnabled && IsAcceptable(e) && e.DataTransfer.TryGetValue(AssetDragData.Format) is { } a) Value = a.Uid;
		e.Handled = true;
	}
}
