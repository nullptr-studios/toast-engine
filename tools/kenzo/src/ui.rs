use crate::app::{App, AppFocus, ConnectionState, PopupOption, Severity};
use ratatui::{
    layout::{Alignment, Constraint, Direction, Layout, Rect},
    style::{Color, Modifier, Style},
    text::{Line, Span, Text},
    widgets::{Block, BorderType, Borders, Cell, Clear, List, ListItem, Paragraph, Row, Table},
    Frame,
};

pub fn draw(f: &mut Frame, app: &mut App) {
    let size = f.area();

    match &app.state {
        ConnectionState::Disconnected | ConnectionState::Connecting(_) => {
            render_disconnected(f, app, size);
        }
        ConnectionState::Connected
        | ConnectionState::CsvMode(_)
        | ConnectionState::Reconnecting(_) => {
            render_connected(f, app, size);
        }
    }

    // Render detail view on top if open
    if app.detail_view_open {
        render_detail_view(f, app, size);
    }
}

fn render_disconnected(f: &mut Frame, app: &mut App, area: Rect) {
    // Ensure the popup has enough space for all options (at least 10 lines and 60 columns)
    let popup_width = 60.min(area.width);
    let popup_height = 10.min(area.height);
    let area = Rect::new(
        area.x + (area.width.saturating_sub(popup_width)) / 2,
        area.y + (area.height.saturating_sub(popup_height)) / 2,
        popup_width,
        popup_height,
    );

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
            Constraint::Length(1), // Empty
            Constraint::Length(1), // Localhost
            Constraint::Length(1), // Empty
            Constraint::Length(1), // Remote IP
            Constraint::Length(1), // Empty
            Constraint::Length(1), // CSV
            Constraint::Min(0),    // Rest
        ])
        .split(area);

    let spinner = ["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"];
    let spinner_char = spinner[app.spinner_frame % spinner.len()];

    // Localhost
    let localhost_style = if app.popup_selection == PopupOption::Localhost {
        Style::default()
            .fg(Color::Yellow)
            .add_modifier(Modifier::BOLD)
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
    f.render_widget(
        Paragraph::new(localhost_text).style(localhost_style),
        chunks[1],
    );

    // Remote IP
    let remote_style = if app.popup_selection == PopupOption::RemoteIp {
        Style::default()
            .fg(Color::Yellow)
            .add_modifier(Modifier::BOLD)
    } else {
        Style::default().fg(Color::Gray)
    };
    let is_connecting_remote = match &app.state {
        ConnectionState::Connecting(addr) if !addr.contains("127.0.0.1") => true,
        _ => false,
    };
    let remote_input = if app.popup_selection == PopupOption::RemoteIp {
        app.input_buffer.value()
    } else {
        ""
    };
    let remote_text = if is_connecting_remote {
        format!("{} Connecting to {}:12801...", spinner_char, remote_input)
    } else {
        format!("  Connect to IP: {}", remote_input)
    };
    f.render_widget(Paragraph::new(remote_text).style(remote_style), chunks[3]);

    // CSV
    let csv_style = if app.popup_selection == PopupOption::CsvFile {
        Style::default()
            .fg(Color::Yellow)
            .add_modifier(Modifier::BOLD)
    } else {
        Style::default().fg(Color::Gray)
    };
    let csv_input = if app.popup_selection == PopupOption::CsvFile {
        app.input_buffer.value()
    } else {
        ""
    };
    let csv_text = format!("  Open CSV: {}", csv_input);
    f.render_widget(Paragraph::new(csv_text).style(csv_style), chunks[5]);
}

fn render_connected(f: &mut Frame, app: &mut App, area: Rect) {
    let is_reconnecting = matches!(app.state, ConnectionState::Reconnecting(_));

    let main_chunks = Layout::default()
        .direction(Direction::Vertical)
        .constraints(if is_reconnecting {
            vec![
                Constraint::Min(10),
                Constraint::Length(3), // Search
                Constraint::Length(1), // Keybinds
                Constraint::Length(1), // Status bar
            ]
        } else {
            vec![
                Constraint::Min(10),
                Constraint::Length(3), // Search
                Constraint::Length(1), // Keybinds
            ]
        })
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
            .border_style(
                Style::default().fg(if app.focus == AppFocus::FilterSeverity {
                    Color::Yellow
                } else {
                    Color::DarkGray
                }),
            );

        let mut sevs: Vec<_> = app.seen_severities.iter().collect();
        sevs.sort();
        let sev_items: Vec<ListItem> = sevs
            .iter()
            .map(|&&s| {
                let label = match s {
                    0 => "TRACE",
                    1 => "INFO",
                    2 => "WARNING",
                    3 => "ERROR",
                    4 => "CRITICAL",
                    _ => "UNKNOWN",
                };
                let checked = if app.excluded_severities.contains(&s) {
                    "[ ]"
                } else {
                    "[x]"
                };
                ListItem::new(format!("{} {}", checked, label))
            })
            .collect();
        f.render_stateful_widget(
            List::new(sev_items).block(severity_block).highlight_style(
                Style::default()
                    .add_modifier(Modifier::BOLD)
                    .fg(Color::Yellow),
            ),
            filter_chunks[0],
            &mut app.severity_list_state,
        );

        // Sink Filter
        let sink_block = Block::default()
            .borders(Borders::ALL)
            .title(" Sinks ")
            .border_type(BorderType::Rounded)
            .border_style(Style::default().fg(if app.focus == AppFocus::FilterSink {
                Color::Yellow
            } else {
                Color::DarkGray
            }));

        let mut sinks: Vec<_> = app.seen_sinks.iter().collect();
        sinks.sort();
        let sink_items: Vec<ListItem> = sinks
            .iter()
            .map(|&s| {
                let checked = if app.excluded_sinks.contains(s) {
                    "[ ]"
                } else {
                    "[x]"
                };
                ListItem::new(format!("{} {}", checked, s))
            })
            .collect();
        f.render_stateful_widget(
            List::new(sink_items).block(sink_block).highlight_style(
                Style::default()
                    .add_modifier(Modifier::BOLD)
                    .fg(Color::Yellow),
            ),
            filter_chunks[1],
            &mut app.sink_list_state,
        );
    }

    // Table
    render_table(f, app, content_chunks[1]);

    // Search
    let search_block = Block::default()
        .borders(Borders::ALL)
        .title(" Search (/) ")
        .border_type(BorderType::Rounded)
        .border_style(Style::default().fg(if app.focus == AppFocus::Search {
            Color::Yellow
        } else {
            Color::DarkGray
        }));
    let search_text = if app.search_query.is_empty() && app.focus != AppFocus::Search {
        Span::styled("Type / to search...", Style::default().fg(Color::DarkGray))
    } else {
        Span::raw(&app.search_query)
    };
    f.render_widget(
        Paragraph::new(search_text).block(search_block),
        main_chunks[1],
    );

    // Keybinds
    let keybinds = if !app.detail_view_open {
        let s_style = if !app.scroll_locked {
            Style::default().fg(Color::Black).bg(Color::Yellow)
        } else {
            Style::default().fg(Color::Gray).bg(Color::DarkGray)
        };
        let w_style = if app.wrap_logs {
            Style::default().fg(Color::Black).bg(Color::Magenta)
        } else {
            Style::default().fg(Color::Gray).bg(Color::DarkGray)
        };
        Line::from(vec![
            Span::styled(" q ", Style::default().fg(Color::Gray).bg(Color::DarkGray)),
            Span::raw(" Quit |"),
            Span::styled(" v ", Style::default().fg(Color::Gray).bg(Color::DarkGray)),
            Span::raw(" View |"),
            Span::styled(" f ", Style::default().fg(Color::Gray).bg(Color::DarkGray)),
            Span::raw(" Filters |"),
            Span::styled(" s ", s_style),
            Span::raw(" Auto-scroll |"),
            Span::styled(" w ", w_style),
            Span::raw(" Wrap |"),
            Span::styled(" / ", Style::default().fg(Color::Gray).bg(Color::DarkGray)),
            Span::raw(" Search |"),
            Span::styled(" hjkl ", Style::default().fg(Color::Gray).bg(Color::DarkGray)),
            Span::raw(" Move |"),
            Span::styled(" Shift+HJKL ", Style::default().fg(Color::Gray).bg(Color::DarkGray)),
            Span::raw(" Switch Focus "),
        ])
    } else {
        Line::from(vec![
            Span::styled("DETAIL VIEW ACTIVE", Style::default().fg(Color::Black).bg(Color::Yellow).add_modifier(Modifier::BOLD)), Span::raw(" - "),
            Span::styled(" ↑↓/jk ", Style::default().fg(Color::Gray).bg(Color::DarkGray)), Span::raw(" Scroll |"),
            Span::styled(" ESC/q ", Style::default().fg(Color::Gray).bg(Color::DarkGray)), Span::raw(" Close "),
        ])
    };
    f.render_widget(
        Paragraph::new(keybinds).alignment(Alignment::Center),
        main_chunks[2],
    );

    // Reconnecting Status Bar
    if is_reconnecting {
        let spinner = ["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"];
        let spinner_char = spinner[app.spinner_frame % spinner.len()];
        let status_text = format!(
            "{} reconnecting... Press \"q\" to go to to main menu",
            spinner_char
        );
        f.render_widget(
            Paragraph::new(status_text)
                .alignment(Alignment::Center)
                .style(
                    Style::default()
                        .bg(Color::Yellow)
                        .fg(Color::Black)
                        .add_modifier(Modifier::BOLD),
                ),
            main_chunks[3],
        );
    }
}

fn render_table(f: &mut Frame, app: &mut App, area: Rect) {
    let mut title = " Kenzo Logs ".to_string();
    if app.scroll_locked {
        title = " Kenzo Logs 🔒 ".to_string();
    }

    let table_block = Block::default()
        .borders(Borders::ALL)
        .title(title)
        .border_type(BorderType::Rounded)
        .border_style(Style::default().fg(if app.focus == AppFocus::Table {
            Color::Yellow
        } else {
            Color::DarkGray
        }));

    let inner_area = table_block.inner(area);
    let show_sink = area.width > 100;
    let show_file = area.width > 130;

    let mut widths = vec![
        Constraint::Length(15), // Time
        Constraint::Length(12), // Severity
    ];
    if show_sink {
        widths.push(Constraint::Length(15));
    }
    if show_file {
        widths.push(Constraint::Length(25));
    }
    widths.push(Constraint::Min(20)); // Message

    // Calculate message column width for wrapping (table inner area, minus fixed columns and spacing)
    let column_spacing = 1u16;
    let mut occupied_width = 15u16 + 12u16; // Time + Severity
    let mut num_cols = 2u16;
    if show_sink {
        occupied_width += 15;
        num_cols += 1;
    }
    if show_file {
        occupied_width += 25;
        num_cols += 1;
    }
    let spacing_width = column_spacing.saturating_mul(num_cols.saturating_sub(1));
    let msg_col_width = inner_area
        .width
        .saturating_sub(occupied_width + spacing_width)
        .max(1) as usize;

    let rows: Vec<Row> = app
        .filtered_logs
        .iter()
        .map(|&log_idx| {
            let log = &app.logs[log_idx];
            let severity = Severity::from(log.severity);
            let (fg, bg) = match severity {
                Severity::Trace => (Color::Gray, Color::Reset),
                Severity::Info => (Color::Green, Color::Reset),
                Severity::Warning => (Color::Yellow, Color::Reset),
                Severity::Error => (Color::Red, Color::Reset),
                Severity::Critical => (Color::Black, Color::Red),
            };
            let style = Style::default().fg(fg).bg(bg);
            let highlight_q = app.search_query.to_lowercase();

            let sev_label = match log.severity {
                0 => "TRACE",
                1 => "INFO",
                2 => "WARNING",
                3 => "ERROR",
                4 => "CRITICAL",
                _ => "UNKNOWN",
            };
            let sev_str = format!("[{}]", sev_label);
            let time_str = format!("{}  ", format_timestamp(log.timestamp));
            let sink_str = format!("[{}]", log.sink);
            let file_str = format!("{}:{}", log.filepath, log.line_number);
            let msg_str = log.message.replace('\t', "    ");

            let mut wrapped_lines = Vec::new();
            if app.wrap_logs {
                for line in msg_str.lines() {
                    let wrapped = textwrap::fill(line, msg_col_width);
                    for subline in wrapped.lines() {
                        wrapped_lines.push(subline.to_string());
                    }
                }
            } else {
                let single_line = msg_str.replace('\r', "").replace('\n', " ");
                wrapped_lines.push(truncate_with_ellipsis(&single_line, msg_col_width));
            }

            if wrapped_lines.is_empty() {
                wrapped_lines.push(String::new());
            }

            let row_height = if app.wrap_logs {
                wrapped_lines.len().max(1) as u16
            } else {
                1
            };

            let msg_text = Text::from(
                wrapped_lines
                    .iter()
                    .map(|l| {
                        Line::from(highlight_text(
                            l,
                            &highlight_q,
                            if severity == Severity::Critical {
                                style
                            } else if severity == Severity::Trace {
                                style
                            } else {
                                Style::default().fg(Color::White)
                            },
                        ))
                    })
                    .collect::<Vec<_>>(),
            );

            let mut cells = vec![
                Cell::from(Line::from(highlight_text(&time_str, &highlight_q, style))),
                Cell::from(Line::from(highlight_text(&sev_str, &highlight_q, style))),
            ];
            if show_sink {
                cells.push(Cell::from(Line::from(highlight_text(
                    &sink_str,
                    &highlight_q,
                    style,
                ))));
            }
            if show_file {
                cells.push(Cell::from(Line::from(highlight_text(
                    &file_str,
                    &highlight_q,
                    style,
                ))));
            }
            cells.push(Cell::from(msg_text));

            Row::new(cells).style(style).height(row_height)
        })
        .collect();

    let mut header_cells = vec!["Time", "Severity"];
    if show_sink {
        header_cells.push("Sink");
    }
    if show_file {
        header_cells.push("File");
    }
    header_cells.push("Message");

    let table = Table::new(rows, widths)
        .header(
            Row::new(header_cells).style(
                Style::default()
                    .fg(Color::Cyan)
                    .add_modifier(Modifier::BOLD),
            ),
        )
        .block(table_block)
        .column_spacing(1)
        .row_highlight_style(Style::default().add_modifier(Modifier::REVERSED));

    f.render_stateful_widget(table, area, &mut app.table_state);
}

fn truncate_with_ellipsis(text: &str, max_width: usize) -> String {
    if max_width == 0 {
        return String::new();
    }
    let text_len = text.chars().count();
    if text_len <= max_width {
        return text.to_string();
    }
    if max_width <= 3 {
        return ".".repeat(max_width);
    }
    let mut truncated = String::new();
    for (i, ch) in text.chars().enumerate() {
        if i >= max_width - 3 {
            break;
        }
        truncated.push(ch);
    }
    truncated.push_str("...");
    truncated
}

fn highlight_text(text: &str, query: &str, base_style: Style) -> Vec<Span<'static>> {
    if query.is_empty() || !text.to_lowercase().contains(query) {
        return vec![Span::styled(text.to_string(), base_style)];
    }
    let mut spans = Vec::new();
    let mut last_end = 0;
    let text_lower = text.to_lowercase();
    for (start, _) in text_lower.match_indices(query) {
        if start > last_end {
            spans.push(Span::styled(text[last_end..start].to_string(), base_style));
        }
        spans.push(Span::styled(
            text[start..start + query.len()].to_string(),
            base_style.bg(Color::Yellow).fg(Color::Black),
        ));
        last_end = start + query.len();
    }
    if last_end < text.len() {
        spans.push(Span::styled(text[last_end..].to_string(), base_style));
    }
    spans
}

fn format_timestamp(ts: u64) -> String {
    use chrono::{DateTime, Local, TimeZone, Utc};
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

fn render_detail_view(f: &mut Frame, app: &mut App, area: Rect) {
    if let Some(log_idx) = app.detail_view_log_idx {
        let log = &app.logs[log_idx];

        // Calculate dialog dimensions
        let dialog_width = (area.width as usize).saturating_sub(4).max(80) as u16;
        let dialog_height = (area.height as usize).saturating_sub(4).max(20) as u16;

        let dialog_area = Rect::new(
            area.x + (area.width.saturating_sub(dialog_width)) / 2,
            area.y + (area.height.saturating_sub(dialog_height)) / 2,
            dialog_width,
            dialog_height,
        );

        let severity = Severity::from(log.severity);
        let (fg, _bg) = match severity {
            Severity::Trace => (Color::Gray, Color::Reset),
            Severity::Info => (Color::Green, Color::Reset),
            Severity::Warning => (Color::Yellow, Color::Reset),
            Severity::Error => (Color::Red, Color::Reset),
            Severity::Critical => (Color::Black, Color::Red),
        };

        let sev_label = match log.severity { 0 => "TRACE", 1 => "INFO", 2 => "WARNING", 3 => "ERROR", 4 => "CRITICAL", _ => "UNKNOWN" };
        let time_str = format_timestamp(log.timestamp);

        // Create header
        let header = format!("[{}] {} - {} ({}:{})", sev_label, time_str, log.sink, log.filepath, log.line_number);

        // Split message into lines and wrap long lines
        let msg_lines: Vec<&str> = log.message.lines().collect();
        let content_width = (dialog_width as usize).saturating_sub(4);

        let mut wrapped_lines = Vec::new();
        for line in msg_lines.iter() {
            if line.is_empty() {
                wrapped_lines.push(String::new());
            } else if line.len() > content_width {
                // Wrap long lines
                let wrapped = textwrap::fill(line, content_width);
                for subline in wrapped.lines() {
                    wrapped_lines.push(subline.to_string());
                }
            } else {
                wrapped_lines.push(line.to_string());
            }
        }

        let available_lines = (dialog_height as usize).saturating_sub(6); // Account for header, blank line, controls

        // Clamp scroll position to valid range
        let max_scroll = wrapped_lines.len().saturating_sub(available_lines);
        if app.detail_view_scroll > max_scroll {
            app.detail_view_scroll = max_scroll;
        }

        let start_line = app.detail_view_scroll;
        let end_line = (start_line + available_lines).min(wrapped_lines.len());

        let mut content_lines = vec![
            Line::from(Span::styled(header, Style::default().add_modifier(Modifier::BOLD).fg(Color::Cyan))),
            Line::from(""),
        ];

        // Add message lines with proper formatting
        for line in &wrapped_lines[start_line..end_line] {
            content_lines.push(Line::from(Span::styled(line.clone(), Style::default().fg(fg))));
        }

        let content = Text::from(content_lines);

        let scroll_info = if wrapped_lines.len() > available_lines {
            format!("Line {}/{}", start_line + 1, wrapped_lines.len())
        } else {
            String::new()
        };

        // Use bright yellow border to indicate focus
        let block = Block::default()
            .borders(Borders::ALL)
            .title(" Message Details (FOCUSED) ")
            .border_type(BorderType::Rounded)
            .border_style(Style::default().fg(Color::Yellow).add_modifier(Modifier::BOLD))
            .title_alignment(Alignment::Center);

        let paragraph = Paragraph::new(content)
            .block(block)
            .style(Style::default());

        f.render_widget(Clear, dialog_area);
        f.render_widget(paragraph, dialog_area);

        // Draw footer with controls and scroll info
        let footer_text = if scroll_info.is_empty() {
            "Press ↑↓/jk to scroll • ESC/q to close".to_string()
        } else {
            format!("Press ↑↓/jk to scroll • {} • ESC/q to close", scroll_info)
        };

        let footer_area = Rect::new(
            dialog_area.x + 1,
            dialog_area.y + dialog_area.height.saturating_sub(1),
            dialog_area.width.saturating_sub(2),
            1,
        );
        f.render_widget(Paragraph::new(footer_text).style(Style::default().fg(Color::Yellow).add_modifier(Modifier::BOLD)), footer_area);
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
