#ifndef RECT_HPP
#define RECT_HPP

namespace havel {
    struct Rect {
        int left;
        int top;
        int width;
        int height;

        Rect(int l, int t, int w, int h)
            : left(l), top(t), width(w), height(h) {}

        int right() const { return left + width; }
        int bottom() const { return top + height; }
        int area() const { return width * height; }

        bool contains(int x, int y) const {
            return (x >= left && x <= right() && y >= top && y <= bottom());
        }
    };
}

#endif // RECT_HPP
