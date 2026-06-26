//
// DragVec2Box.cs by Xein
// 23 Jun 2026
//

using Avalonia;
using Avalonia.Data;

namespace editor.Components.Elements;

public sealed class DragVec2Box : DragVectorBoxBase {
	public static readonly StyledProperty<float> XProperty =
		AvaloniaProperty.Register<DragVec2Box, float>(nameof(X), defaultBindingMode: BindingMode.TwoWay);

	public static readonly StyledProperty<float> YProperty =
		AvaloniaProperty.Register<DragVec2Box, float>(nameof(Y), defaultBindingMode: BindingMode.TwoWay);

	public static readonly StyledProperty<string> LabelXProperty =
		AvaloniaProperty.Register<DragVec2Box, string>(nameof(LabelX), "X");

	public static readonly StyledProperty<string> LabelYProperty =
		AvaloniaProperty.Register<DragVec2Box, string>(nameof(LabelY), "Y");

	public float X {
		get => GetValue(XProperty);
		set => SetValue(XProperty, value);
	}

	public float Y {
		get => GetValue(YProperty);
		set => SetValue(YProperty, value);
	}

	public string LabelX {
		get => GetValue(LabelXProperty);
		set => SetValue(LabelXProperty, value);
	}

	public string LabelY {
		get => GetValue(LabelYProperty);
		set => SetValue(LabelYProperty, value);
	}

	protected override int AxisCount => 2;

	protected override AxisSpec[] BuildAxes() => [
		new AxisSpec(LabelXProperty, "Red", XProperty),
		new AxisSpec(LabelYProperty, "Green", YProperty)
	];
}
