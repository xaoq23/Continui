// Painter forwards into the frame's batch builder; behavior of each call is
// documented in painter.hpp, batching in batcher.hpp.
#include <continui/painter.hpp>

#include "batcher.hpp"

namespace continui {

void Painter::rect(const Rect bounds, const Paint paint) {
    batches_->rect(bounds, paint);
}

void Painter::rounded_rect(const Rect bounds, const Radius radius,
                           const Paint paint) {
    batches_->rounded_rect(bounds, radius, paint);
}

void Painter::image(const Texture& texture, const Rect bounds) {
    if (!texture.valid()) {
        return;
    }
    batches_->image(texture.id_, bounds);
}

void Painter::text(const GlyphRun& run, const PointF origin, const Paint& paint) {
    if (text_ != nullptr) {
        batches_->text(run, origin, paint, *text_);
    }
}

void Painter::clip(const Rect bounds) {
    batches_->clip(bounds);
}

}  // namespace continui
