use clap::Parser;
use anyhow::Result;

pub mod proto {
    include!(concat!(env!("OUT_DIR"), "/logging.rs"));
}

mod app;
mod ui;
mod tui;

use crate::app::App;

#[derive(Parser, Debug)]
#[command(author, version, about, long_about = None)]
struct Args {
    /// Path to a CSV log file to open directly
    #[arg(long)]
    csv: Option<String>,
}

#[tokio::main]
async fn main() -> Result<()> {
    let args = Args::parse();
    
    // Initialize Terminal
    let mut terminal = tui::init()?;
    
    // Create App
    let mut app = App::new(args.csv);
    
    // Run Main Loop
    let res = app.run(&mut terminal).await;

    // Restore Terminal
    tui::restore()?;

    if let Err(err) = res {
        println!("{err:?}");
    }

    Ok(())
}
