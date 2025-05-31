#include "Widget.hpp"
#include <algorithm>

namespace havel {

Widget::Widget() = default;

void Widget::SetBounds(int x, int y, int width, int height) {
    if (bounds.x == x && bounds.y == y && 
        bounds.width == width && bounds.height == height) {
        return;
    }
    
    bounds.x = x;
    bounds.y = y;
    bounds.width = width;
    bounds.height = height;
    
    NeedsRedraw();
    InvalidateLayout();
}

void Widget::SetParent(Widget* parent) {
    if (this->parent == parent) return;
    this->parent = parent;
    InvalidateLayout();
}

void Widget::StartAnimation(const std::string& name, float duration) {
    animations[name] = Animation{
        .duration = duration,
        .currentTime = 0,
        .startValue = 0,
        .endValue = 1,
        .running = true
    };
    NeedsRedraw();
}

void Widget::StopAnimation(const std::string& name) {
    auto it = animations.find(name);
    if (it != animations.end()) {
        it->second.running = false;
        NeedsRedraw();
    }
}

void Widget::UpdateAnimations(float deltaTime) {
    bool needsUpdate = false;
    
    for (auto it = animations.begin(); it != animations.end();) {
        auto& anim = it->second;
        if (!anim.running) {
            it = animations.erase(it);
            needsUpdate = true;
            continue;
        }
        
        anim.currentTime += deltaTime;
        if (anim.currentTime >= anim.duration) {
            if (anim.callback) {
                anim.callback(anim.endValue);
            }
            it = animations.erase(it);
            needsUpdate = true;
        } else {
            float progress = anim.currentTime / anim.duration;
            float value = anim.startValue + (anim.endValue - anim.startValue) * progress;
            if (anim.callback) {
                anim.callback(value);
            }
            ++it;
            needsUpdate = true;
        }
    }
    
    if (needsUpdate) {
        NeedsRedraw();
    }
}

void Widget::NeedsRedraw() {
    needsRedraw = true;
    if (parent) {
        parent->NeedsRedraw();
    }
}

void Widget::InvalidateLayout() {
    needsLayout = true;
    if (parent) {
        parent->InvalidateLayout();
    }
}

} // namespace havel 