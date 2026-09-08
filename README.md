#### *PLEASE NOTE*
This branch is only for testing on a nucleo STM32 dev board. The firmware and pinout is slightly different than the actual BMS master board. Pins PA2 and PA3 are LPUART1_TX/LPURT1_RX to allow the connection through STM32 viritual COM port on the nucleo board. This makes serial debugging easier.

#### Syntax Guide 
STM32CubeMX will override code not included in user code comment brackets when changes are made to the hardware/firmware configuration using STM32CubeMX. 
*Please*, only include code in vendor generated files as shown: 
```
// User Code Begin --- // 

/* only place your code here */

// User Code End --- // 
```