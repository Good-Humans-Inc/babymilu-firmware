Place the custom FCC PHY init binary here before running `idf.py build`:

- Filename: `custom_fcc_phy_init_data.bin`
- Full path: `D:\test\haha\phy_init\custom_fcc_phy_init_data.bin`

This project is configured to:

- load PHY init data from the `phy_init` partition at boot
- copy this file to the build output as `build/phy_init_data.bin` during the build

If the file is present before CMake configures the build, `idf.py flash` will use the custom PHY init data for the `phy_init` partition.
