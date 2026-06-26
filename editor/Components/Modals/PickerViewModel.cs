using System.Collections;
using System.Threading.Tasks;
using Avalonia.Controls;
using Lucide.Avalonia;

namespace editor.Components.Modals;

public abstract class PickerViewModel {
	public abstract string WindowTitle { get; }
	public abstract IEnumerable Items { get; }

	public virtual string AcceptLabel => "Select";
	public virtual LucideIconKind AcceptIconKind => LucideIconKind.Check;

	// null = no extra button
	public virtual string? ExtraButtonLabel => null;
	public virtual LucideIconKind ExtraIconKind => LucideIconKind.Plus;

	public abstract void UpdateFilter(string query, bool caseSensitive);
	public virtual bool IsSelectable(object? item) => item is not null;
	public abstract string? GetResult(object? selected);
	public virtual Task OnExtraButton(Window owner, object? selected) => Task.CompletedTask;
}
