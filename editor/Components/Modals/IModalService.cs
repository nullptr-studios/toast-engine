using System.Threading.Tasks;

namespace editor.Components.Modals;

public enum SaveChangesResult { Cancel, Save, DontSave }

public interface IModalService {
	Task ShowInfo(string title, string message);
	Task ShowWarning(string title, string message);
	Task ShowError(string title, string message);
	Task<bool> ShowConfirm(string title, string message);
	Task<SaveChangesResult> ShowSaveChanges(string filename);
	Task<string?> ShowSaveFile(string defaultPath);
}
