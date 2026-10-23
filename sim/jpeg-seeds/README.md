Amostras JPEG geradas com ffmpeg (`testsrc2` e `mandelbrot`, imagens sintéticas do próprio ffmpeg),
com fatores de amostragem diferentes (Y 2x2 e Y 2x2 com croma 1x2; o 4:4:4 do ffmpeg, Y 1x2, a JPEGDEC original também não decodifica). Servem de
sementes para `sim/test-jpeg.sh`: o modo `valid` exige que todas decodifiquem, e o fuzzing parte delas.
