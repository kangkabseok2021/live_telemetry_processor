#include "ImagePipeline.h"

namespace imaging {

void ImagePipeline::calibrate(int /*steps*/)            { busy_ = false; }
void ImagePipeline::startAcquisition()                   { busy_ = false; }
void ImagePipeline::stopAcquisition()                    { busy_ = false; }
void ImagePipeline::processBatch(uint32_t /*batch_size*/) { busy_ = false; }
bool ImagePipeline::isBusy() const noexcept              { return busy_; }

} // namespace imaging
