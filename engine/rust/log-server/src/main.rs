use tokio::net::TcpListener;
use tokio::io::AsyncReadExt;
use prost::Message;

pub mod proto {
    include!(concat!(env!("OUT_DIR"), "/logging.rs"));
}

use proto::LogData;
use proto::LogBatch;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<String> = std::env::args().collect();

    let mut port = "12800";
    // if args.len() > 1 {
    //     port = args[1].as_str();
    // }
    let addr = format!("127.0.0.1:{}", port);
    let listener = TcpListener::bind(addr.clone()).await?;
    println!("Server listening on {}. Waiting for Game Engine...", addr);

    // Accept only one engine connection
    let (mut socket, peer_addr) = listener.accept().await?;
    println!("Engine connected: {}", peer_addr);

    let mut buf = vec![0u8; 1024];

    loop {
        let n = match socket.read(&mut buf).await {
            Ok(0) => {
                println!("Engine disconnected.");
                break;
            }
            Ok(n) => n,
            Err(e) => {
                eprintln!("Read error: {:?}", e);
                break;
            }
        };

        // Try to decode the protobuf
        match LogBatch::decode(&buf[..n]) {
            Ok(batch) => {
                for log in batch.logs {
                    println!("--- Log Received ---");
                    println!("Time: {}", log.timestamp);
                    println!("File: {}:{}", log.filepath, log.line_number);
                    println!("Msg:  {}", log.message);
                    println!("Sev:  {:?}", log.severity);
                }
            }
            Err(e) => {
                eprintln!("Failed to parse Protobuf: {:?}", e);
                // If it fails, print the raw bytes to debug what the engine sent
                println!("Raw bytes: {:?}", &buf[..n]);
            }
        }
    }

    println!("Server closed.");
    Ok(())
}
