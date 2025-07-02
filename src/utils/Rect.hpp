#ifndef RECT_HPP
#define RECT_HPP

namespace havel {
    struct Rect {
        int x;
        int y;
        int left;
        int top;
        int width;
        int height;
        int right;
        int bottom;
        Rect() = default;
        Rect(int x, int y, int w, int h)
            : x(x), y(y), left(x), top(y), width(w), height(h), right(x + w), bottom(y + h) {}
        Rect(int x, int y, int l, int t, int w, int h)
            : x(x), y(y), left(l), top(t), width(w), height(h), right(l + w), bottom(t + h) {}

        int area() const { return width * height; }

        bool contains(int x, int y) const {
            return (x >= left && x <= right && y >= top && y <= bottom);
        }
    };
}

#endif // RECT_HPP
