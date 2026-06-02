using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System;
using System.Xml.Serialization;
using System.Linq;

namespace editor.StartWindow;

[XmlRoot("Project")]
public struct ProjectListItem {
	[XmlAttribute("Name")]
	public string title {get; set;}

	[XmlAttribute("Path")]
	public string path {get; set;}

	[XmlAttribute("Date")]
	public string date {get; set;}

	[XmlAttribute("Version")]
	public string version {get; set;}

	[XmlAttribute("Thumbnail")]
	public string thumbnail_path {get; set;}
}

[XmlRoot("ProjectList")]
public class ProjectList {
	[XmlArray("Projects")]
	[XmlArrayItem("Project")]
	public ObservableCollection<ProjectListItem> projects {get; set;} = [];

	private static string m_path = Path.Combine(
		Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
		"ToastEngine",
		"project_list.xml"
	);

	private ProjectList() { }

	public static ProjectList loadList() {
		string? dir = Path.GetDirectoryName(m_path);
		if (dir != null && !Directory.Exists(dir)) {
			Directory.CreateDirectory(dir);
		}

		if (!File.Exists(m_path)) return new ProjectList();

		var serializer = new XmlSerializer(typeof(ProjectList));
		using FileStream stream = File.OpenRead(m_path);
		var loaded = serializer.Deserialize(stream) as ProjectList ?? new ProjectList();
		
		// Sort by date (newest first)
		var sorted = loaded.projects.OrderByDescending(p => {
			if (DateTime.TryParseExact(p.date, "dd MMM yyyy HH:mm", null, System.Globalization.DateTimeStyles.None, out var parsedDate)) {
				return parsedDate;
			}
			return DateTime.MinValue;
		}).ToList();
		
		loaded.projects = new ObservableCollection<ProjectListItem>(sorted);
		return loaded;
	}

	public void saveList() {
		var serializer = new XmlSerializer(typeof(ProjectList));
		using FileStream stream = File.Create(m_path);
		serializer.Serialize(stream, this);
	}
}
