#ifndef BB_LIB_H
#define BB_LIB_H

#include <cmath>
#include <cassert>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>

#include <QOpenGLFunctions>
#include <QDateTime>
#include <QWaitCondition>
#include <QDebug>

#include "frequency.h"
#include "amplitude.h"
#include "time_type.h"

class Trace;

typedef unsigned long ulong;
typedef struct complex_f { float re, im; } complex_f;

typedef QVector<QPointF> LineList;
typedef std::vector<float> GLVector;

struct bandwidth_lut { double bw, fft_size; };
extern bandwidth_lut native_bw_lut[];

struct span_to_bandwidth { double span, nbw, nnbw; };
extern span_to_bandwidth auto_bw_lut[];

// Must correspond to combo-box indices
enum WaterfallState { WaterfallOFF = 0, Waterfall2D = 1, Waterfall3D = 2 };
enum BBStyleSheet { LIGHT_STYLE_SHEET = 0, DARK_STYLE_SHEET = 1 };

// GLSL shaders
extern char *persist_vs;
extern char *persist_fs;

// Software Mode/State, one value stored in Settings
enum OperationalMode {
    MODE_IDLE = -1,
    MODE_SWEEPING = 0,
    MODE_REAL_TIME = 1,
    MODE_ZERO_SPAN = 2,
    MODE_TIME_GATE = 3,
    MODE_AUDIO = 7
};

enum Setting {
    CENTER = 0,
    SPAN = 1,
    START = 2,
    STOP = 3,
    RBW = 4,
    VBW = 5
};

enum GLShaderType {
    VERTEX_SHADER,
    FRAGMENT_SHADER
};

class SleepEvent {
public:
    SleepEvent() { mut.lock(); }
    ~SleepEvent() { Wake(); mut.unlock(); }

    void Sleep(ulong ms = 0xFFFFFFFFUL) { wait_con.wait(&mut, ms); }
    void Wake() { wait_con.wakeAll(); }

private:
    QMutex mut;
    QWaitCondition wait_con;
};

class GLShader {
public:
    GLShader(GLShaderType type, char *file_name);
    ~GLShader();

    // Must be called with an active context
    bool Compile(QOpenGLFunctions *gl);

    bool Compiled() const { return (compiled == GL_TRUE); }
    GLuint Handle() const { return shader_handle; }

private:
    GLShaderType shader_type;
    GLuint shader_handle;
    char *shader_source;
    int compiled;
};

class GLProgram {
public:
    GLProgram(char *vertex_file,
              char *fragment_file);
    ~GLProgram();

    bool Compile(QOpenGLFunctions *gl);
    bool Compiled() const { return (compiled == GL_TRUE); }
    GLuint Handle() const { return program_handle; }

    GLShader* GetShader(GLShaderType type) {
        if(type == VERTEX_SHADER)
            return &vertex_shader;
        else
            return &fragment_shader;
    }

private:
    GLuint program_handle;
    GLShader vertex_shader, fragment_shader;
    int compiled;
};

#if !defined(_WIN32) || defined(_WIN64)
#include <windows.h>

    // Windows Event semaphore, At one point I tested this
    //   to be more efficient than a condition variable approach
    class semaphore {
        HANDLE handle;
    public:
        semaphore() { handle = CreateEventA(nullptr, false, false, nullptr); }
        ~semaphore() { CloseHandle(handle); }

        void wait() { WaitForSingleObject(handle, INFINITE); }
        void notify() { SetEvent(handle); }
    };

#else // Linux

    // Semaphore using C++ stdlib
    class semaphore {
        std::mutex m;
        std::condition_variable cv;
        bool set;

    public:
        semaphore() : set(false) {}
        ~semaphore() { cv.notify_all(); }

        void wait() {
            std::unique_lock<std::mutex> lg(m);
            while(!set) { // while() handles spurious wakeup
                cv.wait(lg);//, [=]{ return set; });
            }
            set = false;
        }

        void notify() {
            std::lock_guard<std::mutex> lg(m);
            if(!set) {
                set = true;
                cv.notify_all();
            }
        }
    };

#endif // Semaphore


///*
// * Semaphore/WaitCondition
// * Utilizing only C++11 standard library
// * Handles spurious wake ups
// */
//class semaphore {
//    std::mutex m;
//    std::condition_variable cv;
//    bool set;

//public:
//    semaphore() : set(false) {}
//    ~semaphore() { cv.notify_all(); }

//    void wait() {
//        std::unique_lock<std::mutex> lg(m);
//        while(!set) { // while() handles spurious wakeup
//            cv.wait(lg);//, [=]{ return set; });
//        }
//        set = false;
//    }

//    void notify() {
//        std::lock_guard<std::mutex> lg(m);
//        if(!set) {
//            set = true;
//            cv.notify_all();
//        }
//    }
//};

GLuint get_texture_from_file(const QString &file_name);

namespace bb_lib {

// Returns true if a new value was retrieved
//bool GetUserInput(const Frequency &fin, Frequency &fout);
//bool GetUserInput(const Amplitude &ain, Amplitude &aout);
//bool GetUserInput(const Time &tin, Time &tout);
//bool GetUserInput(double din, double &dout);

// For copying unicode strings
// Returns length copied, including null char
// maxCopy : maximum size of dst, will copy up to maxCopy-1
//   values, then will null terminate
// Always null terminates
int cpy_16u(const ushort *src, ushort *dst, int maxCopy);

template<class _Type>
inline _Type max2(_Type a, _Type b) {
    return ((a > b) ? a : b);
}

template<class _Type>
inline _Type min2(_Type a, _Type b) {
    return ((a < b) ? a : b);
}

template<class _Type>
inline _Type max3(_Type a, _Type b, _Type c) {
    _Type d = ((a > b) ? a : b);
    return ((d > c) ? d : c);
}

template<class _Type>
inline _Type min3(_Type a, _Type b, _Type c) {
    _Type d = ((a < b) ? a : b);
    return ((d < c) ? d : c);
}

template<class _Type>
inline void clamp(_Type &val, _Type min, _Type max) {
    if(val < min) val = min;
    if(val > max) val = max;
}

// Interpolation between a -> b
// p is a 0.0 -> 1.0 value
template<typename _Type>
inline _Type lerp(_Type a, _Type b, float p) {
    return a * (1.f - p) + b * p;
}

// dB to linear voltage correction
// Used for path-loss corrections
inline void db_to_lin(float *srcDst, int len) {
    for(int i = 0; i < len; i++) {
        srcDst[i] = pow(10, srcDst[i] * 0.05);
    }
}

// Convert dBm value to mV
inline void dbm_to_mv(float *srcDst, int len) {
    for(int i = 0; i < len; i++) {
        srcDst[i] = pow(10,(srcDst[i] + 46.9897) * 0.05);
    }
}

/*
 * a ^ n
 */
inline double power(double a, int n)
{
    assert(n >= 0);

    if(n == 0)
        return 1.0;

    int r = a;
    for(int i = 0; i < n-1; i++)
        r *= a;
    return r;
}

// Get next multiple of 'factor' after start
// e.g. factor = 25, start = 38, return 50
// if start is a multiple of factor, return start
inline double next_multiple_of(double factor, double start)
{
    if(fmod(start, factor) == 0.0)
        return start;

    int m = int(start / factor) + 1;
    return m * factor;
}

// Return value [0.0, 1.0], represent fraction of
//   f between [start, stop]
inline double frac_between(double start, double stop, double f) {
    return (f - start) / (stop - start);
}

// Get the closest index representative of the bw parameter
int get_native_bw_index(double bw);
// Get next bandwidth in sequence
double sequence_bw(double bw, bool native_bw, bool increase);
// Get next 1/2/5
double sequence_span(double span, bool increase);

inline double log2(double val) {
    return log10(val) / log10(2);
}

inline int pow2(int val) {
    if(val < 0) return 0;
    return 0x1 << val;
}

inline int fft_size_from_non_native_rbw(const double rbw) {
    double min_bin_sz = rbw / 3.2;
    double min_fft = 80.0e6 / min_bin_sz;
    int order = (int)ceil(log2(min_fft));
    return pow2(order);
}

// For non-native bandwidths only
inline int get_flattop_bandwidth(double rbw) {
    return (rbw * (double)fft_size_from_non_native_rbw(rbw)) / 80.0e6;
}

// Adjust rbw to prevent small(<1) and large(>1.2m) sweep sizes
// Return true if adjustment made
bool adjust_rbw_on_span(Frequency &rbw, double span, bool native_rbw);

// Retrieve the 'best' possible rbw based on span
// 'best' is determined by the auto_bw_lut[]
double get_best_rbw(double span, bool native_rbw);

QString get_my_documents_path();

// n/a for now, shaders are static text strings
char* get_gl_shader_source(const char *file_name);

inline qint64 get_ms_since_epoch() {
    return QDateTime::currentMSecsSinceEpoch();
}
inline QDateTime get_date_time(qint64 ms_since_epoch) {
    return QDateTime::fromMSecsSinceEpoch(ms_since_epoch);
}

// File name for sweep recordings, no milliseconds
inline QString get_recording_filename()
{
    //return "funfun.sweep";
    return QDateTime::currentDateTime().toString(
                "yyyy-MM-dd hh'h'mm'm'ss's'") + ".bbr";
}

// Text string for widget display purposes, with milliseconds
inline QString get_time_string(int64_t ms_since_epoch) {
    return QDateTime::fromMSecsSinceEpoch(ms_since_epoch).toString(
                "dd/MM/yyyy hh:mm:ss:zzz");
}

} // namespace bb_lib

void normalize_trace(const Trace *t, GLVector &vector, QPoint grat_size);
//void normalize_trace(const Trace *t, LineList &ll, QSize grat_size);

#endif // BB_LIB_H