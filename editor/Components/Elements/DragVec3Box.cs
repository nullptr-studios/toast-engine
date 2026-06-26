//
// DragVec3Box.cs by Xein
// 23 Jun 2026
//

using Avalonia;
using Avalonia.Data;

namespace editor.Components.Elements;

public sealed class DragVec3Box : DragVectorBoxBase {
	public static readonly StyledProperty<float> XProperty =
		AvaloniaProperty.Register<DragVec3Box, float>(nameof(X), defaultBindingMode: BindingMode.TwoWay);

	public static readonly StyledProperty<float> YProperty =
		AvaloniaProperty.Register<DragVec3Box, float>(nameof(Y), defaultBindingMode: BindingMode.TwoWay);

	public static readonly StyledProperty<float> ZProperty =
		AvaloniaProperty.Register<DragVec3Box, float>(nameof(Z), defaultBindingMode: BindingMode.TwoWay);

	public static readonly StyledProperty<string> LabelXProperty =
		AvaloniaProperty.Register<DragVec3Box, string>(nameof(LabelX), "X");

	public static readonly StyledProperty<string> LabelYProperty =
		AvaloniaProperty.Register<DragVec3Box, string>(nameof(LabelY), "Y");

	public static readonly StyledProperty<string> LabelZProperty =
		AvaloniaProperty.Register<DragVec3Box, string>(nameof(LabelZ), "Z");

	public float X {
		get => GetValue(XProperty);
		set => SetValue(XProperty, value);
	}

	public float Y {
		get => GetValue(YProperty);
		set => SetValue(YProperty, value);
	}

	public float Z {
		get => GetValue(ZProperty);
		set => SetValue(ZProperty, value);
	}

	public string LabelX {
		get => GetValue(LabelXProperty);
		set => SetValue(LabelXProperty, value);
	}

	public string LabelY {
		get => GetValue(LabelYProperty);
		set => SetValue(LabelYProperty, value);
	}

	public string LabelZ {
		get => GetValue(LabelZProperty);
		set => SetValue(LabelZProperty, value);
	}

	protected override int AxisCount => 3;

	protected override AxisSpec[] BuildAxes() => [
		new AxisSpec(LabelXProperty, "Red", XProperty),
		new AxisSpec(LabelYProperty, "Green", YProperty),
		new AxisSpec(LabelZProperty, "Blue", ZProperty)
	];
}
