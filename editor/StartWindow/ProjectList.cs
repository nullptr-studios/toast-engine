using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System;
using System.Xml.Serialization;
using System.Linq;

namespace editor.StartWindow;

[XmlRoot("Project")]
public record struct ProjectListItem {
	[XmlAttribute("Name")]
	public string Title {get; set;}

	[XmlAttribute("Path")]
	public string Path {get; set;}

	[XmlAttribute("Date")]
	public string Date {get; set;}

	[XmlAttribute("Version")]
	public string Version {get; set;}

	[XmlAttribute("Thumbnail")]
	public string ThumbnailPath {get; set;}
}

[XmlRoot("ProjectList")]
public class ProjectList {
	[XmlArray("Projects")]
	[XmlArrayItem("Project")]
	public ObservableCollection<ProjectListItem> Projects {get; set;} = [];

	private static string m_path = Path.Combine(
		Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
		"ToastEngine",
		"project_list.xml"
	);

	private ProjectList() { }

	public static ProjectList LoadList() {
		string? dir = Path.GetDirectoryName(m_path);
		if (dir != null && !Directory.Exists(dir)) {
			Directory.CreateDirectory(dir);
		}

		if (!File.Exists(m_path)) return new ProjectList();

		var serializer = new XmlSerializer(typeof(ProjectList));
		using FileStream stream = File.OpenRead(m_path);
		var loaded = serializer.Deserialize(stream) as ProjectList ?? new ProjectList();
		
		// Sort by date (newest first)
		var sorted = loaded.Projects.OrderByDescending(p => {
			if (DateTime.TryParseExact(p.Date, "dd MMM yyyy HH:mm", null, System.Globalization.DateTimeStyles.None, out var parsedDate)) {
				return parsedDate;
			}
			return DateTime.MinValue;
		}).ToList();
		
		loaded.Projects = new ObservableCollection<ProjectListItem>(sorted);
		return loaded;
	}

	public void SaveList() {
		var serializer = new XmlSerializer(typeof(ProjectList));
		using FileStream stream = File.Create(m_path);
		serializer.Serialize(stream, this);
	}
}
