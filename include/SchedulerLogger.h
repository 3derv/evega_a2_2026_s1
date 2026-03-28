#ifndef SCHEDULER_LOGGER_H
#define SCHEDULER_LOGGER_H

#include <iostream>
#include <iomanip>
#include <mutex>
#include <string>

// SchedulerLogger: traza ciclo-a-ciclo del scheduler, compartida por todos los modelos.
//
// SOLID:
//   SRP — toda la lógica de formato vive aquí; los renderers solo llaman log_*.
//   OCP — agregar un modelo nuevo solo requiere añadir llamadas log_*; no tocar este archivo.
//   DIP — IRenderer::set_verbose() expone el contrato; main depende de la interfaz,
//          no de cada renderer concreto.
//   DRY — un único formato de columnas para los 5 modelos (sequential, fgmt, cgmt, smt, cmp).
//
// Thread-safety:
//   std::mutex protege la salida para modelos con OS threads reales (CMP).
//   En modelos serializados (FGMT semaforos, CGMT mutex) el mutex no contende
//   porque solo un thread escribe a la vez.
//
// Uso:
//   logger.set_max_cycles(32);               // 0 = deshabilitado
//   logger.log_header("fgmt", 4);
//   logger.log_compute(cycle, tid, x, y, PIXEL_QUANTUM_NS);
//   logger.log_stall  (cycle, tid, x, y, cost_ns, "nota del scheduler");
//   logger.log_done   (cycle, tid);
//
// Desde la línea de comandos:
//   ./build/raytracer --model fgmt --verbose 30 --runs 1
class SchedulerLogger {
public:
    // Habilitar traza para los primeros `n` ciclos de pipeline (0 = deshabilitado).
    void set_max_cycles(int n) { max_cycles_ = n; }

    // True si el ciclo indicado debe registrarse.
    bool active(int cycle) const { return max_cycles_ > 0 && cycle < max_cycles_; }

    // Encabezado del experimento: imprime modelo, threads, ancho de issue y constantes VT.
    // Llamar una vez al inicio de render_frame(), antes de lanzar los workers.
    void log_header(const std::string& model, int threads, int issue_width = 1,
                    long long pixel_quantum_ns = 1000, long long stall_penalty_ns = 3200) {
        if (max_cycles_ <= 0) return;
        std::lock_guard<std::mutex> lk(mu_);
        std::cout << "\n[" << model << "]"
                  << "  threads="       << threads
                  << "  issue_width="   << issue_width
                  << "  pixel_quantum=" << pixel_quantum_ns << "ns"
                  << "  stall_penalty=" << stall_penalty_ns << "ns\n"
                  << std::string(72, '-') << "\n";
    }

    // Pixel renderizado exitosamente: thread ocupó el slot, avanzó el PC y acumuló VT.
    void log_compute(int cycle, int tid, int x, int y, long long vt_ns) {
        if (!active(cycle)) return;
        std::lock_guard<std::mutex> lk(mu_);
        std::cout << "[C" << std::setw(5) << cycle << "]"
                  << " T" << tid << " COMPUTE"
                  << " px=(" << std::setw(3) << x << "," << std::setw(2) << y << ")"
                  << " +" << std::setw(5) << vt_ns << "ns\n";
    }

    // Stall detectado: `note` describe el efecto en el scheduler y el coste VT asociado.
    //   FGMT     → slot wasted (PIXEL_QUANTUM_NS desperdiciado, no avanza pixel)
    //   CGMT     → ctx switch→TN (CONTEXT_SWITCH_COST_NS, stall oculto)
    //   SMT      → miss→ejected (0ns, slot inmediatamente disponible para otro thread)
    //   Sequential/CMP → no ctx switch (CACHE_MISS_PENALTY_NS completo)
    void log_stall(int cycle, int tid, int x, int y, long long vt_ns,
                   const char* note = "") {
        if (!active(cycle)) return;
        std::lock_guard<std::mutex> lk(mu_);
        std::cout << "[C" << std::setw(5) << cycle << "]"
                  << " T" << tid << " STALL  "
                  << " px=(" << std::setw(3) << x << "," << std::setw(2) << y << ")"
                  << " +" << std::setw(5) << vt_ns << "ns";
        if (note && *note) std::cout << "  (" << note << ")";
        std::cout << "\n";
    }

    // Thread completó su tile — ya no tomará más slots de pipeline.
    void log_done(int cycle, int tid) {
        if (!active(cycle)) return;
        std::lock_guard<std::mutex> lk(mu_);
        std::cout << "[C" << std::setw(5) << cycle << "]"
                  << " T" << tid << " DONE\n";
    }

private:
    int        max_cycles_ = 0;
    std::mutex mu_;
};

#endif // SCHEDULER_LOGGER_H
