// SPDX-License-Identifier: GPL-2.0-or-later

#include "frequencymeter.h"

#include <algorithm>
#include <cmath>

namespace {

/// Añade a `periods` la distancia (en muestras) entre cruces consecutivos del mismo sentido.
void collectPeriods( const std::vector< double > &crossings, std::vector< double > &periods ) {
    for ( size_t i = 1; i < crossings.size(); ++i )
        periods.push_back( crossings[ i ] - crossings[ i - 1 ] );
}

} // namespace


double measureFrequencyByCrossings( const std::vector< double > &samples, double interval ) {
    const size_t n = samples.size();
    if ( n < 4 || interval <= 0 )
        return 0;
    const auto [ minIt, maxIt ] = std::minmax_element( samples.begin(), samples.end() );
    const double lo = *minIt;
    const double hi = *maxIt;
    const double pp = hi - lo;
    if ( !( pp > 0 ) )
        return 0;
    const double mid = ( lo + hi ) / 2;
    const double hyst = 0.1 * pp;

    // Cruce exacto del nivel medio: retrocede desde `i` (ya fuera de la banda de histéresis) hasta el
    // último par de muestras que salta el nivel medio y lo interpola. -1 si el cruce queda antes del
    // inicio del registro o antes de `limit` (el cruce anterior).
    auto crossing = [ & ]( size_t i, bool rising, size_t limit ) -> double {
        size_t k = i;
        while ( k > limit && ( rising ? samples[ k - 1 ] > mid : samples[ k - 1 ] < mid ) )
            --k;
        if ( k == 0 || k <= limit )
            return -1;
        const double a = samples[ k - 1 ];
        const double b = samples[ k ];
        return double( k - 1 ) + ( b != a ? ( mid - a ) / ( b - a ) : 0.5 );
    };

    // Antirrebote: un cambio de nivel solo cuenta si se mantiene `kRun` muestras seguidas al otro lado
    // de la histéresis. Así una muestra suelta errónea (un pico de ruido; hasta dso5200basic 047ed81
    // también la muestra 0 del registro, que era la última del búfer circular) no se toma como flanco
    // ni como nivel inicial.
    constexpr size_t kRun = 3;
    auto side = [ & ]( double v ) { return v > mid + hyst ? 1 : ( v < mid - hyst ? -1 : 0 ); };

    std::vector< double > rises;
    std::vector< double > falls;
    int level = 0;      // nivel establecido: 1 alto, -1 bajo, 0 aún desconocido
    int candidate = 0;  // nivel al que parece estar cambiando
    size_t runStart = 0;
    size_t runLength = 0;
    size_t last = 0;    // dónde se estableció el nivel anterior: el cruce no puede estar antes
    for ( size_t i = 0; i < n; ++i ) {
        const int s = side( samples[ i ] );
        if ( s == 0 || s == level ) {
            if ( s == level )
                runLength = 0; // volvió al nivel establecido: era un pico suelto
            continue;
        }
        if ( s != candidate || runLength == 0 ) {
            candidate = s;
            runStart = i;
            runLength = 0;
        }
        if ( ++runLength < kRun )
            continue;
        if ( level != 0 ) { // cambio real de nivel: se ubica el cruce del nivel medio
            const double t = crossing( runStart, s > 0, last );
            if ( t >= 0 )
                ( s > 0 ? rises : falls ).push_back( t );
        }
        level = s;
        last = runStart;
        runLength = 0;
    }

    std::vector< double > periods;
    collectPeriods( rises, periods );
    collectPeriods( falls, periods );
    if ( periods.empty() )
        return 0;

    // Regularidad: con ruido o con varios cruces por ciclo los periodos individuales difieren mucho.
    double sum = 0;
    for ( double p : periods )
        sum += p;
    const double mean = sum / double( periods.size() );
    for ( double p : periods )
        if ( std::fabs( p - mean ) > 0.05 * mean )
            return 0;

    // Periodo: el tramo más largo cubierto por cruces del mismo sentido, que promedia todos los ciclos.
    double span = 0;
    double cycles = 0;
    if ( rises.size() >= 2 ) {
        span += rises.back() - rises.front();
        cycles += double( rises.size() - 1 );
    }
    if ( falls.size() >= 2 ) {
        span += falls.back() - falls.front();
        cycles += double( falls.size() - 1 );
    }
    if ( span <= 0 )
        return 0;
    return cycles / ( span * interval );
}
