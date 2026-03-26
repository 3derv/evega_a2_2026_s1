#ifndef CACHE_MODEL_H
#define CACHE_MODEL_H

#include <cmath>
#include <random>

// CacheModel: Simula comportamiento de cache con localidad espacial.
// Modela hits/misses para píxeles basándose en:
//   - Proximidad espacial (píxeles cercanos reutilizan cache)
//   - Tamaño de cache (probabilidad inversamente proporcional a CACHE_SIZE)
//   - Distancia desde último miss (recuperación progresiva)
//
// Uso: Consultar is_cache_miss(x, y) para cada píxel.
//      Retorna true si hay miss → ejecutar NOPs de penalización.
class CacheModel {
private:
    int cache_size;                // Bytes de cache disponible
    int last_miss_x, last_miss_y;  // Posición del último miss
    std::mt19937 rng;              // Generador aleatorio
    std::uniform_real_distribution<double> dist;

public:
    // Constructor: inicializar con tamaño de cache y semilla opcional.
    // Cache típico L1: 32-64 KB. Ajustar según modelo deseado.
    // Semilla fija (42) por defecto → resultados deterministas y reproducibles:
    // misma escena + misma cámara = mismo patrón de cache misses en cada ejecución.
    // Para threads distintos pasar seed = BASE_SEED + thread_id.
    CacheModel(int cache_size = 32768, uint32_t seed = 42u)
        : cache_size(cache_size), last_miss_x(0), last_miss_y(0), dist(0.0, 1.0) {
        rng.seed(seed);
    }

    // is_cache_miss(): Determinar si hay miss en coordenada (x, y).
    // Probabilidad base = 1.0 / (cache_size / 64)  [64 bytes por píxel típico]
    // Ajustado por distancia: píxeles muy cercanos al último miss tienen menor prob.
    bool is_cache_miss(int x, int y) {
        // Probabilidad base inversamente proporcional al tamaño de cache
        double base_prob = 64.0 / cache_size;  // ~0.002 para 32KB cache
        
        // Distancia manhatan desde último miss
        int dx = std::abs(x - last_miss_x);
        int dy = std::abs(y - last_miss_y);
        int distance = dx + dy;
        
        // Localidad espacial: si está cerca del último miss, menor probabilidad
        // Rango típico: 0-50 píxeles de distancia
        double spatial_factor = 1.0 - std::exp(-distance / 20.0);  // Converge a 1.0
        
        double miss_prob = base_prob * spatial_factor;
        
        // Generar valor aleatorio y comparar
        bool is_miss = dist(rng) < miss_prob;
        
        // Actualizar posición de último miss si ocurre
        if (is_miss) {
            last_miss_x = x;
            last_miss_y = y;
        }
        
        return is_miss;
    }

    // reset(): Reiniciar modelo para nueva frame
    void reset() {
        last_miss_x = 0;
        last_miss_y = 0;
    }

    // Métodos para diagnosticar cache behavior (opcional)
    int get_cache_size() const { return cache_size; }
    void set_cache_size(int size) { cache_size = size; }
};

#endif // CACHE_MODEL_H
