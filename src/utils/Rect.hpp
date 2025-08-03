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
        int w;
        int h;
        Rect() = default;
        Rect(int x, int y) : x(x), y(y), left(x), top(y), width(0), height(0), right(x), bottom(y), w(0), h(0) {}
        Rect(int x, int y, int w, int h)
            : x(x), y(y), left(x), top(y), width(w), height(h), right(x + w), bottom(y + h), w(w), h(h) {}
        Rect(int x, int y, int l, int t, int w, int h)
            : x(x), y(y), left(l), top(t), width(w), height(h), right(l + w), bottom(t + h), w(w), h(h) {}

        int area() const { return width * height; }

        bool contains(int x, int y) const {
            return (x >= left && x <= right && y >= top && y <= bottom);
        }
    };
}

#endif // RECT_HPP
