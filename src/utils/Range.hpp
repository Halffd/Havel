namespace havel {

    class RangeIter {
        int value, step;
    public:
        constexpr RangeIter(int val, int st) : value(val), step(st) {}
        constexpr int operator*() const { return value; }
        constexpr RangeIter& operator++() { value += step; return *this; }
        constexpr bool operator!=(const RangeIter& other) const {
            return step > 0 ? value < other.value : value > other.value;
        }
    };
    
    class Range {
        int start, stop, step;
    public:
        constexpr Range(int stop) : start(0), stop(stop), step(1) {}
        constexpr Range(int start, int stop, int step = 1)
            : start(start), stop(stop), step(step) {}
    
        constexpr RangeIter begin() const { return {start, step}; }
        constexpr RangeIter end() const { return {stop, step}; }
    };
    
    inline constexpr auto range(int stop) { return Range(stop); }
    inline constexpr auto range(int start, int stop, int step = 1) { return Range(start, stop, step); }
    
}
    