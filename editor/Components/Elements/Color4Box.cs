//
// Color4Box.cs by Xein
// 24 Jun 2026
//

using Avalonia;
using Avalonia.Data;

namespace editor.Components.Elements;

public sealed class Color4Box : ColorBoxBase {
	public static readonly StyledProperty<float> AProperty =
		AvaloniaProperty.Register<Color4Box, float>(nameof(A), 1f, defaultBindingMode: BindingMode.TwoWay);

	public float A {
		get => GetValue(AProperty);
		set => SetValue(AProperty, value);
	}

	protected override bool HasAlpha => true;

	protected override float GetAlpha() {
		return A;
	}

	protected override void SetAlpha(float value) {
		A = value;
	}

	protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change) {
		base.OnPropertyChanged(change);
		if (change.Property == AProperty) RefreshDisplay();
	}
}
