use ratatui::{
    layout::{Constraint, Direction, Layout, Rect, Alignment},
    style::{Color, Modifier, Style},
    text::{Span, Line, Text},
    widgets::{Block, Borders, Cell, Clear, Paragraph, Row, Table, BorderType, List, ListItem},
    Frame,
};
use crate::app::{App, ConnectionState, PopupOption, Severity, AppFocus};

pub fn draw(f: &mut Frame, app: &App) {
    let size = f.area();

    match &app.state {
        ConnectionState::Disconnected | ConnectionState::Connecting(_) => {
            render_disconnected(f, app, size);
        }
        ConnectionState::Connected | ConnectionState::CsvMode(_) => {
            render_connected(f, app, size);
        }
    }
}

fn render_disconnected(f: &mut Frame, app: &App, area: Rect) {
    // Make popup as small as possible
    let area = centered_rect(40, 8, area); 
    
    let block = Block::default()
        .borders(Borders::ALL)
        .title(" Kenzo ")
        .title_alignment(Alignment::Center)
        .border_type(BorderType::Rounded)
        .border_style(Style::default().fg(Color::Yellow));

    f.render_widget(Clear, area);
    f.render_widget(block, area);

    let chunks = Layout::default()
        .direction(Direction::Vertical)
        .margin(1)
        .constraints([
            Constraint::Length(1), // Localhost
            Constraint::Length(1), // Remote IP
            Constraint::Length(1), // CSV
        ])
        .split(area);

    let spinner = ["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"];
    let spinner_char = spinner[app.spinner_frame % spinner.len()];

    // Localhost
    let localhost_style = if app.popup_selection == PopupOption::Localhost {
        Style::default().fg(Color::Yellow).add_modifier(Modifier::BOLD)
    } else {
        Style::default().fg(Color::Gray)
    };
    let is_connecting_localhost = match &app.state {
        ConnectionState::Connecting(addr) if addr.contains("127.0.0.1") => true,
        _ => false,
    };
    let localhost_text = if is_connecting_localhost {
        format!("{} Connecting to Localhost...", spinner_char)
    } else {
        "  Connect to Localhost".to_string()
    };
    f.render_widget(Paragraph::new(localhost_text).style(localhost_style), chunks[0]);

    // Remote IP
    let remote_style = if app.popup_selection == PopupOption::RemoteIp {
        Style::default().fg(Color::Yellow).add_modifier(Modifier::BOLD)
    } else {
        Style::default().fg(Color::Gray)
    };
    let is_connecting_remote = match &app.state {
        ConnectionState::Connecting(addr) if !addr.contains("127.0.0.1") => true,
        _ => false,
    };
    let remote_input = if app.popup_selection == PopupOption::RemoteIp { app.input_buffer.value() } else { "" };
    let remote_text = if is_connecting_remote {
        format!("{} Connecting to {}:12801...", spinner_char, remote_input)
    } else {
        format!("  Connect to IP: {}", remote_input)
    };
    f.render_widget(Paragraph::new(remote_text).style(remote_style), chunks[1]);

    // CSV
    let csv_style = if app.popup_selection == PopupOption::CsvFile {
        Style::default().fg(Color::Yellow).add_modifier(Modifier::BOLD)
    } else {
        Style::default().fg(Color::Gray)
    };
    let csv_input = if app.popup_selection == PopupOption::CsvFile { app.input_buffer.value() } else { "" };
    let csv_text = format!("  Open CSV: {}", csv_input);
    f.render_widget(Paragraph::new(csv_text).style(csv_style), chunks[2]);
}

fn render_connected(f: &mut Frame, app: &App, area: Rect) {
    let main_chunks = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Min(10),
            Constraint::Length(3), // Search
            Constraint::Length(1), // Keybinds
        ])
        .split(area);

    let content_chunks = Layout::default()
        .direction(Direction::Horizontal)
        .constraints(if app.show_filters {
            [Constraint::Percentage(20), Constraint::Percentage(80)]
        } else {
            [Constraint::Percentage(0), Constraint::Percentage(100)]
        })
        .split(main_chunks[0]);

    // Filters
    if app.show_filters {
        let filter_chunks = Layout::default()
            .direction(Direction::Vertical)
            .constraints([Constraint::Percentage(50), Constraint::Percentage(50)])
            .split(content_chunks[0]);

        // Severity Filter
        let severity_block = Block::default()
            .borders(Borders::ALL)
            .title(" Severities ")
            .border_type(BorderType::Rounded)
            .border_style(Style::default().fg(if app.focus == AppFocus::FilterSeverity { Color::Yellow } else { Color::DarkGray }));
        
        let mut sevs: Vec<_> = app.seen_severities.iter().collect();
        sevs.sort();
        let sev_items: Vec<ListItem> = sevs.iter().map(|&&s| {
            let label = match s { 0 => "TRACE", 1 => "INFO", 2 => "WARNING", 3 => "ERROR", 4 => "CRITICAL", _ => "UNKNOWN" };
            let checked = if app.excluded_severities.contains(&s) { "[ ]" } else { "[x]" };
            ListItem::new(format!("{} {}", checked, label))
        }).collect();
        f.render_stateful_widget(List::new(sev_items).block(severity_block).highlight_style(Style::default().add_modifier(Modifier::BOLD).fg(Color::Yellow)), filter_chunks[0], &mut app.severity_list_state.clone());

        // Sink Filter
        let sink_block = Block::default()
            .borders(Borders::ALL)
            .title(" Sinks ")
            .border_type(BorderType::Rounded)
            .border_style(Style::default().fg(if app.focus == AppFocus::FilterSink { Color::Yellow } else { Color::DarkGray }));
        
        let mut sinks: Vec<_> = app.seen_sinks.iter().collect();
        sinks.sort();
        let sink_items: Vec<ListItem> = sinks.iter().map(|&s| {
            let checked = if app.excluded_sinks.contains(s) { "[ ]" } else { "[x]" };
            ListItem::new(format!("{} {}", checked, s))
        }).collect();
        f.render_stateful_widget(List::new(sink_items).block(sink_block).highlight_style(Style::default().add_modifier(Modifier::BOLD).fg(Color::Yellow)), filter_chunks[1], &mut app.sink_list_state.clone());
    }

    // Table
    render_table(f, app, content_chunks[1]);

    // Search
    let search_block = Block::default()
        .borders(Borders::ALL)
        .title(" Search (/) ")
        .border_type(BorderType::Rounded)
        .border_style(Style::default().fg(if app.focus == AppFocus::Search { Color::Yellow } else { Color::DarkGray }));
    let search_text = if app.search_query.is_empty() && app.focus != AppFocus::Search { Span::styled("Type / to search...", Style::default().fg(Color::DarkGray)) } else { Span::raw(&app.search_query) };
    f.render_widget(Paragraph::new(search_text).block(search_block), main_chunks[1]);

    // Keybinds
    let keybinds = Line::from(vec![
        Span::styled(" q ", Style::default().fg(Color::Gray).bg(Color::DarkGray)), Span::raw(" Quit |"),
        Span::styled(" f ", Style::default().fg(Color::Gray).bg(Color::DarkGray)), Span::raw(" Filters |"),
        Span::styled(" d ", Style::default().fg(Color::Gray).bg(Color::DarkGray)), Span::raw(" Auto-scroll |"),
        Span::styled(" / ", Style::default().fg(Color::Gray).bg(Color::DarkGray)), Span::raw(" Search |"),
        Span::styled(" hjkl ", Style::default().fg(Color::Gray).bg(Color::DarkGray)), Span::raw(" Move |"),
        Span::styled(" Shift+HJKL ", Style::default().fg(Color::Gray).bg(Color::DarkGray)), Span::raw(" Switch Focus "),
    ]);
    f.render_widget(Paragraph::new(keybinds).alignment(Alignment::Center), main_chunks[2]);
}

fn render_table(f: &mut Frame, app: &App, area: Rect) {
    let mut title = " Kenzo Logs ".to_string();
    if app.scroll_locked {
        title = " Kenzo Logs 🔒 ".to_string();
    }

    let table_block = Block::default()
        .borders(Borders::ALL)
        .title(title)
        .border_type(BorderType::Rounded)
        .border_style(Style::default().fg(if app.focus == AppFocus::Table { Color::Yellow } else { Color::DarkGray }));

    let widths = [
        Constraint::Length(15), // Time
        Constraint::Length(12), // Severity
        Constraint::Length(15), // Sink
        Constraint::Length(25), // File
        Constraint::Min(50),    // Message
    ];

    // Calculate approximate width for the message column to perform manual wrapping
    // This is a rough estimation because layout can change
    let table_width = area.width;
    let msg_col_width = table_width.saturating_sub(17 + 12 + 15 + 25 + 10) as usize; // subtracting column sizes + separators

    let rows: Vec<Row> = app.filtered_logs.iter().map(|&log_idx| {
        let log = &app.logs[log_idx];
        let severity = Severity::from(log.severity);
        let (fg, bg) = match severity {
            Severity::Trace => (Color::Gray, Color::Reset), // Lighter gray for trace
            Severity::Info => (Color::Green, Color::Reset),
            Severity::Warning => (Color::Yellow, Color::Reset),
            Severity::Error => (Color::Red, Color::Reset),
            Severity::Critical => (Color::Black, Color::Red),
        };
        let style = Style::default().fg(fg).bg(bg);
        let highlight_q = app.search_query.to_lowercase();
        
        let sev_label = match log.severity { 0 => "TRACE", 1 => "INFO", 2 => "WARNING", 3 => "ERROR", 4 => "CRITICAL", _ => "UNKNOWN" };
        let sev_str = format!("[{}]", sev_label);
        let time_str = format!("{}  ", format_timestamp(log.timestamp)); // Two characters padding
        let sink_str = format!("[{}]", log.sink);
        let file_str = format!("{}:{}", log.filepath, log.line_number);
        let msg_str = log.message.replace('\t', "    ");

        let mut wrapped_lines = Vec::new();
        for line in msg_str.lines() {
            let wrapped = textwrap::fill(line, msg_col_width);
            for subline in wrapped.lines() {
                wrapped_lines.push(subline.to_string());
            }
        }
        
        let row_height = wrapped_lines.len().max(1) as u16;

        let msg_text = Text::from(wrapped_lines.iter().map(|l| Line::from(highlight_text(l, &highlight_q, if severity == Severity::Critical { style } else if severity == Severity::Trace { style } else { Style::default().fg(Color::White) }))).collect::<Vec<_>>());

        Row::new(vec![
            Cell::from(Line::from(highlight_text(&time_str, &highlight_q, style))),
            Cell::from(Line::from(highlight_text(&sev_str, &highlight_q, style))),
            Cell::from(Line::from(highlight_text(&sink_str, &highlight_q, style))),
            Cell::from(Line::from(highlight_text(&file_str, &highlight_q, style))),
            Cell::from(msg_text),
        ]).style(style).height(row_height)
    }).collect();

    let table = Table::new(rows, widths)
        .header(Row::new(vec!["Time", "Severity", "Sink", "File", "Message"]).style(Style::default().fg(Color::Cyan).add_modifier(Modifier::BOLD)))
        .block(table_block)
        .column_spacing(1)
        .row_highlight_style(Style::default().add_modifier(Modifier::REVERSED));

    f.render_stateful_widget(table, area, &mut app.table_state.clone());
}

fn highlight_text(text: &str, query: &str, base_style: Style) -> Vec<Span<'static>> {
    if query.is_empty() || !text.to_lowercase().contains(query) {
        return vec![Span::styled(text.to_string(), base_style)];
    }
    let mut spans = Vec::new();
    let mut last_end = 0;
    let text_lower = text.to_lowercase();
    for (start, _) in text_lower.match_indices(query) {
        if start > last_end { spans.push(Span::styled(text[last_end..start].to_string(), base_style)); }
        spans.push(Span::styled(text[start..start + query.len()].to_string(), base_style.bg(Color::Yellow).fg(Color::Black)));
        last_end = start + query.len();
    }
    if last_end < text.len() { spans.push(Span::styled(text[last_end..].to_string(), base_style)); }
    spans
}

fn format_timestamp(ts: u64) -> String {
    use chrono::{DateTime, Utc, TimeZone, Local};
    let seconds = (ts / 1_000_000_000) as i64;
    let nanos = (ts % 1_000_000_000) as u32;
    match Utc.timestamp_opt(seconds, nanos) {
        chrono::LocalResult::Single(dt) => {
            let local_dt: DateTime<Local> = DateTime::from(dt);
            local_dt.format("%H:%M:%S.%f").to_string()
        }
        _ => ts.to_string(),
    }
}

fn centered_rect(percent_x: u16, percent_y: u16, r: Rect) -> Rect {
    let popup_layout = Layout::default()
        .direction(Direction::Vertical)
        .constraints([Constraint::Percentage((100 - percent_y) / 2), Constraint::Percentage(percent_y), Constraint::Percentage((100 - percent_y) / 2)])
        .split(r);
    Layout::default()
        .direction(Direction::Horizontal)
        .constraints([Constraint::Percentage((100 - percent_x) / 2), Constraint::Percentage(percent_x), Constraint::Percentage((100 - percent_x) / 2)])
        .split(popup_layout[1])[1]
}
