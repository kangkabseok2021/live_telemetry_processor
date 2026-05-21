#pragma once
#include <cstdint>

namespace imaging {

class ImagePipeline {
public:
    void calibrate(int steps);
    void startAcquisition();
    void stopAcquisition();
    void processBatch(uint32_t batch_size);
    bool isBusy() const noexcept;

private:
    bool busy_{false};
};

} // namespace imaging
