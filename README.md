# Small Battery-powered Image Logger

![Vision System](images/drone-cam.jpg)

## Programming the FPGA

The pre-built FPGA image can be found in the `fpga` folder.  To program the FPGA using the developer kit, set the DIP switches as follows:

| Functionality | SW1-6	| SW1-7	| SW1-8	| Description |
| :---:         | ---:  |  ---: | ---:  |--- |
| Flash         | On 	| On   	| Off   | FTDI connected to FPGA flash |

Create a new project in the Radiant programmer and select the iCE40UP5K device from the iCE40 UltraPlus family.

Use the following SPI Flash Options:

* Family: SPI Serial Flash
* Vendor: WinBond
* Device: W25x40CL
* Package: 8-pad USON

## Programming the microcontroller

Open the `sketch/land_drone.ino` sketch in the Arduion IDE and upload
to the Feather board.

The Adafruit website has detail instructions for using the Arduino IDE
with the Feather M4 Express, see: [Arduino IDE
Setup](https://learn.adafruit.com/adafruit-feather-m4-express-atsamd51/setup)
