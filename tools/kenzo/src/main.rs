use clap::Parser;
use anyhow::Result;

pub mod proto;

mod app;
mod ui;
mod tui;

use crate::app::App;

#[derive(Parser, Debug)]
struct Args {
    /// Path to a CSV log file to open directly
    #[arg(long)]
    csv: Option<String>,
}

#[tokio::main]
async fn main() -> Result<()> {
    let args = Args::parse();
    
    // Set up panic handler to restore terminal
    let default_panic = std::panic::take_hook();
    std::panic::set_hook(Box::new(move |panic_info| {
        let _ = tui::restore();
        default_panic(panic_info);
    }));
    
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
