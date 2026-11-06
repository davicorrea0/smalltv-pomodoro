// Incluído em todo .cpp pelo compilar.sh (-include), junto com -DNO_GLOBAL_TWOWIRE.
// O relógio não usa I2C, mas a Adafruit BusIO (dependência da Adafruit GFX) inclui o Wire.h e usa &Wire como valor
// padrão em Adafruit_I2CDevice.h. Com -DNO_GLOBAL_TWOWIRE o core não cria o objeto global Wire (−296 B de RAM fixa,
// −608 B de flash); esta declaração deixa a BusIO compilar. Como ninguém constrói um Adafruit_I2CDevice, nada
// referencia o Wire e o link passa. Se algum dia algo usar I2C, o link falha com "undefined reference to Wire":
// tire as duas flags do compilar.sh.
#pragma once
#ifdef __cplusplus
class TwoWire;
extern TwoWire Wire;
#endif
