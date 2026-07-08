using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json.Nodes;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Dock.Model.Controls;
using Dock.Model.Mvvm.Controls;
using editor.Assets;
using editor.Assets.Types;
using editor.Components.Modals;
using editor.Editors;
using editor.Engine;
using Proto.Events;

namespace editor.Workspace;

public partial class MainWindowViewModel : ViewModelBase {
	private readonly AutosaveService m_autosave;
	private readonly DockFactory m_dockFactory;
	private readonly ToastEngine m_toast;
	private readonly ToastZoneFactory m_toastZoneFactory;

	private readonly Dictionary<ulong, WorkspaceViewModel> m_workspaces = [];
	private ulong m_activeWorkspaceHandle;
	[ObservableProperty] private bool m_curveEditorVisible = true;
	[ObservableProperty] private bool m_genericEditorVisible;
	[ObservableProperty] private bool m_hapticsEditorVisible = true;

	[ObservableProperty] private bool m_hierarchyVisible = true;
	[ObservableProperty] private bool m_inspectorVisible = true;
	[ObservableProperty] private bool m_logsVisible = true;
	[ObservableProperty] private bool m_schemaEditorVisible;

	[ObservableProperty] private bool m_toastZoneActive;
	private bool m_toastZonePinned;

	public MainWindowViewModel(ToastEngine toast) {
		m_toast = toast;

		m_dockFactory = new DockFactory();
		MainLayout = m_dockFactory.CreateLayout();
		m_dockFactory.InitLayout(MainLayout);

		m_toastZoneFactory = new ToastZoneFactory();
		ToastZoneLayout = m_toastZoneFactory.CreateLayout();
		m_toastZoneFactory.InitLayout(ToastZoneLayout);

		m_dockFactory.DockableClosed += (_, e) => {
			if (e.Dockable is WorkspaceViewModel ws) m_workspaces.Remove(ws.Handle);
			if (e.Dockable == m_dockFactory.Hierarchy) m_hierarchyVisible = false;
			if (e.Dockable == m_dockFactory.Inspector) m_inspectorVisible = false;
			if (e.Dockable == m_dockFactory.GenericEditorVm) m_genericEditorVisible = false;
			if (e.Dockable == m_dockFactory.SchemaEditorVm) m_schemaEditorVisible = false;

			OnPropertyChanged(nameof(HierarchyVisible));
			OnPropertyChanged(nameof(InspectorVisible));
			OnPropertyChanged(nameof(GenericEditorVisible));
			OnPropertyChanged(nameof(SchemaEditorVisible));

			if (m_workspaces.Count == 0) {
				m_activeWorkspaceHandle = 0;
				m_dockFactory.Hierarchy?.Clear();
				Events.Send(new SetActiveWorkspace { Handle = 0 });
			} else {
				m_activeWorkspaceHandle = 0;
				SyncActiveWorkspace();
			}
		};

		m_toastZoneFactory.DockableClosed += (_, e) => {
			if (e.Dockable == m_toastZoneFactory.LogsVm) m_logsVisible = false;
			if (e.Dockable == m_toastZoneFactory.HapticsEditorVm) m_hapticsEditorVisible = false;
			if (e.Dockable == m_toastZoneFactory.CurveEditorVm) m_curveEditorVisible = false;

			OnPropertyChanged(nameof(LogsVisible));
			OnPropertyChanged(nameof(HapticsEditorVisible));
			OnPropertyChanged(nameof(CurveEditorVisible));
		};

		m_dockFactory.ActiveDockableChanged += (_, _) => {
			SyncActiveWorkspace();
			PlayCommand.NotifyCanExecuteChanged();
			PlayInWindowCommand.NotifyCanExecuteChanged();
		};

		WorkspaceState.Modified += () => {
			if (m_dockFactory.ActiveWorkspace is { } ws) ws.IsModified = true;
		};

		m_dockFactory.SchemaEditorVm!.SchemaSaved +=
			path => m_dockFactory.GenericEditorVm?.RefreshFromSchema(path);

		EditorManager.OpenRequested += OnEditorOpenRequested;

		WorkspaceViewModel.PlayModeChanged += () => {
			SaveCurrentNodeCommand.NotifyCanExecuteChanged();
			SaveCurrentNodeAsCommand.NotifyCanExecuteChanged();
			SaveAllNodesCommand.NotifyCanExecuteChanged();
			PlayCommand.NotifyCanExecuteChanged();
			PlayInWindowCommand.NotifyCanExecuteChanged();
		};

		m_autosave = new AutosaveService(EnumerateAutosavables);
	}

	public IRootDock MainLayout { get; set; }
	public IRootDock ToastZoneLayout { get; set; }

	private IEnumerable<IAutosavable> EnumerateAutosavables() {
		foreach (var ws in m_workspaces.Values) yield return ws;
		if (m_dockFactory.GenericEditorVm is { } generic) yield return generic;
		if (m_dockFactory.SchemaEditorVm is { } schema) yield return schema;
		if (m_toastZoneFactory.CurveEditorVm is { } curve) yield return curve;
		if (m_toastZoneFactory.HapticsEditorVm is { } haptics) yield return haptics;
	}

	[RelayCommand]
	private Task AutosaveNow() {
		return m_autosave.RequestAutosave();
	}

	[RelayCommand]
	private void OpenProjectSettings() {
		if (!ProjectContext.IsInitialized) return;

		// Find the .toast project file in the project root
		var toastFile = Directory.EnumerateFiles(ProjectContext.ProjectPath, "*.toast").FirstOrDefault();
		if (toastFile is null) return;

		var virtualPath = $"project://{Path.GetFileName(toastFile)}";
		var definition  = new ProjectSettingsAsset();

		// Use the filename as a synthetic uid so the editor can track the open file
		m_dockFactory.OpenGenericEditor(Path.GetFileNameWithoutExtension(toastFile), virtualPath, definition);
		GenericEditorVisible = true;
	}

	partial void OnHierarchyVisibleChanged(bool value) {
		if (value != m_dockFactory.IsToolVisible("Hierarchy"))
			m_dockFactory.ToggleTool("Hierarchy");
	}

	partial void OnInspectorVisibleChanged(bool value) {
		if (value != m_dockFactory.IsToolVisible("Inspector"))
			m_dockFactory.ToggleTool("Inspector");
	}

	partial void OnGenericEditorVisibleChanged(bool value) {
		if (value != m_dockFactory.IsToolVisible("GenericEditor"))
			m_dockFactory.ToggleTool("GenericEditor");
	}

	partial void OnSchemaEditorVisibleChanged(bool value) {
		if (value != m_dockFactory.IsToolVisible("SchemaEditor"))
			m_dockFactory.ToggleTool("SchemaEditor");
	}

	partial void OnLogsVisibleChanged(bool value) {
		if (value != m_toastZoneFactory.IsToolVisible("Logs"))
			m_toastZoneFactory.ToggleTool("Logs");
	}

	partial void OnHapticsEditorVisibleChanged(bool value) {
		if (value != m_toastZoneFactory.IsToolVisible("Haptics"))
			m_toastZoneFactory.ToggleTool("Haptics");
	}

	partial void OnCurveEditorVisibleChanged(bool value) {
		if (value != m_toastZoneFactory.IsToolVisible("Curve"))
			m_toastZoneFactory.ToggleTool("Curve");
	}

	private async void OnEditorOpenRequested(AssetFile file) {
		if (file.Definition is not { CanBeEdited: true } def) return;
		if (file.Uid is not { } uid) return;
		if (!AssetDatabase.TryResolve(uid, out var virtualPath, out _)) return;

		var recoverPath = await AutosaveService.TryRecoverAsync(uid, virtualPath);

		switch (def.EditorTool) {
			case "GenericEditor":
				m_dockFactory.OpenGenericEditor(uid, virtualPath, def, recoverPath);
				GenericEditorVisible = true; // ShowRightTool already ran; IsToolVisible = true → no re-toggle
				break;
			case "SchemaEditor":
				m_dockFactory.OpenSchemaEditor(uid, virtualPath, recoverPath);
				SchemaEditorVisible = true;
				break;
			case "NodeEditor":
				OpenWorkspaceFile(uid, virtualPath, recoverPath);
				break;
			case "CurveEditor":
				if (m_toastZoneFactory.CurveEditorVm is { } curveVm) {
					_ = OpenToastEditorAsync(curveVm, uid, virtualPath, def, recoverPath);
					CurveEditorVisible = true;
				}

				break;
			case "HapticsEditor":
				if (m_toastZoneFactory.HapticsEditorVm is { } hapticsVm) {
					_ = OpenToastEditorAsync(hapticsVm, uid, virtualPath, def, recoverPath);
					HapticsEditorVisible = true;
				}

				break;
		}
	}

	private void OpenWorkspaceFile(string uid, string virtualPath, string? recoverPath) {
		var recoverVirtual = recoverPath is null ? null : ProjectContext.ToVirtual(recoverPath);
		if (WorkspaceViewModel.OpenFile(m_toast, uid, virtualPath, recoverVirtual) is not { } ws) return;
		m_workspaces[ws.Handle] = m_dockFactory.AddWorkspace(ws);
		if (recoverVirtual is not null) ws.IsModified = true; // recovered content is unsaved by definition
		SyncActiveWorkspace();
	}

	private async Task OpenToastEditorAsync<T>(
		T editor, string uid, string virtualPath, BaseAsset definition, string? contentSourceRealPath = null)
		where T : Tool, IToastZoneEditor {
		if (editor.IsDirty && !await editor.ConfirmCloseCurrentAsync()) return;
		editor.OpenFile(uid, virtualPath, definition, contentSourceRealPath);
		m_toastZoneFactory.ShowTool(editor);

		// pin the zone so it stays up while editing
		m_toastZonePinned = true;
		ToastZoneActive = true;
	}

	private void SyncActiveWorkspace() {
		// a playing tab routes to its temporary play clone, not the frozen editing workspace
		var handle = m_dockFactory.ActiveWorkspace?.EffectiveHandle ?? 0;
		if (handle == m_activeWorkspaceHandle) return;
		m_activeWorkspaceHandle = handle;
		Events.Send(new SetActiveWorkspace { Handle = handle });
	}

	public void ShowToastZone(bool active) {
		if (m_toastZonePinned) return;
		ToastZoneActive = active;
	}

	public void PinToastZone() {
		m_toastZonePinned = !m_toastZonePinned;
		ToastZoneActive = m_toastZonePinned;
	}

	[RelayCommand]
	private async Task NewNode() {
		if (App.MainWindow is not { } owner) return;
		var popup = new NodeTypeTree();
		var result = await popup.ShowDialog<string?>(owner);
		if (result is null) return;

		if (WorkspaceViewModel.CreateNew(m_toast, result) is not { } ws) return;
		m_workspaces[ws.Handle] = m_dockFactory.AddWorkspace(ws);
		ws.IsModified = true;
		SyncActiveWorkspace();
	}

	[RelayCommand]
	private async Task OpenNodeFile() {
		if (App.MainWindow is not { } owner) return;
		var uid = await new AssetList("Node").ShowDialog<string?>(owner);
		if (uid is null) return;

		if (!AssetDatabase.TryResolve(uid, out var virtualPath, out _)) return;

		var recoverPath = await AutosaveService.TryRecoverAsync(uid, virtualPath);
		OpenWorkspaceFile(uid, virtualPath, recoverPath);
	}

	[RelayCommand]
	private void CloseNodeFile() {
		if (m_dockFactory.ActiveWorkspace is { } ws) m_dockFactory.CloseDockable(ws);
	}

	// saving is locked while any tab is in play mode
	private static bool CanSave() {
		return !WorkspaceViewModel.AnyPlayActive;
	}

	[RelayCommand(CanExecute = nameof(CanSave))]
	private async Task SaveCurrentNode() {
		if (m_dockFactory.ActiveWorkspace is { } ws) await ws.Save();
	}

	[RelayCommand(CanExecute = nameof(CanSave))]
	private async Task SaveCurrentNodeAs() {
		if (m_dockFactory.ActiveWorkspace is { } ws) await ws.SaveAs();
	}

	[RelayCommand(CanExecute = nameof(CanSave))]
	private async Task SaveAllNodes() {
		foreach (var ws in m_workspaces.Values) await ws.Save();
	}

	private static bool CanReloadGame() => ProjectContext.IsInitialized;

	private static bool CanCompileGameRelease() => ProjectContext.IsInitialized;

	private bool CanPlay() => m_dockFactory.ActiveWorkspace is { } ws && ws.TogglePlayCommand.CanExecute(null);

	private bool CanPlayInWindow() => m_dockFactory.ActiveWorkspace is { } ws && ws.TogglePlayExternalCommand.CanExecute(null);

	[RelayCommand(CanExecute = nameof(CanPlay))]
	private void Play() {
		if (m_dockFactory.ActiveWorkspace is { } ws)
			ws.TogglePlayCommand.Execute(null);
	}

	[RelayCommand(CanExecute = nameof(CanPlayInWindow))]
	private void PlayInWindow() {
		if (m_dockFactory.ActiveWorkspace is { } ws)
			ws.TogglePlayExternalCommand.Execute(null);
	}

	[RelayCommand(CanExecute = nameof(CanReloadGame))]
	private async Task ReloadGame() {
		if (App.MainWindow is not { } owner) return;
		if (!ProjectContext.IsInitialized) return;

		string toastPath = Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "..", "toast_engine"));
		var cmakeGenerator = OperatingSystem.IsWindows() ? "-G \"Visual Studio 18 2026\"" : "-G \"Ninja\"";

		var tasks = new List<LoaderTask>();

		// Generate game reflection data before cmake
		var libSrc = Path.Combine(ProjectContext.ProjectPath, "lib", "src");
		var libGenerated = Path.Combine(ProjectContext.ProjectPath, "lib", "generated");
		Directory.CreateDirectory(libGenerated);

		var exeExt = OperatingSystem.IsWindows() ? ".exe" : "";
		var refgen = Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,
			"..", "reflection_generator", $"reflection_generator{exeExt}"));
		var gameDb = ProjectContext.Resolve("cache://game_reflect.json");
		tasks.Add(LoaderTask.Run("Generate game reflection", refgen,
			$"--database \"{gameDb}\" --output \"{libGenerated}\" --input \"{libSrc}\" " +
			$"--include-root \"{libSrc}\" --register-fn registerGameTypes --attribute Game"));

		// Copy engine reflection database to cache
		tasks.Add(LoaderTask.Do("Copy engine reflection", async log => {
			var src = Path.Combine(ProjectContext.CorePath, "engine_reflect.json");
			var dst = ProjectContext.Resolve("cache://engine_reflect.json");
			if (File.Exists(src)) {
				File.Copy(src, dst, true);
				log("Copied engine_reflect.json");
			}
			await Task.CompletedTask;
		}));

		tasks.Add(LoaderTask.Run(
			"cmake configure",
			"cmake",
			$"lib/ -B .toast/cmake_cache {cmakeGenerator} -DTOAST_PATH={toastPath}"
		));
		tasks.Add(LoaderTask.Run(
			"cmake build",
			"cmake",
			"--build .toast/cmake_cache"
		));

		tasks.Add(LoaderTask.Do("Reload game", async log => {
			m_toast.ReloadGame();
			await Task.CompletedTask;
		}));

		var vm = new LoaderViewModel(tasks) {
			OnComplete = async () => {
				ReflectionDatabase.Update();
				AssetDatabase.RebuildAssetDatabase();
				if (HierarchyViewModel.Current is { } hvm) {
					hvm.SelectedNode = null;
				}
				await Task.CompletedTask;
			}
		};

		await new SimpleLoaderWindow(vm).ShowDialog(owner);
	}

	[RelayCommand(CanExecute = nameof(CanCompileGameRelease))]
	private async Task CompileGameRelease() {
		if (App.MainWindow is not { } owner) return;
		if (!ProjectContext.IsInitialized) return;

		string playerPath   = Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "..", "..", "..", "tools", "player"));
		string toastPath    = Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "..", "toast_engine"));
		string packerBin    = Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "..", "packer", $"packer{(OperatingSystem.IsWindows() ? ".exe" : "")}"));
		string outputDir    = Path.GetFullPath(Path.Combine(ProjectContext.ProjectPath, "build"));
		string stageRoot    = Path.GetFullPath(Path.Combine(ProjectContext.ProjectPath, ".toast", "pack_stage"));
		var cmakeGenerator  = OperatingSystem.IsWindows() ? "-G \"Visual Studio 18 2026\"" : "-G \"Ninja\"";

		Directory.CreateDirectory(outputDir);

		async Task CopyDlls(Action<string> log) {
			var libExt = OperatingSystem.IsWindows() ? ".dll" : ".so";
			var exeExt = OperatingSystem.IsWindows() ? ".exe" : "";
			var extensions = new[] { libExt, exeExt }.Where(e => !string.IsNullOrEmpty(e)).ToArray();
			var sources = new[] {
				Path.Combine(toastPath, "bin"),
				Path.Combine(ProjectContext.ProjectPath, "build"),
			};
			foreach (var src in sources) {
				if (!Directory.Exists(src)) { log($"warning: source dir not found: {src}"); continue; }
				var files = Directory.EnumerateFiles(src, "*.*", SearchOption.TopDirectoryOnly)
					.Where(f => extensions.Contains(Path.GetExtension(f), StringComparer.OrdinalIgnoreCase));
				foreach (var file in files) {
					var dest = Path.Combine(outputDir, Path.GetFileName(file));
					log($"  copy {Path.GetFileName(file)}");
					await Task.Run(() => File.Copy(file, dest, overwrite: true));
				}
			}
		}

		async Task CopyProjectToast(Action<string> log) {
			var toastFile = Directory.EnumerateFiles(ProjectContext.ProjectPath, "*.toast").FirstOrDefault();
			if (toastFile is null) { log("warning: no .toast file found in project root"); return; }
			var dest = Path.Combine(outputDir, Path.GetFileName(toastFile));
			log($"  copy {Path.GetFileName(toastFile)}");
			await Task.Run(() => File.Copy(toastFile, dest, overwrite: true));
		}

		async Task BakeAndPack(Action<string> log, string dbName, string dbSourceDir, string manifestJsonPath) {
			var stageDir = Path.Combine(stageRoot, dbName);
			if (Directory.Exists(stageDir)) Directory.Delete(stageDir, recursive: true);
			Directory.CreateDirectory(stageDir);

			// Copy all source files into staging
			log($"  staging {dbName}/ ...");
			await Task.Run(() => {
				foreach (var srcFile in Directory.EnumerateFiles(dbSourceDir, "*", SearchOption.AllDirectories)) {
					var rel  = Path.GetRelativePath(dbSourceDir, srcFile);
					var dest = Path.Combine(stageDir, rel);
					Directory.CreateDirectory(Path.GetDirectoryName(dest)!);
					File.Copy(srcFile, dest, overwrite: true);
				}
			});

			// Read the manifest JSON to find all node UIDs, then overwrite their staged files with binary
			log($"  baking nodes in {dbName}/ ...");
			if (File.Exists(manifestJsonPath)) {
				var manifestJson = JsonNode.Parse(File.ReadAllText(manifestJsonPath));
				if (manifestJson?["node"] is JsonObject nodeCollection) {
					foreach (var (uid, pathNode) in nodeCollection) {
						var virtualPath = pathNode?.GetValue<string>();
						if (virtualPath is null) continue;
						var sep = virtualPath.IndexOf("://", StringComparison.Ordinal);
						if (sep < 0) continue;
						var rel  = virtualPath[(sep + 3)..];
						var dest = Path.Combine(stageDir, rel.Replace('/', Path.DirectorySeparatorChar));
						Directory.CreateDirectory(Path.GetDirectoryName(dest)!);
						log($"    bake {rel}");
						await Task.Run(() => ToastEngine.BakeAsset(uid, dest));
					}
				}
			}

			// Copy the manifest JSON into the stage folder root
			var manifestDest = Path.Combine(stageDir, $"{dbName}.json");
			if (File.Exists(manifestJsonPath)) {
				log($"  copying manifest {dbName}.json");
				await Task.Run(() => File.Copy(manifestJsonPath, manifestDest, overwrite: true));
			} else {
				log($"  warning: manifest not found at {manifestJsonPath}");
			}

			// Run the Rust packer on the staging directory
			var pakOut = Path.Combine(outputDir, $"{dbName}.pak");
			log($"  packing → {dbName}.pak");
			await Task.Run(() => {
				using var proc = new System.Diagnostics.Process();
				proc.StartInfo = new System.Diagnostics.ProcessStartInfo {
					FileName               = packerBin,
					Arguments              = $"\"{stageDir}\" \"{pakOut}\"",
					WorkingDirectory       = ProjectContext.ProjectPath,
					RedirectStandardOutput = true,
					RedirectStandardError  = true,
					UseShellExecute        = false,
					CreateNoWindow         = true,
				};
				proc.OutputDataReceived += (_, e) => { if (e.Data is not null) log(e.Data); };
				proc.ErrorDataReceived  += (_, e) => { if (e.Data is not null) log(e.Data); };
				proc.Start();
				proc.BeginOutputReadLine();
				proc.BeginErrorReadLine();
				proc.WaitForExit();
				if (proc.ExitCode != 0)
					throw new Exception($"packer exited with code {proc.ExitCode} for {dbName}");
			});
		}

		AssetDatabase.RebuildAssetDatabase();

		var tasks = new List<LoaderTask> {
			// Publish player
			LoaderTask.Run(
				"dotnet publish player",
				"dotnet",
				$"publish \"{playerPath}\" -c Release -p:PublishAot=true -p:PublishSingleFile=true -p:OptimizationPreference=Speed -o \"{outputDir}\""
			),
			// Generate CMake on Release
			LoaderTask.Run(
				"cmake configure (Release)",
				"cmake",
				$"lib/ -B .toast/cmake_release_cache {cmakeGenerator} -DTOAST_PATH={toastPath} -DCMAKE_BUILD_TYPE=Release"
			),
			// Build Game DLL on Release
			LoaderTask.Run(
				"cmake build (Release)",
				"cmake",
				"--build .toast/cmake_release_cache --config Release --parallel"
			),
			// Copy Engine libs
			LoaderTask.Do("copy libraries", CopyDlls),
			// Copy project.toast
			LoaderTask.Do("copy project.toast", CopyProjectToast),
		};

		// Bake assets
		foreach (var db in ProjectContext.Databases) {
			var dbSource   = Path.Combine(ProjectContext.ProjectPath, db);
			var manifestJs = Path.Combine(ProjectContext.CachePath, $"{db}.json");
			var dbCapture  = db;
			tasks.Add(LoaderTask.Do($"bake & pack {db}://", (log) => BakeAndPack(log, dbCapture, dbSource, manifestJs)));
		}

		// Bake core
		var coreManifest = Path.Combine(ProjectContext.CachePath, "core.json");
		tasks.Add(LoaderTask.Do("bake & pack core://", (log) => BakeAndPack(log, "core", ProjectContext.CorePath, coreManifest)));

		var vm = new LoaderViewModel(tasks);
		await new SimpleLoaderWindow(vm).ShowDialog(owner);
	}

}
