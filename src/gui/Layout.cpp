#include "Layout.hpp"
#include <algorithm>

namespace havel {

// Base Layout implementation
void Layout::AddWidget(std::shared_ptr<Widget> widget) {
    if (!widget) return;
    
    // Remove from any existing parent
    if (auto parent = widget->GetParent()) {
        parent->RemoveWidget(widget);
    }
    
    // Add to this layout
    widget->SetParent(shared_from_this());
    children.push_back(widget);
    
    // Update layout
    UpdateLayout();
}

void Layout::RemoveWidget(std::shared_ptr<Widget> widget) {
    if (!widget) return;
    
    auto it = std::find(children.begin(), children.end(), widget);
    if (it != children.end()) {
        widget->SetParent(nullptr);
        children.erase(it);
        UpdateLayout();
    }
}

void Layout::Clear() {
    for (auto& child : children) {
        child->SetParent(nullptr);
    }
    children.clear();
    UpdateLayout();
}

// BoxLayout implementation
void BoxLayout::UpdateLayout() {
    if (children.empty()) return;
    
    int availableWidth = bounds.width - 2 * margin - (children.size() - 1) * spacing;
    int availableHeight = bounds.height - 2 * margin - (children.size() - 1) * spacing;
    
    // Calculate total size of non-expanding widgets
    int fixedSize = 0;
    int expandingCount = 0;
    
    for (const auto& child : children) {
        if (child->GetSizePolicy() & SizePolicy::Expand) {
            expandingCount++;
        } else {
            fixedSize += (orientation == Orientation::Horizontal) ? 
                child->GetPreferredSize().width : child->GetPreferredSize().height;
        }
    }
    
    // Calculate size for expanding widgets
    int remaining = (orientation == Orientation::Horizontal) ? 
        std::max(0, availableWidth - fixedSize) : 
        std::max(0, availableHeight - fixedSize);
    
    int expandSize = expandingCount > 0 ? remaining / expandingCount : 0;
    
    // Position widgets
    int pos = margin;
    
    for (auto& child : children) {
        Size preferred = child->GetPreferredSize();
        Size min = child->GetMinimumSize();
        Size max = child->GetMaximumSize();
        
        int width = preferred.width;
        int height = preferred.height;
        
        if (orientation == Orientation::Horizontal) {
            if (child->GetSizePolicy() & SizePolicy::Expand) {
                width = std::max(min.width, std::min(expandSize, max.width));
            }
            child->SetBounds(bounds.x + pos, bounds.y + margin, width, bounds.height - 2 * margin);
            pos += width + spacing;
        } else {
            if (child->GetSizePolicy() & SizePolicy::Expand) {
                height = std::max(min.height, std::min(expandSize, max.height));
            }
            child->SetBounds(bounds.x + margin, bounds.y + pos, bounds.width - 2 * margin, height);
            pos += height + spacing;
        }
    }
}

// GridLayout implementation
void GridLayout::UpdateLayout() {
    if (children.empty()) return;
    
    int itemWidth = (bounds.width - (columns - 1) * hSpacing - 2 * margin) / columns;
    int itemHeight = (bounds.height - (rows - 1) * vSpacing - 2 * margin) / rows;
    
    for (int i = 0; i < children.size(); ++i) {
        int row = i / columns;
        int col = i % columns;
        
        if (row >= rows) break;
        
        int x = bounds.x + margin + col * (itemWidth + hSpacing);
        int y = bounds.y + margin + row * (itemHeight + vSpacing);
        
        children[i]->SetBounds(x, y, itemWidth, itemHeight);
    }
}

} // namespace havel
