#ifndef EXPORTER_H
#define EXPORTER_H

#include "Constants.h"
#include "Metrics.h"
#include "Vector3.h"
#include <string>
#include <vector>

namespace trace {

class Exporter {
public:
    Exporter() = default;

    void save_image(const std::vector<Vector3>& frame) const;
    void save_csv(const Metrics& metrics) const;

private:
    std::string image_file_ = constants::IMAGE_FILE;
    std::string csv_file_ = constants::CSV_FILE;
};

} // namespace trace

#endif // EXPORTER_H