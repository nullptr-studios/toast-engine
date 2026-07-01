use crate::proto::{LogBatch, LogData};
use crate::tui::Tui;
use anyhow::Result;
use crossterm::event::{self, Event, KeyCode, KeyEvent, KeyEventKind, KeyModifiers};
use prost::Message;
use std::time::Duration;
use tokio::io::AsyncReadExt;
use tokio::net::TcpStream;
use tokio::sync::mpsc;
use tui_input::backend::crossterm::EventHandler;
use tui_input::Input;

#[derive(Debug, PartialEq, Clone)]
pub enum Severity {
    Trace = 0,
    Info = 1,
    Warning = 2,
    Error = 3,
    Critical = 4,
}

impl From<i32> for Severity {
    fn from(i: i32) -> Self {
        match i {
            0 => Severity::Trace,
            1 => Severity::Info,
            2 => Severity::Warning,
            3 => Severity::Error,
            4 => Severity::Critical,
            _ => Severity::Info,
        }
    }
}

#[derive(PartialEq, Debug, Clone)]
pub enum ConnectionState {
    Disconnected,
    Connecting(String),   // Address
    Reconnecting(String), // Address
    Connected,
    #[allow(dead_code)]
    CsvMode(String), // Path
}

#[derive(PartialEq, Debug, Clone, Copy)]
pub enum AppFocus {
    Table,
    Search,
    FilterSeverity,
    FilterSink,
}

#[derive(PartialEq, Debug, Clone, Copy)]
pub enum PopupOption {
    Localhost,
    RemoteIp,
    CsvFile,
}

pub struct App {
    pub logs: Vec<LogData>,
    pub filtered_logs: Vec<usize>, // Indices of logs that match filters
    pub display_rows: Vec<(usize, usize)>, // Maps display row to (log_idx, chunk_idx) for split messages
    pub state: ConnectionState,
    pub popup_selection: PopupOption,
    pub input_buffer: Input,
    pub spinner_frame: usize,

    // Filters & Search
    pub search_query: String,
    pub show_filters: bool,
    pub excluded_severities: std::collections::HashSet<i32>,
    pub excluded_sinks: std::collections::HashSet<String>,
    pub seen_severities: std::collections::HashSet<i32>,
    pub seen_sinks: std::collections::HashSet<String>,

    // Search navigation
    pub search_matches: Vec<usize>, // indices into filtered_logs, not logs; must be recalculated when filters change
    pub current_match_idx: Option<usize>,

    // Scrolling
    pub scroll_locked: bool, // true = user is scrolling manually; false = view auto-follows newest log
    pub wrap_logs: bool,

    // Navigation
    pub table_state: ratatui::widgets::TableState,
    pub severity_list_state: ratatui::widgets::ListState,
    pub sink_list_state: ratatui::widgets::ListState,
    pub focus: AppFocus,
    
    // Detail view
    pub detail_view_open: bool,
    pub detail_view_log_idx: Option<usize>,
    pub detail_view_scroll: usize,

    // Communication
    pub log_rx: Option<mpsc::Receiver<Vec<LogData>>>,
    pub stop_tx: Option<tokio::sync::oneshot::Sender<()>>,

    // Reconnection
    pub last_connect_addr: Option<String>,

    // Control
    pub should_exit: bool,
}

impl App {
    pub fn new(csv_path: Option<String>) -> Self {
        let mut app = Self {
            logs: Vec::new(),
            filtered_logs: Vec::new(),
            display_rows: Vec::new(),
            state: ConnectionState::Disconnected,
            popup_selection: PopupOption::Localhost,
            input_buffer: Input::default(),
            spinner_frame: 0,
            search_query: String::new(),
            show_filters: false,
            excluded_severities: std::collections::HashSet::new(),
            excluded_sinks: std::collections::HashSet::new(),
            seen_severities: std::collections::HashSet::new(),
            seen_sinks: std::collections::HashSet::new(),
            search_matches: Vec::new(),
            current_match_idx: None,
            scroll_locked: false,
            wrap_logs: true,
            table_state: ratatui::widgets::TableState::default(),
            severity_list_state: ratatui::widgets::ListState::default(),
            sink_list_state: ratatui::widgets::ListState::default(),
            focus: AppFocus::Table,
            detail_view_open: false,
            detail_view_log_idx: None,
            detail_view_scroll: 0,
            log_rx: None,
            stop_tx: None,
            last_connect_addr: None,
            should_exit: false,
        };

        if let Some(path) = csv_path {
            app.load_csv(&path);
        }

        app
    }

    pub async fn run(&mut self, terminal: &mut Tui) -> Result<()> {
        let tick_rate = Duration::from_millis(100);
        let mut last_tick = std::time::Instant::now();

        if let ConnectionState::Disconnected = self.state {
            self.connect("127.0.0.1:12801".to_string()).await;
        }

        loop {
            // Draw
            terminal.draw(|f| crate::ui::draw(f, self))?;

            // Check if we should exit
            if self.should_exit {
                break Ok(());
            }

            // Handle Network Events
            let mut batches = Vec::new();
            if let Some(rx) = &mut self.log_rx {
                while let Ok(batch) = rx.try_recv() {
                    batches.push(batch);
                }
            }

            if !batches.is_empty() {
                // Transition from Connecting to Connected on first batch (even if empty)
                if let ConnectionState::Connecting(_) | ConnectionState::Reconnecting(_) =
                    self.state
                {
                    self.state = ConnectionState::Connected;
                }

                for batch in batches {
                    for log in batch {
                        self.process_log(log);
                    }
                }
                self.update_filtered_logs();
            }

            // Handle Input
            let timeout = tick_rate
                .checked_sub(last_tick.elapsed())
                .unwrap_or_else(|| Duration::from_secs(0));

            if crossterm::event::poll(timeout)? {
                if let Event::Key(key) = event::read()? {
                    if key.kind == KeyEventKind::Press
                        || (key.kind == KeyEventKind::Repeat
                            && matches!(
                                key.code,
                                KeyCode::Char('h')
                                    | KeyCode::Char('j')
                                    | KeyCode::Char('k')
                                    | KeyCode::Char('l')
                                    | KeyCode::Up
                                    | KeyCode::Down
                                    | KeyCode::Left
                                    | KeyCode::Right
                            ))
                    {
                        self.handle_key(key).await?;
                    }
                }
            }

            if last_tick.elapsed() >= tick_rate {
                self.on_tick();
                last_tick = std::time::Instant::now();
            }

            if let Some(rx) = &mut self.log_rx {
                if rx.is_closed() {
                    // Auto-reconnect if we were connected or connecting
                    if let (
                        ConnectionState::Connected
                        | ConnectionState::Connecting(_)
                        | ConnectionState::Reconnecting(_),
                        Some(addr),
                    ) = (&self.state, &self.last_connect_addr)
                    {
                        self.reconnect(addr.clone()).await;
                    } else {
                        self.disconnect();
                    }
                }
            }
        }
    }

    fn on_tick(&mut self) {
        self.spinner_frame = (self.spinner_frame + 1) % 10;
    }

    async fn handle_key(&mut self, key: KeyEvent) -> Result<()> {
        if key.modifiers.contains(KeyModifiers::SHIFT) {
            match key.code {
                KeyCode::Char('H') | KeyCode::Left => self.focus = AppFocus::FilterSeverity,
                KeyCode::Char('L') | KeyCode::Right => self.focus = AppFocus::Table,
                KeyCode::Char('J') | KeyCode::Down => self.focus = AppFocus::FilterSink,
                KeyCode::Char('K') | KeyCode::Up => self.focus = AppFocus::FilterSeverity,
                _ => {}
            }
            if self.focus == AppFocus::FilterSeverity || self.focus == AppFocus::FilterSink {
                self.show_filters = true;
            }
        }

        match &self.state {
            ConnectionState::Disconnected | ConnectionState::Connecting(_) => {
                self.handle_disconnected_key(key).await
            }
            ConnectionState::Connected
            | ConnectionState::CsvMode(_)
            | ConnectionState::Reconnecting(_) => self.handle_connected_key(key).await,
        }
    }

    async fn handle_disconnected_key(&mut self, key: KeyEvent) -> Result<()> {
        match key.code {
            KeyCode::Char('q') => {
                self.should_exit = true;
            }
            KeyCode::Esc => {
                self.input_buffer = Input::default();
            }
            KeyCode::Up | KeyCode::Char('k') => {
                self.popup_selection = match self.popup_selection {
                    PopupOption::Localhost => PopupOption::CsvFile,
                    PopupOption::RemoteIp => PopupOption::Localhost,
                    PopupOption::CsvFile => PopupOption::RemoteIp,
                };
            }
            KeyCode::Down | KeyCode::Char('j') => {
                self.popup_selection = match self.popup_selection {
                    PopupOption::Localhost => PopupOption::RemoteIp,
                    PopupOption::RemoteIp => PopupOption::CsvFile,
                    PopupOption::CsvFile => PopupOption::Localhost,
                };
            }
            KeyCode::Enter => match self.popup_selection {
                PopupOption::Localhost => self.connect("127.0.0.1:12801".to_string()).await,
                PopupOption::RemoteIp => {
                    let ip = self.input_buffer.value().to_string();
                    if !ip.is_empty() {
                        self.connect(format!("{}:12801", ip)).await;
                    }
                }
                PopupOption::CsvFile => {
                    let path = self.input_buffer.value().to_string();
                    if !path.is_empty() {
                        self.load_csv(&path);
                    }
                }
            },
            _ => match self.popup_selection {
                PopupOption::RemoteIp | PopupOption::CsvFile => {
                    self.input_buffer.handle_event(&Event::Key(key));
                }
                _ => {}
            },
        }
        Ok(())
    }

    async fn handle_connected_key(&mut self, key: KeyEvent) -> Result<()> {
        if self.detail_view_open {
            match key.code {
                KeyCode::Esc => self.close_detail_view(),
                KeyCode::Char('q') => self.close_detail_view(),
                KeyCode::Down | KeyCode::Char('j') => {
                    self.scroll_detail_view_down(10000); // Max scrollable (will be clamped in render)
                }
                KeyCode::Up | KeyCode::Char('k') => self.scroll_detail_view_up(),
                _ => {}
            }
            return Ok(());
        }

        if self.focus == AppFocus::Search {
            match key.code {
                KeyCode::Enter => self.focus = AppFocus::Table,
                KeyCode::Esc => {
                    self.search_query.clear();
                    self.focus = AppFocus::Table;
                    self.update_filtered_logs();
                }
                KeyCode::Backspace => {
                    self.search_query.pop();
                    self.update_filtered_logs();
                }
                KeyCode::Char(c) => {
                    self.search_query.push(c);
                    self.update_filtered_logs();
                }
                _ => {}
            }
            return Ok(());
        }

        match key.code {
            KeyCode::Char('q') => {
                self.disconnect();
            }
            KeyCode::Char('v') => self.open_detail_view(),
            KeyCode::Char('/') => self.focus = AppFocus::Search,
            KeyCode::Char('f') => {
                self.show_filters = !self.show_filters;
                if !self.show_filters {
                    self.focus = AppFocus::Table;
                }
            }
            KeyCode::Char('s') => {
                self.scroll_locked = !self.scroll_locked;
                if !self.scroll_locked {
                    self.jump_to_latest();
                }
            }
            KeyCode::Char('w') => {
                self.wrap_logs = !self.wrap_logs;
            }
            KeyCode::Char('n') => self.next_match(),
            KeyCode::Char('p') => self.prev_match(),
            KeyCode::Char('1') => self.toggle_severity(0),
            KeyCode::Char('2') => self.toggle_severity(1),
            KeyCode::Char('3') => self.toggle_severity(2),
            KeyCode::Char('4') => self.toggle_severity(3),
            KeyCode::Char('5') => self.toggle_severity(4),
            KeyCode::Char('j') | KeyCode::Down => self.move_down(),
            KeyCode::Char('k') | KeyCode::Up => self.move_up(),
            KeyCode::Enter | KeyCode::Char(' ') => self.toggle_selection(),
            KeyCode::Esc => {
                if !self.search_query.is_empty() {
                    self.search_query.clear();
                    self.update_filtered_logs();
                }
            }
            _ => {}
        }
        Ok(())
    }

    fn move_down(&mut self) {
        match self.focus {
            AppFocus::Table => {
                self.scroll_locked = true;
                let i = match self.table_state.selected() {
                    Some(i) => (i + 1).min(self.display_rows.len().saturating_sub(1)),
                    None => 0,
                };
                self.table_state.select(Some(i));
            }
            AppFocus::FilterSeverity => {
                let i = match self.severity_list_state.selected() {
                    Some(i) => (i + 1).min(self.seen_severities.len().saturating_sub(1)),
                    None => 0,
                };
                self.severity_list_state.select(Some(i));
            }
            AppFocus::FilterSink => {
                let i = match self.sink_list_state.selected() {
                    Some(i) => (i + 1).min(self.seen_sinks.len().saturating_sub(1)),
                    None => 0,
                };
                self.sink_list_state.select(Some(i));
            }
            _ => {}
        }
    }

    fn move_up(&mut self) {
        match self.focus {
            AppFocus::Table => {
                self.scroll_locked = true;
                let i = match self.table_state.selected() {
                    Some(i) => i.saturating_sub(1),
                    None => 0,
                };
                self.table_state.select(Some(i));
            }
            AppFocus::FilterSeverity => {
                let i = match self.severity_list_state.selected() {
                    Some(i) => i.saturating_sub(1),
                    None => 0,
                };
                self.severity_list_state.select(Some(i));
            }
            AppFocus::FilterSink => {
                let i = match self.sink_list_state.selected() {
                    Some(i) => i.saturating_sub(1),
                    None => 0,
                };
                self.sink_list_state.select(Some(i));
            }
            _ => {}
        }
    }

    fn toggle_severity(&mut self, sev: i32) {
        if self.excluded_severities.contains(&sev) {
            self.excluded_severities.remove(&sev);
        } else {
            self.excluded_severities.insert(sev);
        }
        self.update_filtered_logs();
    }

    fn toggle_selection(&mut self) {
        match self.focus {
            AppFocus::FilterSeverity => {
                if let Some(i) = self.severity_list_state.selected() {
                    let mut sevs: Vec<_> = self.seen_severities.iter().collect();
                    sevs.sort();
                    if let Some(&sev) = sevs.get(i) {
                        if self.excluded_severities.contains(sev) {
                            self.excluded_severities.remove(sev);
                        } else {
                            self.excluded_severities.insert(*sev);
                        }
                        self.update_filtered_logs();
                    }
                }
            }
            AppFocus::FilterSink => {
                if let Some(i) = self.sink_list_state.selected() {
                    let mut sinks: Vec<_> = self.seen_sinks.iter().collect();
                    sinks.sort();
                    if let Some(&sink) = sinks.get(i) {
                        if self.excluded_sinks.contains(sink) {
                            self.excluded_sinks.remove(sink);
                        } else {
                            self.excluded_sinks.insert(sink.clone());
                        }
                        self.update_filtered_logs();
                    }
                }
            }
            _ => {}
        }
    }

    fn next_match(&mut self) {
        if self.search_matches.is_empty() {
            return;
        }
        let next_match_idx = match self.current_match_idx {
            Some(i) => (i + 1) % self.search_matches.len(),
            None => 0,
        };
        self.current_match_idx = Some(next_match_idx);
        self.scroll_locked = true;
        self.table_state
            .select(Some(self.search_matches[next_match_idx]));
    }

    fn prev_match(&mut self) {
        if self.search_matches.is_empty() {
            return;
        }
        let prev_match_idx = match self.current_match_idx {
            Some(i) => {
                if i == 0 {
                    self.search_matches.len() - 1
                } else {
                    i - 1
                }
            }
            None => self.search_matches.len() - 1,
        };
        self.current_match_idx = Some(prev_match_idx);
        self.scroll_locked = true;
        self.table_state
            .select(Some(self.search_matches[prev_match_idx]));
    }

    async fn connect(&mut self, addr: String) {
        self.disconnect();
        self.state = ConnectionState::Connecting(addr.clone());
        self.last_connect_addr = Some(addr.clone());
        let (tx, rx) = mpsc::channel(100);
        let (stop_tx, mut stop_rx) = tokio::sync::oneshot::channel();

        self.log_rx = Some(rx);
        self.stop_tx = Some(stop_tx);

        let addr_clone = addr.clone();
        tokio::spawn(async move {
            match TcpStream::connect(&addr_clone).await {
                Ok(mut socket) => {
                    // empty batch signals "connected, no logs yet" so the app transitions out of Connecting
                    let _ = tx.send(Vec::new()).await;

                    loop {
                        tokio::select! {
                             _ = &mut stop_rx => break,
                             res = read_framed(&mut socket) => {
                                 match res {
                                     Ok(batch) => { if tx.send(batch.logs).await.is_err() { break; } }
                                     Err(_) => break,
                                 }
                             }
                        }
                    }
                }
                Err(_) => {}
            }
        });
    }

    async fn reconnect(&mut self, addr: String) {
        self.disconnect();
        self.state = ConnectionState::Reconnecting(addr.clone());
        self.last_connect_addr = Some(addr.clone());
        let (tx, rx) = mpsc::channel(100);
        let (stop_tx, mut stop_rx) = tokio::sync::oneshot::channel();

        self.log_rx = Some(rx);
        self.stop_tx = Some(stop_tx);

        let addr_clone = addr.clone();
        tokio::spawn(async move {
            match TcpStream::connect(&addr_clone).await {
                Ok(mut socket) => {
                    // empty batch signals "connected, no logs yet" so the app transitions out of Connecting
                    let _ = tx.send(Vec::new()).await;

                    loop {
                        tokio::select! {
                             _ = &mut stop_rx => break,
                             res = read_framed(&mut socket) => {
                                 match res {
                                     Ok(batch) => { if tx.send(batch.logs).await.is_err() { break; } }
                                     Err(_) => break,
                                 }
                             }
                        }
                    }
                }
                Err(_) => {}
            }
        });
    }

    fn disconnect(&mut self) {
        if let Some(tx) = self.stop_tx.take() {
            let _ = tx.send(());
        }
        self.log_rx = None;
        self.state = ConnectionState::Disconnected;
    }

    fn load_csv(&mut self, path: &str) {
        match csv::Reader::from_path(path) {
            Ok(mut rdr) => {
                self.logs.clear();
                for result in rdr.records() {
                    if let Ok(record) = result {
                        let log = LogData {
                            timestamp: record.get(0).unwrap_or("0").parse().unwrap_or(0),
                            severity: match record.get(1).unwrap_or("INFO") {
                                "TRACE" => 0,
                                "INFO" => 1,
                                "WARNING" => 2,
                                "ERROR" => 3,
                                "CRITICAL" => 4,
                                _ => 1,
                            },
                            filepath: record.get(2).unwrap_or("").to_string(),
                            line_number: record.get(3).unwrap_or("0").parse().unwrap_or(0),
                            sink: record.get(4).unwrap_or("").to_string(),
                            message: record.get(5).unwrap_or("").to_string(),
                        };
                        self.process_log(log);
                    }
                }
                self.state = ConnectionState::CsvMode(path.to_string());
                self.update_filtered_logs();
            }
            Err(_) => self.state = ConnectionState::Disconnected,
        }
    }

    fn process_log(&mut self, log: LogData) {
        if let ConnectionState::Connecting(_) | ConnectionState::Reconnecting(_) = self.state {
            self.state = ConnectionState::Connected;
        }
        self.seen_severities.insert(log.severity);
        self.seen_sinks.insert(log.sink.clone());
        self.logs.push(log);
    }

    pub fn update_filtered_logs(&mut self) {
        let q = self.search_query.to_lowercase();

        self.filtered_logs = self
            .logs
            .iter()
            .enumerate()
            .filter_map(|(i, log)| {
                if self.excluded_severities.contains(&log.severity) {
                    return None;
                }
                if self.excluded_sinks.contains(&log.sink) {
                    return None;
                }
                Some(i)
            })
            .collect();

        // Simple 1:1 mapping between display rows and filtered logs
        self.display_rows.clear();
        for &log_idx in &self.filtered_logs {
            self.display_rows.push((log_idx, 0));
        }

        self.search_matches.clear();
        if !q.is_empty() {
            for (idx, &log_idx) in self.filtered_logs.iter().enumerate() {
                let log = &self.logs[log_idx];
                let severity_label = match log.severity {
                    0 => "trace",
                    1 => "info",
                    2 => "warning",
                    3 => "error",
                    4 => "critical",
                    _ => "",
                };
                if log.message.to_lowercase().contains(&q)
                    || log.sink.to_lowercase().contains(&q)
                    || log.filepath.to_lowercase().contains(&q)
                    || severity_label.contains(&q)
                {
                    self.search_matches.push(idx);
                }
            }
        }

        if self.search_matches.is_empty() {
            self.current_match_idx = None;
        } else if self.current_match_idx.is_none() {
            self.current_match_idx = Some(0);
        }

        if !self.scroll_locked {
            self.jump_to_latest();
        }
    }

    fn jump_to_latest(&mut self) {
        if !self.display_rows.is_empty() {
            self.table_state.select(Some(self.display_rows.len() - 1));
        }
    }

    pub fn open_detail_view(&mut self) {
        if let Some(selected) = self.table_state.selected() {
            if selected < self.display_rows.len() {
                let (log_idx, _chunk_idx) = self.display_rows[selected];
                self.detail_view_open = true;
                self.detail_view_log_idx = Some(log_idx);
                self.detail_view_scroll = 0;
            }
        }
    }

    pub fn close_detail_view(&mut self) {
        self.detail_view_open = false;
        self.detail_view_log_idx = None;
        self.detail_view_scroll = 0;
    }

    pub fn scroll_detail_view_down(&mut self, max_lines: usize) {
        if self.detail_view_scroll < max_lines.saturating_sub(1) {
            self.detail_view_scroll = self.detail_view_scroll.saturating_add(3); // Scroll by 3 lines
            if self.detail_view_scroll >= max_lines {
                self.detail_view_scroll = max_lines.saturating_sub(1);
            }
        }
    }

    pub fn scroll_detail_view_up(&mut self) {
        if self.detail_view_scroll >= 3 {
            self.detail_view_scroll -= 3;
        } else if self.detail_view_scroll > 0 {
            self.detail_view_scroll = 0;
        }
    }
}

// wire format: 4-byte big-endian length prefix then protobuf bytes; matches log_server framing
async fn read_framed(socket: &mut TcpStream) -> Result<LogBatch> {
    let len = socket.read_u32().await? as usize;
    let mut buf = vec![0u8; len];
    socket.read_exact(&mut buf).await?;
    Ok(LogBatch::decode(&buf[..])?)
}
