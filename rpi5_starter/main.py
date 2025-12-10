#!/usr/bin/env python3
"""
BabyMilu Raspberry Pi 5 - Main Entry Point
Port from ESP32 firmware to Python
"""

import asyncio
import logging
from application import Application

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)


async def main():
    """Main entry point - similar to app_main() in ESP32 firmware"""
    logger.info("=== BabyMilu RPi5 Starting ===")
    
    # Get application instance (singleton pattern like ESP32)
    app = Application.get_instance()
    
    # Start the application
    await app.start()
    
    logger.info("=== BabyMilu RPi5 Started ===")
    
    # Main event loop (runs forever)
    try:
        await app.run()
    except KeyboardInterrupt:
        logger.info("Shutting down...")
        await app.shutdown()


if __name__ == "__main__":
    asyncio.run(main())


