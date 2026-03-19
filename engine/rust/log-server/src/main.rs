use tokio::net::TcpListener;
use tokio::io::AsyncReadExt;
use prost::Message;
use std::env;

pub mod proto {
    include!(concat!(env!("OUT_DIR"), "/logging.rs"));
}

use proto::LogBatch;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<String> = env::args().collect();
    let single_shot = !args.iter().any(|a| a == "--infinite");

    // allow overriding port via --port <port>
    let mut port = "12800".to_string();
    if let Some(pos) = args.iter().position(|a| a == "--port") {
        if args.len() > pos + 1 {
            port = args[pos+1].clone();
        }
    }

    let addr = format!("127.0.0.1:{}", port);
    let listener = TcpListener::bind(addr.clone()).await?;
    println!("Server listening on {}. Waiting for Game Engine...", addr);

    loop {
        let (mut socket, peer_addr) = listener.accept().await?;
        println!("Engine connected: {}", peer_addr);

        if single_shot {
            // Handle single connection inline and then exit
            let mut buf = vec![0u8; 65536];
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
                        println!("Raw bytes: {:?}", &buf[..n]);
                    }
                }
            }
            println!("Server closed.");
            break;
        } else {
            // Spawn a task to handle the client and keep listening for further connections.
            tokio::spawn(async move {
                let mut buf = vec![0u8; 65536];
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
                            println!("Raw bytes: {:?}", &buf[..n]);
                        }
                    }
                }
                println!("Client handler exiting.");
            });
        }
    }

    Ok(())
}
