// faces_list.h — registro dos mostradores (a única lista a editar para criar um novo).
//
// Cada linha: X(ID, função, "Nome no painel", "Descrição curta").
// A ordem é o número salvo nas configurações: acrescente sempre no FIM, nunca no meio.
// A partir desta lista saem: o enum THEME_*, a tabela de desenho em ui.cpp, a galeria do
// painel (/api/faces), os testes e as prévias do simulador. Guia: docs/CRIAR_MOSTRADOR.md
#pragma once

#define UI_FACES(X)                                                                         \
  X(MATRIX, themeMatrix, "Matriz", "Pixel art com cartões de clima e focos do dia")         \
  X(TOMATO, themeTomato, "Tomate", "Cena em pixel art que muda com a hora e o clima")      \
  X(ANALOG, themeAnalog, "Analógico", "Ponteiros bastão e marcas grossas")                 \
  X(WEATHER, themeWeather, "Clima", "Temperatura grande e mini cartões")                   \
  X(NEON, themeNeon, "Neon", "Tubos rosa e ciano com brilho")                              \
  X(FLIP, themeFlip, "Flip", "Plaquinhas que viram, com a fonte Bebas Neue")               \
  X(LCD, themeLcd, "LCD retrô", "Relógio digital dos anos 80 (fonte DSEG7)")               \
  X(WORDS, themeWords, "Palavras", "A hora por extenso, no idioma do relógio")         \
  X(FORECAST, themeForecast, "Previsão", "Hoje e os próximos 4 dias, com chuva e mín/máx")
