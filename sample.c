// sample.c - Program designed to benefit from our A7 optimizations
// Performs heavy array computation with patterns that exercise:
//   - Function Inlining (small helpers called in tight loops)
//   - LICM (global N loaded inside every loop iteration)
//   - CSE (i*N computed redundantly, same index computed twice)
//   - Constant Folding (compile-time constant arithmetic)
//   - Algebraic Simplification (x+0, x*1 patterns)
//   - DCE (unused computations after propagation)

#include "minicio.h"

int N;
int grid[400];
int tmp[400];

// Small helper: get value at (r,c) -- inlining target
int getval(int r, int c) {
    return grid[r * N + c];
}

// Small helper: set value at (r,c) -- inlining target
void setval(int r, int c, int v) {
    grid[r * N + c] = v;
}

// Small helper: get temp at (r,c) -- inlining target
int gettmp(int r, int c) {
    return tmp[r * N + c];
}

// Small helper: set temp at (r,c) -- inlining target
void settmp(int r, int c, int v) {
    tmp[r * N + c] = v;
}

// Initialize grid with a pattern
// After inlining: inner loop has redundant i*N, j*N computations -> CSE
// Global N loaded every iteration -> LICM
void init() {
    int i;
    int j;
    for (i = 0; i < N; i = i + 1)
        for (j = 0; j < N; j = j + 1)
            setval(i, j, (i * N + j) * 1 + 0);
}

// Compute each cell as average of its neighbors (clamped to bounds)
// Heavy use of getval -> inlining turns these into direct array accesses
// Then LICM hoists N loads, CSE eliminates duplicate i*N calculations
void smooth() {
    int i;
    int j;
    int sum;
    int count;
    for (i = 0; i < N; i = i + 1) {
        for (j = 0; j < N; j = j + 1) {
            sum = getval(i, j);
            count = 1;
            if (i > 0) {
                sum = sum + getval(i - 1, j);
                count = count + 1;
            }
            if (i < N - 1) {
                sum = sum + getval(i + 1, j);
                count = count + 1;
            }
            if (j > 0) {
                sum = sum + getval(i, j - 1);
                count = count + 1;
            }
            if (j < N - 1) {
                sum = sum + getval(i, j + 1);
                count = count + 1;
            }
            settmp(i, j, sum / count);
        }
    }
    // Copy tmp back to grid
    for (i = 0; i < N; i = i + 1)
        for (j = 0; j < N; j = j + 1)
            setval(i, j, gettmp(i, j));
}

// Sum all values in the grid -- inlining + LICM + CSE target
int total() {
    int i;
    int j;
    int s;
    s = 0;
    for (i = 0; i < N; i = i + 1)
        for (j = 0; j < N; j = j + 1)
            s = s + getval(i, j);
    return s;
}

int main() {
    int iters;
    int k;
    N = getint();
    iters = getint();
    init();
    for (k = 0; k < iters; k = k + 1)
        smooth();
    putint(total());
    putnewline();
    return 0;
}
