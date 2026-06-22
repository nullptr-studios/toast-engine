use crate::proto::{log_data::Severity, LogBatch, LogData};
use crate::storage::LogStorage;
use tokio::net::{TcpListener, TcpStream};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use prost::Message;
use std::sync::Arc;
use tokio::sync::{Mutex, broadcast};
use std::time::{SystemTime, UNIX_EPOCH};

pub struct Server {
    storage: LogStorage,
    engine_connected: Arc<Mutex<bool>>,
}

impl Server {
    pub fn new(storage: LogStorage) -> Self {
        Self {
            storage,
            engine_connected: Arc::new(Mutex::new(false)),
        }
    }

    pub async fn run_tui_listener(&self, port: u16) -> Result<(), Box<dyn std::error::Error>> {
        let addr = format!("0.0.0.0:{}", port);
        let listener = TcpListener::bind(&addr).await?;
        println!("TUI Listener running on {}", addr);

        loop {
            let (mut socket, peer_addr) = listener.accept().await?;
            println!("TUI connected: {}", peer_addr);
            let storage = self.storage.clone();

            tokio::spawn(async move {
                if let Err(e) = handle_tui_connection(&mut socket, storage).await {
                    eprintln!("TUI connection error: {:?}", e);
                }
                println!("TUI disconnected: {}", peer_addr);
            });
        }
    }

    pub async fn run_engine_listener(&self, port: u16, infinite: bool) -> Result<(), Box<dyn std::error::Error>> {
        let addr = format!("0.0.0.0:{}", port);
        let listener = TcpListener::bind(&addr).await?;
        println!("Engine Listener running on {}", addr);

        loop {
            let (mut socket, peer_addr) = listener.accept().await?;
            println!("Engine connected: {}", peer_addr);

            {
                let mut connected = self.engine_connected.lock().await;
                if *connected {
                    // Reject every engine connection if we're already connected to one instance
                    println!("Engine already connected, rejecting {}", peer_addr);
                    continue;
                }
                *connected = true;
            }

            let storage = self.storage.clone();
            if let Err(e) = handle_engine_connection(&mut socket, storage).await {
                eprintln!("Engine connection error: {:?}", e);
            }

            println!("Engine disconnected: {}", peer_addr);

            {
                let mut connected = self.engine_connected.lock().await;
                *connected = false;
            }

            // Save CSV
            if let Err(e) = self.storage.save_csv().await {
                eprintln!("Failed to save CSV: {:?}", e);
            } else {
                println!("Logs saved to CSV.");
            }

            if !infinite {
                println!("Shutting down server (no --infinite).");
                std::process::exit(0);
            }
        }
    }
}

fn build_disconnect_log() -> LogData {
    let timestamp = match SystemTime::now().duration_since(UNIX_EPOCH) {
        Ok(duration) => duration.as_millis() as u64,
        Err(_) => 0,
    };

    LogData {
        timestamp,
        severity: Severity::Critical as i32,
        filepath: String::new(),
        line_number: 0,
        sink: "Log Server".to_string(),
        message: "Toast Engine disconnected".to_string(),
    }
}

async fn handle_engine_connection(socket: &mut TcpStream, storage: LogStorage) -> Result<(), Box<dyn std::error::Error>> {
    loop {
        let len = match socket.read_u32().await {
            Ok(l) => l,
            Err(e) if e.kind() == std::io::ErrorKind::UnexpectedEof => {
                send_disconnect_notice(&storage).await;
                return Ok(());
            }
            Err(e) => {
                send_disconnect_notice(&storage).await;
                return Err(e.into());
            }
        };

        let mut buf = vec![0u8; len as usize];
        if let Err(e) = socket.read_exact(&mut buf).await {
            if e.kind() == std::io::ErrorKind::UnexpectedEof {
                send_disconnect_notice(&storage).await;
                return Ok(());
            }
            send_disconnect_notice(&storage).await;
            return Err(e.into());
        }

        let batch = LogBatch::decode(&buf[..])?;
        storage.add_logs(batch).await;
    }
}

async fn send_disconnect_notice(storage: &LogStorage) {
    let disconnect_log = build_disconnect_log();
    storage.add_logs(LogBatch { logs: vec![disconnect_log] }).await;
    tokio::task::yield_now().await;
}

async fn handle_tui_connection(socket: &mut TcpStream, storage: LogStorage) -> Result<(), Box<dyn std::error::Error>> {
    // Our approach for the TUI connection should be:
    //    - On connection, make a batch of all the previous messages logged by the engine
    //      so that the TUI isn't missing any information
    //    - Then, every time we receive a log, we will send a unique TCP package to the
    //      TUI. We may saturate the TCP connection of the TUI but having a log delayed a
    //      couple of milliseconds is not that big of a deal if the timestamp is correct
    // OPTIMIZE: How is this handled with CPUs that may not be able to allocate a thread
    // to the TUI process??

    // Send initial batch
    let existing_logs = storage.get_all().await;
    let batch = LogBatch { logs: existing_logs };
    let mut buf = Vec::new();
    batch.encode(&mut buf)?;
    send_framed(socket, &batch).await?;

    // Subscribe to new logs
    let mut rx = storage.subscribe();
    loop {
        match rx.recv().await {
            Ok(log) => {
                let update_batch = LogBatch { logs: vec![log] };
                send_framed(socket, &update_batch).await?;
            }
            Err(broadcast::error::RecvError::Lagged(_)) => {
                // FIXME: lagged subscribers silently drop messages (broadcast capacity 1000)
            }
            Err(broadcast::error::RecvError::Closed) => {
                break;
            }
        }
    }
    Ok(())
}

async fn send_framed(socket: &mut TcpStream, batch: &LogBatch) -> Result<(), Box<dyn std::error::Error>> {
    let mut buf = Vec::new();
    batch.encode(&mut buf)?;
    let len = buf.len() as u32;
    socket.write_u32(len).await?;
    socket.write_all(&buf).await?;
    Ok(())
}
