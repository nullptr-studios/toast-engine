/*
 * @file main.rs
 * @author Xein
 * @date 26 Mar 2026
 */

use clap::Parser;
use std::sync::Arc;

pub mod proto;

mod server;
mod storage;

use storage::LogStorage;
use server::Server;

#[derive(Parser, Debug)]
struct Args {
    /// Port for the engine to connect to
    #[arg(long, default_value_t = 12800)]
    port: u16,

    /// Keep server running even after engine disconnects
    #[arg(long, default_value_t = false)]
    infinite: bool,
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args = Args::parse();
    
    let storage = LogStorage::new();
    let server = Arc::new(Server::new(storage));

    // Spawn TUI listener
    let server_tui = server.clone();
    tokio::spawn(async move {
        if let Err(e) = server_tui.run_tui_listener(12801).await {
            eprintln!("TUI Listener failed: {:?}", e);
        }
    });

    // Run Engine listener
    server.run_engine_listener(args.port, args.infinite).await?;

    Ok(())
}
