//
// IToastZoneEditor.cs by Xein
// 04 Jul 2026
//

using System.Threading.Tasks;
using editor.Assets.Types;

namespace editor.Editors;

// Contract for asset editors docked in the Toast Zone
// MainWindowViewModel routes Asset Browser double-clicks through this
public interface IToastZoneEditor {
	bool IsDirty { get; }
	Task<bool> ConfirmCloseCurrentAsync();
	void OpenFile(string uid, string virtualPath, BaseAsset definition);
}
