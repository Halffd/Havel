#pragma once
#include <memory>
#include <vector>
#include "Widget.hpp"

namespace havel {

class Layout {
public:
    virtual ~Layout() = default;
    
    // Core layout interface
    virtual void AddWidget(std::shared_ptr<Widget> widget) = 0;
    virtual void RemoveWidget(std::shared_ptr<Widget> widget) = 0;
    virtual void UpdateLayout() = 0;
    
    // Layout properties
    virtual void SetSpacing(int spacing) { this->spacing = spacing; }
    virtual void SetMargins(int left, int top, int right, int bottom) {
        margins[0] = left;
        margins[1] = top;
        margins[2] = right;
        margins[3] = bottom;
    }
    
    // Child management
    virtual size_t GetChildCount() const { return children.size(); }
    virtual std::shared_ptr<Widget> GetChild(size_t index) const {
        return (index < children.size()) ? children[index] : nullptr;
    }
    
    // Parent widget
    virtual void SetParent(Widget* parent) { this->parent = parent; }
    virtual Widget* GetParent() const { return parent; }
    
protected:
    std::vector<std::shared_ptr<Widget>> children;
    int spacing{0};
    int margins[4]{0, 0, 0, 0}; // left, top, right, bottom
    Widget* parent{nullptr};
};

// Common layout implementations
class BoxLayout : public Layout {
public:
    enum class Direction { HORIZONTAL, VERTICAL };
    
    explicit BoxLayout(Direction dir = Direction::HORIZONTAL) : direction(dir) {}
    
    void AddWidget(std::shared_ptr<Widget> widget) override {
        if (widget) {
            children.push_back(widget);
            widget->SetParent(parent);
            UpdateLayout();
        }
    }
    
    void RemoveWidget(std::shared_ptr<Widget> widget) override {
        auto it = std::find(children.begin(), children.end(), widget);
        if (it != children.end()) {
            (*it)->SetParent(nullptr);
            children.erase(it);
            UpdateLayout();
        }
    }
    
    void UpdateLayout() override {
        if (!parent) return;
        
        // Get parent bounds
        int x = margins[0];
        int y = margins[1];
        int width = parent->GetWidth() - margins[0] - margins[2];
        int height = parent->GetHeight() - margins[1] - margins[3];
        
        // Calculate sizes
        if (direction == Direction::HORIZONTAL) {
            int childWidth = (width - (children.size() - 1) * spacing) / children.size();
            for (auto& child : children) {
                child->SetBounds(x, y, childWidth, height);
                x += childWidth + spacing;
            }
        } else { // VERTICAL
            int childHeight = (height - (children.size() - 1) * spacing) / children.size();
            for (auto& child : children) {
                child->SetBounds(x, y, width, childHeight);
                y += childHeight + spacing;
            }
        }
    }
    
    void SetDirection(Direction dir) { 
        direction = dir; 
        UpdateLayout();
    }
    
private:
    Direction direction{Direction::HORIZONTAL};
};

} // namespace havel
