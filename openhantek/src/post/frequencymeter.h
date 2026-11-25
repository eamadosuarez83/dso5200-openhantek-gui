// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <vector>

/// Mide la frecuencia en el dominio del tiempo: busca los cruces del nivel medio ((min+max)/2) con
/// histéresis (10 % de la amplitud pico a pico), ubica cada cruce entre dos muestras por interpolación
/// lineal y promedia los periodos completos (subida→subida y bajada→bajada). Con menos de dos ciclos en
/// el registro sigue siendo precisa, a diferencia del pico de la FFT, cuya resolución es
/// muestreo/muestras (p. ej. 543 Hz con 10240 muestras a 5,56 MS/s).
///
/// Devuelve 0 si no hay al menos un periodo completo o si los periodos medidos no son regulares
/// (ruido, señal no periódica, varios cruces por ciclo); en ese caso hay que usar otro método.
double measureFrequencyByCrossings( const std::vector< double > &samples, double interval );
