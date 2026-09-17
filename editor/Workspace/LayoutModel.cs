//
// LayoutModel.cs by Xein
// 1 Aug 2026
//

using System.Collections.Generic;

namespace editor.Workspace;

public sealed class LayoutFile {
	public int Version { get; set; } = 1;
	public string Name { get; set; } = "";
	public LayoutNode? Main { get; set; }
	public LayoutNode? Toast { get; set; }
	public double ToastZoneHeight { get; set; } = 400;
	public bool ToastZonePinned { get; set; }
}

public sealed class LayoutNode {
	public const string KindRoot = "Root";
	public const string KindProportional = "Proportional";
	public const string KindSplitter = "Splitter";
	public const string KindTool = "Tool";
	public const string KindDocument = "Document";

	public string Kind { get; set; } = "";
	public string? Id { get; set; }

	public double? Proportion { get; set; }

	public string? Orientation { get; set; } // Proportional
	public string? Alignment { get; set; }   // Tool
	public string? GripMode { get; set; }    // Tool
	public bool IsPrimary { get; set; }      // Document

	public List<string>? Dockables { get; set; } // Tool, in tab order
	public string? ActiveDockable { get; set; }
	public List<LayoutNode>? Children { get; set; } // Root, Proportional

	// Root only
	public List<LayoutWindowNode>? Windows { get; set; }
	public List<string>? LeftPinned { get; set; }
	public List<string>? RightPinned { get; set; }
	public List<string>? TopPinned { get; set; }
	public List<string>? BottomPinned { get; set; }
}

public sealed class LayoutWindowNode {
	public string? Id { get; set; }
	public double X { get; set; }
	public double Y { get; set; }
	public double Width { get; set; }
	public double Height { get; set; }
	public bool Topmost { get; set; }
	public string? Title { get; set; }
	public LayoutNode? Layout { get; set; }
}
