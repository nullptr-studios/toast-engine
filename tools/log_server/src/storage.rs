use crate::proto::{LogBatch, LogData};
use std::sync::Arc;
use tokio::sync::{broadcast, RwLock};
use std::path::Path;
use std::fs;
use chrono::Local;

#[derive(Clone)]
pub struct LogStorage {
    logs: Arc<RwLock<Vec<LogData>>>,
    tx: broadcast::Sender<LogData>,
}

impl LogStorage {
    pub fn new() -> Self {
        let (tx, _) = broadcast::channel(1000);
        Self {
            logs: Arc::new(RwLock::new(Vec::new())),
            tx,
        }
    }

    pub async fn add_logs(&self, batch: LogBatch) {
        let mut logs = self.logs.write().await;
        for log in batch.logs {
            // Broadcast first, then store
            let _ = self.tx.send(log.clone());
            logs.push(log);
        }
    }

    pub async fn get_all(&self) -> Vec<LogData> {
        self.logs.read().await.clone()
    }

    pub fn subscribe(&self) -> broadcast::Receiver<LogData> {
        // broadcast capacity is 1000; lagged subscribers silently drop messages
        self.tx.subscribe()
    }

    pub async fn save_csv(&self) -> Result<String, Box<dyn std::error::Error>> {
        let logs = self.logs.read().await;
        let now = Local::now();
        // path is relative to the working directory; the logs/ dir is created if it doesn't exist
        let filename = format!("logs/{}.csv", now.format("%Y-%m-%d_%H-%M-%S"));
        let path = Path::new(&filename);

        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent)?;
        }

        let mut wtr = csv::Writer::from_path(path)?;
        
        // Write header
        wtr.write_record(&["Timestamp", "Severity", "Filepath", "Line", "Sink", "Message"])?;

        for log in logs.iter() {
            let severity_str = match log.severity {
                0 => "TRACE",
                1 => "INFO",
                2 => "WARNING",
                3 => "ERROR",
                4 => "CRITICAL",
                _ => "UNKNOWN",
            };

            wtr.write_record(&[
                log.timestamp.to_string(),
                severity_str.to_string(),
                log.filepath.clone(),
                log.line_number.to_string(),
                log.sink.clone(),
                log.message.clone(),
            ])?;
        }

        wtr.flush()?;
        Ok(filename)
    }
}
