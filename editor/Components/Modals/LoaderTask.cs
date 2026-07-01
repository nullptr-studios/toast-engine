using System;
using System.Threading.Tasks;

namespace editor.Components.Modals;

/// <summary>A step shown in the loading screen</summary>
public abstract record LoaderTask(string Label) {
	/// <summary>Shell process</summary>
	public static LoaderTask Run(string label, string exe, string args) {
		return new ProcessTask(label, exe, args);
	}

	/// <summary>C# async delegate with a log callback</summary>
	public static LoaderTask Do(string label, Func<Action<string>, Task> action) {
		return new ActionTask(label, action);
	}

	/// <summary>
	/// C# async delegate with a log callback and a fractional progress callback
	/// </summary>
	/// Use this one for large tasks as it can provide with a bit more information
	/// to the user about the state of the current task
	public static LoaderTask DoWithProgress(string label, Func<Action<string>, Action<double>, Task> action) {
		return new ActionTaskWithProgress(label, action);
	}
}

internal sealed record ProcessTask(string Label, string Exe, string Args) : LoaderTask(Label);

internal sealed record ActionTask(string Label, Func<Action<string>, Task> Action) : LoaderTask(Label);

internal sealed record ActionTaskWithProgress(
	string Label,
	Func<Action<string>, Action<double>, Task> Action) : LoaderTask(Label);

public sealed class LoaderTaskException : Exception {
	public LoaderTaskException(string label, string message, Exception? inner = null)
		: base(message, inner) {
		Title = $"Task {label} failed";
	}

	public string Title { get; }
}
